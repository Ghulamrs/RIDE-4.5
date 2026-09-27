
#include "bridge.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>

#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

#include <richedit.h>
#include <richole.h>
#include <tom.h>

#pragma comment(lib, "user32.lib")
#endif

#include "about.h"
#include "compile.h"
#include "convert.h"
#include "debugger.h"
#include "shalimar/session.h"
#include "find.h"
#include "indent.h"
#include "product.h"
#include "path.h"
#include "project.h"
#include "symbols.h"
#include "syntax.h"
#include "settings.h"
#include "workspace.h"

namespace {

static_assert(RIDE_KIND_KEYWORD == static_cast<int>(editor::KindKeyword), "kind numbering has drifted");
static_assert(RIDE_KIND_LABEL == static_cast<int>(editor::KindLabel), "kind numbering has drifted");
static_assert(RIDE_LANG_CPP == static_cast<int>(editor::LangCpp), "language numbering has drifted");
static_assert(RIDE_LANG_SHALIMAR == static_cast<int>(editor::LangShalimar), "language numbering has drifted");
static_assert(RIDE_LANG_ASM == static_cast<int>(editor::LangAsm), "language numbering has drifted");
static_assert(RIDE_LANG_JSON == static_cast<int>(editor::LangJson), "language numbering has drifted");
static_assert(RIDE_TOOL_MSVC == static_cast<int>(editor::ToolMsvc), "toolchain numbering has drifted");
static_assert(RIDE_TOOL_SHC == static_cast<int>(editor::ToolShc), "toolchain numbering has drifted");
static_assert(RIDE_TOOL_CXX == static_cast<int>(editor::ToolCxx), "toolchain numbering has drifted");
static_assert(RIDE_TOOL_CXX1 == static_cast<int>(editor::ToolCxx1), "toolchain numbering has drifted");
static_assert(RIDE_CONFIG_RELEASE == static_cast<int>(editor::ConfigRelease), "config numbering has drifted");

char* give(const std::string& text) {
    char* out = static_cast<char*>(std::malloc(text.size() + 1));
    if (!out) return 0;
    std::memcpy(out, text.c_str(), text.size() + 1);
    return out;
}

// A message into the caller's buffer, cut where it must be at the start of a UTF-8 character, so
// the last one is never half a character that the window shows as a replacement mark (L14).
void copyOut(char* into, int size, const std::string& text) {
    size_t room = static_cast<size_t>(size) - 1;
    size_t n = text.size() < room ? text.size() : room;
    if (n < text.size())
        while (n > 0 && (static_cast<unsigned char>(text[n]) & 0xC0) == 0x80) --n;
    std::memcpy(into, text.data(), n);
    into[n] = '\0';
}

std::vector<std::string> split(const char* text) {
    std::vector<std::string> lines;
    std::string line;
    for (const char* p = text ? text : ""; *p; ++p) {
        if (*p == '\n') {
            lines.push_back(line);
            line.clear();
        } else if (*p != '\r') {
            line += *p;
        }
    }
    lines.push_back(line);
    return lines;
}

std::string join(const std::vector<std::string>& lines) {
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) out += "\n";
        out += lines[i];
    }
    return out;
}

editor::IndentStyle styleOf(int width, int tabs, int caseIndent, int dialect) {
    editor::IndentStyle style;
    if (width >= 1 && width <= 16) style.width = static_cast<size_t>(width);
    style.tabs = tabs != 0;
    style.caseIndent = caseIndent ? 1 : 0;
    style.dialect = dialect == RIDE_DIALECT_SHALIMAR ? editor::DialectShalimar
                                                    : editor::DialectC;
    return style;
}

// **Made once and never destroyed, and every string this file hands back goes through it.** A
// function-local static with a destructor registers an atexit handler when first reached, and in
// this mixed image that corrupts the heap - Json::get did, and ride_group_for_file again on 2026-09-11. tests/test.cpp scans the window's sources for the shape.
std::string& scratch() {
    static std::string* kept = new std::string();
    return *kept;
}

#ifdef _WIN32

char faultLog[MAX_PATH] = "";   // set before main, by EarlyWatch below

void write(FILE* f, const char* text) { std::fputs(text, f); }

// The stack, symbolised, in a frame of its own: dbghelp loads, locks and allocates, which is no
// place to be on a stack that has just overflowed, so onFault never calls this for one.
__declspec(noinline) void writeStack(FILE* f) {
    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, NULL, TRUE);

    void* frames[62];
    USHORT got = RtlCaptureStackBackTrace(0, 62, frames, NULL);

    char room[sizeof(SYMBOL_INFO) + 512];
    SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(room);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 500;

    for (USHORT i = 0; i < got; ++i) {
        DWORD64 address = reinterpret_cast<DWORD64>(frames[i]);
        DWORD64 offset = 0;

        if (SymFromAddr(process, address, &offset, symbol)) {
            IMAGEHLP_LINE64 line;
            line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            DWORD column = 0;
            if (SymGetLineFromAddr64(process, address, &column, &line))
                std::fprintf(f, "  %2u  %s  (%s:%lu)\n", i, symbol->Name, line.FileName,
                             static_cast<unsigned long>(line.LineNumber));
            else
                std::fprintf(f, "  %2u  %s + 0x%llX\n", i, symbol->Name,
                             static_cast<unsigned long long>(offset));
        } else {
            std::fprintf(f, "  %2u  %p\n", i, frames[i]);
        }
    }

    SymCleanup(process);
}

// Native faults only. A managed exception is not logged here: they are routine, handled ones
// arrive first-chance, and the window's own ThreadException handler logs the one that escapes.
LONG CALLBACK onFault(EXCEPTION_POINTERS* info) {
    static bool inside = false;
    if (inside) return EXCEPTION_CONTINUE_SEARCH;

    DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != STATUS_HEAP_CORRUPTION &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_ILLEGAL_INSTRUCTION)
        return EXCEPTION_CONTINUE_SEARCH;
    inside = true;

    // Kept to a megabyte: past that it is begun again rather than grown for ever.
    const char* where = faultLog[0] ? faultLog : "fault.log";
    WIN32_FILE_ATTRIBUTE_DATA facts;
    bool full = GetFileAttributesExA(where, GetFileExInfoStandard, &facts) &&
                (facts.nFileSizeHigh != 0 || facts.nFileSizeLow > 1024 * 1024);
    FILE* f = std::fopen(where, full ? "w" : "a");
    if (!f) {
        inside = false;
        return EXCEPTION_CONTINUE_SEARCH;
    }

    std::fprintf(f, "\nexception 0x%08lX at %p\n", static_cast<unsigned long>(code),
                 info->ExceptionRecord->ExceptionAddress);
    if (code == EXCEPTION_ACCESS_VIOLATION &&
        info->ExceptionRecord->NumberParameters >= 2) {
        std::fprintf(f, "  %s address %p\n",
                     info->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
                     reinterpret_cast<void*>(info->ExceptionRecord->ExceptionInformation[1]));
    }

    if (code == EXCEPTION_STACK_OVERFLOW) std::fputs("  stack overflow - no stack is walked on a full one\n", f);
    else writeStack(f);

    std::fclose(f);
    inside = false;
    return EXCEPTION_CONTINUE_SEARCH;
}

#endif

std::string joinedList(const std::vector<std::string>& parts) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) out += (i ? ";" : "") + parts[i];
    return out;
}

std::vector<std::string> splitList(const char* line) {
    std::vector<std::string> out;
    std::string text = line ? line : "";
    size_t at = 0;
    while (at <= text.size()) {
        size_t cut = text.find(';', at);
        if (cut == std::string::npos) cut = text.size();
        std::string part = text.substr(at, cut - at);
        size_t a = part.find_first_not_of(" \t"), b = part.find_last_not_of(" \t");
        if (a != std::string::npos) out.push_back(part.substr(a, b - a + 1));
        at = cut + 1;
    }
    return out;
}




}

struct RIDEProject {
    editor::Project project;
    std::string answer;
    editor::Outcome last;

    std::vector<std::string> sources;

    std::vector<editor::Part> parts;
    std::string program;
    std::string why;
    std::string detail;
    int language;

    editor::DebugPlan plan;
    std::string whyNot;
};

namespace {

// A bare tool name made a full path - beside the editor, then on PATH - so the shell a build runs
// through never looks in the current directory for it (M14); a name found nowhere stays bare.
std::string resolved(const std::string& name) {
    if (name.empty() || name.find_first_of("/\\") != std::string::npos) return name;
    std::string found = editor::path::besideProgram(name);
    if (found.empty()) found = editor::path::onPath(name);
    return found.empty() ? name : found;
}

// The toolchain every build is given: the compilers as named, the
// installation's header directories, and the project's own paths when a
// project is open. The window has one project and passes it, loaded or not.
editor::Toolchain toolFrom(RIDEProject* project, const char* cc1, const char* cl,
                           const char* shc, const char* cxx1) {
    editor::Toolchain tool;
    if (cc1 && *cc1) tool.cc1 = cc1;
    if (cl && *cl) tool.cl = cl;
    if (shc && *shc) tool.shc = shc;
    if (cxx1 && *cxx1) tool.cxx1 = cxx1;
    tool.cc1 = resolved(tool.cc1);
    tool.cl = resolved(tool.cl);
    tool.shc = resolved(tool.shc);
    tool.cxx1 = resolved(tool.cxx1);
    tool.cxx = resolved(tool.cxx);
    tool.include = editor::settings::includeDir();
    tool.lib = editor::settings::libDir();
    if (project && project->project.loaded()) {
        tool.includes = project->project.absoluteIncludes();
        tool.libraries = project->project.absoluteLibraries();
    }
    std::vector<std::string> shared = editor::settings::includes();
    tool.includes.insert(tool.includes.end(), shared.begin(), shared.end());
    shared = editor::settings::libraries();
    tool.libraries.insert(tool.libraries.end(), shared.begin(), shared.end());
    return tool;
}

}

struct RIDEBuild {
    editor::Build built;
    std::string assembly;
    bool stopped = false;
};

struct RIDERan {
    editor::Ran ran;
    bool stopped = false;
};

struct RIDEProgram {
    editor::Built built;
};

struct RIDEDebugger {
    editor::Debugger debugger;

    shalimar::Session shm;
    editor::Stop stop;
    std::vector<editor::Variable> locals;
    std::vector<editor::StackFrame> stack;
    size_t looking;
    std::string frameLine;
    std::string variableLine;
    std::string watchLine;
    std::string lookingLine;
    std::string complaint;
    std::string answer;
    std::string output;
    std::string refusal;

    RIDEDebugger() : looking(0) {}
};

extern "C" {

void ride_watch_for_faults(const char* logPath) {
#ifdef _WIN32
    static bool watching = false;
    if (logPath && *logPath) {
        std::strncpy(faultLog, logPath, sizeof faultLog - 1);
        faultLog[sizeof faultLog - 1] = '\0';
    }
    if (!watching) AddVectoredExceptionHandler(1, onFault);
    watching = true;
#else
    (void)logPath;
#endif
}

#ifdef _WIN32
// Watching from the first native initialiser, before main - a window that
// dies on the way to main leaves nothing otherwise, and one did.
namespace {
struct EarlyWatch {
    EarlyWatch() {
        char temp[MAX_PATH];
        DWORD n = GetEnvironmentVariableA("TEMP", temp, MAX_PATH);
        const std::string leaf = std::string(editor::product::kName) + "-fault.log";
        std::string where = (n > 0 && n < MAX_PATH) ? std::string(temp) + "\\" + leaf : leaf;
        ride_watch_for_faults(where.c_str());
    }
} earlyWatch;
}
#endif

#ifdef _WIN32

static void undoRecording(void* windowHandle, long how) {
    HWND window = reinterpret_cast<HWND>(windowHandle);
    if (!window) return;

    IRichEditOle* ole = 0;
    SendMessage(window, EM_GETOLEINTERFACE, 0, reinterpret_cast<LPARAM>(&ole));
    if (!ole) return;

    ITextDocument* document = 0;
    if (SUCCEEDED(ole->QueryInterface(__uuidof(ITextDocument),
                                      reinterpret_cast<void**>(&document))) &&
        document) {
        document->Undo(how, 0);
        document->Release();
    }
    ole->Release();
}
#endif

const char* ride_settings_set_aside(void) {
    scratch() = editor::settings::setAside();
    return scratch().c_str();
}

const char* ride_code_font(void) {
    scratch() = editor::settings::codeFont();
    return scratch().c_str();
}

int ride_remember_code_font(const char* described) {
    return editor::settings::rememberCodeFont(described ? described : "") ? 1 : 0;
}

void ride_undo_suspend(void* windowHandle) {
#ifdef _WIN32
    undoRecording(windowHandle, tomSuspend);
#else
    (void)windowHandle;
#endif
}

void ride_undo_resume(void* windowHandle) {
#ifdef _WIN32
    undoRecording(windowHandle, tomResume);
#else
    (void)windowHandle;
#endif
}

char* ride_reindent(const char* text, int width, int tabs, int caseIndent, int dialect) {
    return give(join(editor::reindent(split(text),
                                      styleOf(width, tabs, caseIndent, dialect))));
}

char* ride_indent_after_newline(const char* text, int row, int col,
                               int width, int tabs, int caseIndent, int dialect) {
    if (row < 0) row = 0;
    if (col < 0) col = 0;
    return give(editor::indentAfterNewline(split(text), static_cast<size_t>(row),
                                           static_cast<size_t>(col),
                                           styleOf(width, tabs, caseIndent, dialect)));
}

char* ride_indent_for(const char* text, int row, int width, int tabs, int caseIndent,
                     int dialect) {
    if (row < 0) row = 0;
    std::vector<std::string> lines = split(text);
    return give(editor::indentFor(lines, static_cast<size_t>(row),
                                  styleOf(width, tabs, caseIndent, dialect)));
}

void ride_free(char* what) { std::free(what); }

char* ride_about(void) { return give(join(editor::about::lines())); }

char* ride_environment(void) { return give(join(editor::about::environment())); }

char* ride_describe_build(const char* assembly) {
    return give(join(editor::describe(editor::symbolsIn(split(assembly)))));
}

char* ride_debug_note(int kind, const char* arch) {
    return give(join(editor::debugNote(static_cast<editor::ToolchainKind>(kind),
                                       arch ? arch : "")));
}

int ride_find_next(const char* text, const char* needle, int row, int col,
                  int* foundRow, int* foundCol) {
    if (row < 0) row = 0;
    if (col < 0) col = 0;
    editor::Match match = editor::findNext(split(text), needle ? needle : "",
                                           static_cast<size_t>(row),
                                           static_cast<size_t>(col));
    if (!match.found) return 0;
    if (foundRow) *foundRow = static_cast<int>(match.row);
    if (foundCol) *foundCol = static_cast<int>(match.col);
    return 1;
}

int ride_find_previous(const char* text, const char* needle, int row, int col,
                      int* foundRow, int* foundCol) {
    if (row < 0) row = 0;
    if (col < 0) col = 0;
    editor::Match match = editor::findPrevious(split(text), needle ? needle : "",
                                               static_cast<size_t>(row),
                                               static_cast<size_t>(col));
    if (!match.found) return 0;
    if (foundRow) *foundRow = static_cast<int>(match.row);
    if (foundCol) *foundCol = static_cast<int>(match.col);
    return 1;
}

char* ride_replace_all(const char* text, const char* needle, const char* with,
                      int* howMany) {
    std::vector<std::string> lines = split(text);
    size_t count = editor::replaceAll(lines, needle ? needle : "", with ? with : "");
    if (howMany) *howMany = static_cast<int>(count);
    return give(join(lines));
}

int ride_language_for(const char* path) {
    return static_cast<int>(editor::languageFor(path ? path : ""));
}

int ride_dialect_for(int language) {
    return language == RIDE_LANG_SHALIMAR ? RIDE_DIALECT_SHALIMAR : RIDE_DIALECT_C;
}

int ride_highlight(const char* line, int language, int* state,
                  unsigned char* kinds, int kindsSize) {
    editor::SyntaxState carried;
    if (state) {
        carried.comment = (*state & 1) != 0;
        carried.string = (*state & 2) != 0;
    }

    std::vector<unsigned char> worked =
        editor::highlight(line ? line : "", static_cast<editor::Language>(language), carried);

    if (state) *state = (carried.comment ? 1 : 0) | (carried.string ? 2 : 0);

    int n = static_cast<int>(worked.size());
    if (n > kindsSize) n = kindsSize;
    for (int i = 0; i < n; ++i) kinds[i] = worked[static_cast<size_t>(i)];
    return n;
}

RIDEProject* ride_project_new(void) { return new RIDEProject(); }
void ride_project_free(RIDEProject* project) { delete project; }

int ride_project_load(RIDEProject* project, const char* directory,
                     char* error, int errorSize) {
    if (!project) return 0;
    std::string why;
    bool loaded = project->project.load(directory ? directory : ".", why);
    if (error && errorSize > 0) {
        copyOut(error, errorSize, why);
    }
    return loaded ? 1 : 0;
}

const char* ride_project_name(RIDEProject* project) {
    if (!project) return "";
    project->answer = project->project.name();
    return project->answer.c_str();
}

int ride_project_groups(RIDEProject* project) {
    if (!project) return 0;
    return static_cast<int>(project->project.groups().size());
}

const char* ride_project_group_name(RIDEProject* project, int group) {
    if (!project) return "";
    if (group < 0 || group >= ride_project_groups(project)) return "";
    project->answer = project->project.groups()[static_cast<size_t>(group)].name;
    return project->answer.c_str();
}

int ride_project_files(RIDEProject* project, int group) {
    if (!project) return 0;
    if (group < 0 || group >= ride_project_groups(project)) return 0;
    return static_cast<int>(
        project->project.groups()[static_cast<size_t>(group)].files.size());
}

const char* ride_project_file(RIDEProject* project, int group, int file) {
    if (!project) return "";
    if (file < 0 || file >= ride_project_files(project, group)) return "";
    project->answer =
        project->project.groups()[static_cast<size_t>(group)].files[static_cast<size_t>(file)];
    return project->answer.c_str();
}

const char* ride_project_absolute(RIDEProject* project, const char* relative) {
    if (!project) return "";
    project->answer = project->project.absolute(relative ? relative : "");
    return project->answer.c_str();
}

int ride_project_indent_width(RIDEProject* project) {
    if (!project) return 0;
    return static_cast<int>(project->project.indent().width);
}
int ride_project_indent_tabs(RIDEProject* project) {
    if (!project) return 0;
    return project->project.indent().tabs ? 1 : 0;
}
int ride_project_case_indent(RIDEProject* project) {
    if (!project) return 0;
    return static_cast<int>(project->project.indent().caseIndent);
}
int ride_project_toolchain(RIDEProject* project) {
    if (!project) return 0;
    return static_cast<int>(project->project.toolchain());
}
int ride_configuration(void) {
    return editor::settings::configuration() == "release" ? RIDE_CONFIG_RELEASE : 0;
}

void ride_remember_configuration(int config) {
    editor::settings::rememberConfiguration(config == RIDE_CONFIG_RELEASE ? "release" : "debug");
}
const char* ride_project_arch(RIDEProject* project) {
    if (!project) return "";
    project->answer = project->project.arch();
    return project->answer.c_str();
}

int ride_project_runs_as_project(RIDEProject* project, const char* source) {
    if (!project || !source) return 0;
    return static_cast<int>(project->project.runsAsProject(source));
}

int ride_project_set_arch(RIDEProject* project, const char* arch) {
    if (!project || !project->project.loaded() || !arch) return 0;
    project->project.setArch(arch);
    project->last = editor::saveProject(project->project);
    return project->last.ok ? 1 : 0;
}

int ride_project_set_toolchain(RIDEProject* project, int kind) {
    if (!project || !project->project.loaded()) return 0;
    project->project.setToolchain(static_cast<editor::ToolchainKind>(kind));
    project->last = editor::saveProject(project->project);
    return project->last.ok ? 1 : 0;
}

int ride_project_allows(const char* relative, char* why, int whySize) {
    std::string reason;
    bool fine = editor::Project::allows(relative ? relative : "", reason);
    if (why && whySize > 0) {
        copyOut(why, whySize, reason);
    }
    return fine ? 1 : 0;
}

int ride_project_loaded(RIDEProject* project) { return project && project->project.loaded() ? 1 : 0; }

const char* ride_project_root(RIDEProject* project) {
    if (!project) return "";
    project->answer = project->project.root();
    return project->answer.c_str();
}

void ride_project_set_root(RIDEProject* project, const char* path) {
    if (!project) return;
    project->project.setRoot(path ? path : ".");
}

void ride_project_close(RIDEProject* project) { if (project) project->project.close(); }

const char* ride_project_suffix(void) { return editor::Project::suffix(); }

const char* ride_product_name(void) { return editor::product::kName; }
const char* ride_version(void) { return editor::about::version(); }

int ride_project_save_as(RIDEProject* project, const char* file,
                            char* why, int whySize) {
    if (!project) return 0;
    std::string error;
    bool ok = project->project.saveAs(file ? file : "", error);
    if (!ok && why && whySize > 0) {
        std::string said = error.empty() ? std::string("could not save the project") : error;
        copyOut(why, whySize, said);
    }
    return ok ? 1 : 0;
}

const char* ride_group_for_file(const char* name) {
    scratch() = editor::groupForFile(name ? name : "");
    return scratch().c_str();
}

const char* ride_project_relative(RIDEProject* project, const char* path) {
    if (!project) return "";
    project->answer = project->project.relative(path ? path : "");
    return project->answer.c_str();
}


const char* ride_outcome_message(RIDEProject* project) {
    if (!project) return "";
    return project->last.message.c_str();
}

const char* ride_outcome_path(RIDEProject* project) { return project ? project->last.path.c_str() : ""; }

int ride_create_file(RIDEProject* project, const char* relative, const char* group, int kind) {
    if (!project) return 0;
    project->last = editor::createFile(project->project, relative ? relative : "",
                                       group ? group : "", static_cast<editor::ToolchainKind>(kind));
    return project->last.ok ? 1 : 0;
}

int ride_rename_file(RIDEProject* project, const char* fromAbsolute, const char* toRelative) {
    if (!project) return 0;
    project->last = editor::renameFile(project->project, fromAbsolute ? fromAbsolute : "",
                                       toRelative ? toRelative : "");
    return project->last.ok ? 1 : 0;
}

int ride_delete_file(RIDEProject* project, const char* absolute) {
    if (!project) return 0;
    project->last = editor::deleteFile(project->project, absolute ? absolute : "");
    return project->last.ok ? 1 : 0;
}

int ride_move_to_group(RIDEProject* project, const char* absolute, const char* group) {
    if (!project) return 0;
    project->last = editor::moveToGroup(project->project, absolute ? absolute : "",
                                        group ? group : "");
    return project->last.ok ? 1 : 0;
}

int ride_add_existing(RIDEProject* project, const char* absolute, const char* group) {
    if (!project) return 0;
    project->last = editor::addExisting(project->project, absolute ? absolute : "",
                                        group ? group : "");
    return project->last.ok ? 1 : 0;
}

int ride_adopt_saved(RIDEProject* project, const char* absolute) {
    if (!project) return 0;
    editor::Outcome joined = editor::adoptSaved(project->project, absolute ? absolute : "");
    if (joined.ok) project->last = joined;
    return joined.ok ? 1 : 0;
}

int ride_project_holds(RIDEProject* project, const char* absolute) {
    if (!project || !absolute || !*absolute || !project->project.loaded()) return 0;
    std::string relative = project->project.relative(absolute);
    return project->project.groupOf(relative) < project->project.groups().size() ? 1 : 0;
}

const char* ride_project_file_to_open(RIDEProject* project) {
    scratch() = project ? project->project.fileToOpen() : std::string();
    return scratch().c_str();
}

int ride_remember_open(RIDEProject* project, const char* absolute) {
    if (!project) return 0;
    return editor::rememberOpen(project->project, absolute ? absolute : "").ok ? 1 : 0;
}

int ride_remove_from_project(RIDEProject* project, const char* absolute) {
    if (!project) return 0;
    project->last = editor::removeExisting(project->project, absolute ? absolute : "");
    return project->last.ok ? 1 : 0;
}

int ride_begin_project(RIDEProject* project, const char* directory, const char* name,
                      const char* firstFile) {
    if (!project) return 0;
    project->last = editor::beginProject(project->project, directory ? directory : ".",
                                         name ? name : "Project",
                                         firstFile ? firstFile : "");
    return project->last.ok ? 1 : 0;
}


const char* ride_project_includes(RIDEProject* project) {
    scratch() = project ? joinedList(project->project.includes()) : std::string();
    return scratch().c_str();
}

const char* ride_project_libraries(RIDEProject* project) {
    scratch() = project ? joinedList(project->project.libraries()) : std::string();
    return scratch().c_str();
}

int ride_project_set_includes(RIDEProject* project, const char* line) {
    if (!project) return 0;
    project->project.setIncludes(splitList(line));
    project->last = editor::saveProject(project->project);
    return project->last.ok ? 1 : 0;
}

int ride_project_set_libraries(RIDEProject* project, const char* line) {
    if (!project) return 0;
    project->project.setLibraries(splitList(line));
    project->last = editor::saveProject(project->project);
    return project->last.ok ? 1 : 0;
}

const char* ride_includes(void) {
    scratch() = joinedList(editor::settings::includes());
    return scratch().c_str();
}

const char* ride_libraries(void) {
    scratch() = joinedList(editor::settings::libraries());
    return scratch().c_str();
}

namespace {
int (*askNativeInWindow)(const char*) = 0;
bool askNativeThroughWindow(void*, const std::string& question) {
    return askNativeInWindow && askNativeInWindow(question.c_str()) != 0;
}
}
void ride_ask_native(int (*ask)(const char* question)) {
    askNativeInWindow = ask;
    editor::setAskNative(ask ? askNativeThroughWindow : 0, 0);
}

int ride_set_includes(const char* line) { return editor::settings::rememberIncludes(splitList(line)) ? 1 : 0; }
int ride_set_libraries(const char* line) { return editor::settings::rememberLibraries(splitList(line)) ? 1 : 0; }

const char* ride_install_file(void) {
    scratch() = editor::settings::installFile();
    return scratch().c_str();
}

int ride_write_install_file_if_absent(void) {
    return editor::settings::writeInstallFileIfAbsent() ? 1 : 0;
}

const char* ride_include_dir(void) {
    scratch() = editor::settings::includeDir();
    return scratch().c_str();
}

const char* ride_lib_dir(void) {
    scratch() = editor::settings::libDir();
    return scratch().c_str();
}

int ride_remember_header_dirs(const char* include, const char* lib) {
    return editor::settings::rememberHeaderDirs(include ? include : "", lib ? lib : "") ? 1 : 0;
}

int ride_default_compiler(void) {
    return static_cast<int>(editor::toolchainFrom(editor::settings::defaultCompiler()));
}

int ride_default_indent_width(void) { return static_cast<int>(editor::settings::indentWidth()); }
int ride_default_indent_tabs(void) { return editor::settings::indentTabs() ? 1 : 0; }

int ride_remember_default_compiler(int kind) {
    return editor::settings::rememberDefaultCompiler(
               editor::toolchainWord(static_cast<editor::ToolchainKind>(kind))) ? 1 : 0;
}

const char* ride_vcvars(void) {
    scratch() = editor::settings::vcvars();
    return scratch().c_str();
}

int ride_remember_vcvars(const char* file) {
    return editor::settings::rememberVcvars(file ? file : "") ? 1 : 0;
}

const char* ride_assembler(void) {
    scratch() = editor::settings::assembler();
    return scratch().c_str();
}

int ride_remember_assembler(const char* file) {
    return editor::settings::rememberAssembler(file ? file : "") ? 1 : 0;
}

const char* ride_linker(void) {
    scratch() = editor::settings::linker();
    return scratch().c_str();
}

int ride_remember_linker(const char* file) {
    return editor::settings::rememberLinker(file ? file : "") ? 1 : 0;
}

const char* ride_tilinker(void) {
    scratch() = editor::settings::tilinker();
    return scratch().c_str();
}

int ride_remember_tilinker(const char* file) {
    return editor::settings::rememberTilinker(file ? file : "") ? 1 : 0;
}

const char* ride_ti(void) {
    scratch() = editor::settings::ti();
    return scratch().c_str();
}

const char* ride_tilib(void) {
    scratch() = editor::settings::tilib();
    return scratch().c_str();
}

int ride_remember_ti(const char* dir, const char* lib) {
    return editor::settings::rememberTi(dir ? dir : "", lib ? lib : "") ? 1 : 0;
}

int ride_save_project(RIDEProject* project) {
    if (!project) return 0;
    project->last = editor::saveProject(project->project);
    return project->last.ok ? 1 : 0;
}

int ride_parse_diagnostic(const char* text, const char* source, int* line, int* column,
                          const char** file, const char** message) {
    static std::string* keptFile = new std::string();
    static std::string* keptMessage = new std::string();
    editor::Diagnostic d = editor::parseDiagnostic(text ? text : "", source ? source : "");
    *keptFile = d.present ? d.file : std::string();
    *keptMessage = d.present ? d.message : std::string();
    if (line) *line = d.present ? static_cast<int>(d.line) : 0;
    if (column) *column = d.present ? static_cast<int>(d.col) : 0;
    if (file) *file = keptFile->c_str();
    if (message) *message = keptMessage->c_str();
    return d.present ? 1 : 0;
}

const char* ride_compiler_name(int which) {
    if (which == RIDE_COMPILER_CPP) return editor::product::kCompilerCpp;
    if (which == RIDE_COMPILER_SHALIMAR) return editor::product::kCompilerShalimar;
    return editor::product::kCompilerC;
}

const char* ride_arch(int index) {
    if (index < 0 || index >= static_cast<int>(editor::kArchCount)) index = 0;
    return editor::kArches[index];
}

int ride_arch_count(void) { return static_cast<int>(editor::kArchCount); }

const char* ride_toolchain_name(int kind) {
    return editor::toolchainName(static_cast<editor::ToolchainKind>(kind));
}

const char* ride_language_name(int language) {
    return editor::languageName(static_cast<editor::Language>(language));
}

const char* ride_config_name(int config) {
    return editor::configName(static_cast<editor::Configuration>(config));
}

int ride_resolve(int toolchainKind, int language) {
    editor::Toolchain tool;
    tool.kind = static_cast<editor::ToolchainKind>(toolchainKind);
    return static_cast<int>(editor::resolve(tool, static_cast<editor::Language>(language)));
}

int ride_can_compile(int kind, int language) {
    return editor::canCompile(static_cast<editor::ToolchainKind>(kind),
                              static_cast<editor::Language>(language))
               ? 1 : 0;
}

const char* ride_refusal(int kind, int language) {
    scratch() = editor::refusal(static_cast<editor::ToolchainKind>(kind),
                                static_cast<editor::Language>(language));
    return scratch().c_str();
}

int ride_uses_arch(int kind) {
    return editor::usesArch(static_cast<editor::ToolchainKind>(kind)) ? 1 : 0;
}

const char* ride_shown_command(RIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1, int kind,
                              const char* source, int language, const char* arch,
                              int config) {
    editor::Toolchain tool = toolFrom(project, cc1, cl, shc, cxx1);

    scratch() = editor::shownCommand(tool, static_cast<editor::ToolchainKind>(kind),
                                     source ? source : "",
                                     static_cast<editor::Language>(language),
                                     arch ? arch : "",
                                     static_cast<editor::Configuration>(config));
    return scratch().c_str();
}

int ride_runs_here(int kind, const char* arch) {
    return editor::runsHere(static_cast<editor::ToolchainKind>(kind), arch ? arch : "") ? 1 : 0;
}

const char* ride_why_not_run(int kind, const char* arch) {
    scratch() = editor::whyNotRun(static_cast<editor::ToolchainKind>(kind), arch ? arch : "");
    return scratch().c_str();
}

const char* ride_host_arch(void) { return editor::hostArch(); }

const char* ride_shown_run_command(RIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1, int kind,
                                  const char* source, int language, const char* arch,
                                  int config) {
    editor::Toolchain tool = toolFrom(project, cc1, cl, shc, cxx1);

    scratch() = editor::shownProgramCommand(tool, static_cast<editor::ToolchainKind>(kind),
                                            source ? source : "",
                                            static_cast<editor::Language>(language),
                                            arch ? arch : "",
                                            static_cast<editor::Configuration>(config));
    return scratch().c_str();
}

RIDERan* ride_run(RIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1, int kind, const char* source,
                int language, const char* arch, int config) {
    editor::Toolchain tool = toolFrom(project, cc1, cl, shc, cxx1);

    RIDERan* out = new RIDERan();
    editor::BuildScope scope;
    out->ran = editor::runProgram(tool, static_cast<editor::ToolchainKind>(kind),
                                  source ? source : "",
                                  static_cast<editor::Language>(language),
                                  arch ? arch : "",
                                  static_cast<editor::Configuration>(config));
    out->stopped = editor::buildCancelled();
    return out;
}

void ride_run_free(RIDERan* ran) { delete ran; }

int ride_ran_built(RIDERan* ran) { return ran && ran->ran.built ? 1 : 0; }
int ride_ran_ran(RIDERan* ran) { return ran && ran->ran.ran ? 1 : 0; }
int ride_ran_status(RIDERan* ran) { return ran ? ran->ran.status : 0; }
const char* ride_ran_output(RIDERan* ran) { return ran ? ran->ran.output.c_str() : ""; }
int ride_ran_has_error(RIDERan* ran) { return ran && ran->ran.diag.present ? 1 : 0; }
int ride_ran_error_line(RIDERan* ran) { return ran ? static_cast<int>(ran->ran.diag.line) : 0; }
int ride_ran_error_column(RIDERan* ran) { return ran ? static_cast<int>(ran->ran.diag.col) : 0; }
const char* ride_ran_error_message(RIDERan* ran) { return ran ? ran->ran.diag.message.c_str() : ""; }
const char* ride_ran_error_file(RIDERan* ran) { return ran ? ran->ran.diag.file.c_str() : ""; }

RIDEProgram* ride_build_program(RIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1, int kind, const char* source,
                              int language, const char* arch, int config) {
    editor::Toolchain tool = toolFrom(project, cc1, cl, shc, cxx1);

    RIDEProgram* out = new RIDEProgram();
    editor::BuildScope scope;
    out->built = editor::buildProgram(tool, static_cast<editor::ToolchainKind>(kind),
                                      source ? source : "",
                                      static_cast<editor::Language>(language),
                                      arch ? arch : "",
                                      static_cast<editor::Configuration>(config));
    return out;
}

void ride_program_free(RIDEProgram* built) {
    if (!built) return;
    editor::removeProgram(built->built);
    delete built;
}

int ride_program_ok(RIDEProgram* built) { return built && built->built.ok ? 1 : 0; }
const char* ride_program_path(RIDEProgram* built) { return built ? built->built.program.c_str() : ""; }
const char* ride_program_output(RIDEProgram* built) { return built ? built->built.output.c_str() : ""; }
int ride_program_has_error(RIDEProgram* built) { return built && built->built.diag.present ? 1 : 0; }
int ride_program_error_line(RIDEProgram* built) {
    if (!built) return 0;
    return static_cast<int>(built->built.diag.line);
}
int ride_program_error_column(RIDEProgram* built) {
    if (!built) return 0;
    return static_cast<int>(built->built.diag.col);
}
const char* ride_program_error_message(RIDEProgram* built) {
    if (!built) return "";
    return built->built.diag.message.c_str();
}
const char* ride_program_error_file(RIDEProgram* built) {
    return built ? built->built.diag.file.c_str() : "";
}

int ride_debugger_for(int kind, const char* arch) {
    return static_cast<int>(editor::dbg_for(static_cast<editor::ToolchainKind>(kind),
                                                arch ? arch : ""));
}

const char* ride_debugger_name(int kind) {
    return editor::dbg_name(static_cast<editor::DebuggerKind>(kind));
}

const char* ride_no_debugger_because(int kind, const char* arch) {
    scratch() = editor::dbg_whyNot(static_cast<editor::ToolchainKind>(kind),
                                          arch ? arch : "");
    return scratch().c_str();
}

int ride_debugger_stops_itself(int kind) {
    return editor::dbg_stopsItself(static_cast<editor::ToolchainKind>(kind)) ? 1 : 0;
}

const char* ride_release_cannot_stop(int kind) {
    return editor::dbg_stopsItself(static_cast<editor::ToolchainKind>(kind))
               ? shalimar::releaseHasNoSession()
               : "release is built without -g";
}

const char* ride_why_it_did_not_start(int kind, const char* arch) {
    if (editor::dbg_stopsItself(static_cast<editor::ToolchainKind>(kind))) {

        scratch() = shalimar::didNotArm();
        return scratch().c_str();
    }
    scratch() = std::string(editor::dbg_name(
                    editor::dbg_for(static_cast<editor::ToolchainKind>(kind),
                                    arch ? arch : ""))) +
                " could not be started - is it installed?";
    return scratch().c_str();
}

RIDEDebugger* ride_debugger_new(void) { return new RIDEDebugger(); }
void ride_debugger_free(RIDEDebugger* debugger) { delete debugger; }

int ride_debugger_start(RIDEDebugger* debugger, int kind, const char* arch,
                       const char* program) {
    if (!debugger) return 0;
    debugger->stop = editor::Stop();
    debugger->locals.clear();
    debugger->stack.clear();
    debugger->looking = 0;

    const editor::ToolchainKind tool = static_cast<editor::ToolchainKind>(kind);

    if (editor::dbg_stopsItself(tool))
        return debugger->shm.start(program ? program : "") ? 1 : 0;

    return debugger->debugger.start(editor::dbg_for(tool, arch ? arch : ""),
                                    program ? program : "") ? 1 : 0;
}

int ride_debugger_running(RIDEDebugger* debugger) {
    if (!debugger) return 0;
    return (debugger->debugger.running() || debugger->shm.running()) ? 1 : 0;
}

int ride_debugging_shalimar(RIDEDebugger* debugger) {
    if (!debugger) return 0;
    return debugger->shm.running() ? 1 : 0;
}

void ride_debugger_stop(RIDEDebugger* debugger) {
    if (!debugger) return;
    debugger->debugger.stop();
    debugger->shm.stop();
    debugger->stop = editor::Stop();
    debugger->locals.clear();
    debugger->stack.clear();
    debugger->looking = 0;
}

int ride_debugger_break(RIDEDebugger* debugger, const char* file, int line) {
    if (!debugger) return 0;
    if (line < 1) return 0;
    if (debugger->shm.running())
        return debugger->shm.breakAt(file ? file : "", static_cast<size_t>(line)) ? 1 : 0;
    return debugger->debugger.breakAt(file ? file : "", static_cast<size_t>(line)) ? 1 : 0;
}

int ride_debugger_clear(RIDEDebugger* debugger) {
    if (!debugger) return 0;
    if (debugger->shm.running()) return debugger->shm.clearBreakpoints() ? 1 : 0;
    return debugger->debugger.clearBreakpoints() ? 1 : 0;
}

namespace {

void afterMoving(RIDEDebugger* debugger, const editor::Stop& stop, bool itself) {
    debugger->stop = stop;
    debugger->locals.clear();
    debugger->stack.clear();
    debugger->looking = 0;
    if (!stop.stopped) return;

    if (itself) {
        debugger->stack = debugger->shm.frames();
        return;
    }

    debugger->locals = debugger->debugger.locals();
    debugger->stack = debugger->debugger.frames();
}
}

void ride_debugger_run(RIDEDebugger* debugger) {
    if (!debugger) return;
    const bool itself = debugger->shm.running();
    afterMoving(debugger, itself ? debugger->shm.run() : debugger->debugger.run(), itself);
}
void ride_debugger_resume(RIDEDebugger* debugger) {
    if (!debugger) return;
    const bool itself = debugger->shm.running();
    afterMoving(debugger, itself ? debugger->shm.resume() : debugger->debugger.resume(), itself);
}
void ride_debugger_step_over(RIDEDebugger* debugger) {
    if (!debugger) return;
    const bool itself = debugger->shm.running();
    afterMoving(debugger, itself ? debugger->shm.stepOver() : debugger->debugger.stepOver(), itself);
}
void ride_debugger_step_into(RIDEDebugger* debugger) {
    if (!debugger) return;
    const bool itself = debugger->shm.running();
    afterMoving(debugger, itself ? debugger->shm.stepInto() : debugger->debugger.stepInto(), itself);
}
void ride_debugger_step_out(RIDEDebugger* debugger) {
    if (!debugger) return;
    const bool itself = debugger->shm.running();
    afterMoving(debugger, itself ? debugger->shm.stepOut() : debugger->debugger.stepOut(), itself);
}

int ride_stop_stopped(RIDEDebugger* debugger) { return debugger && debugger->stop.stopped ? 1 : 0; }
int ride_stop_exited(RIDEDebugger* debugger) { return debugger && debugger->stop.exited ? 1 : 0; }
int ride_stop_status(RIDEDebugger* debugger) { return debugger ? debugger->stop.status : 0; }
const char* ride_stop_file(RIDEDebugger* debugger) { return debugger ? debugger->stop.file.c_str() : ""; }
int ride_stop_line(RIDEDebugger* debugger) { return debugger ? static_cast<int>(debugger->stop.line) : 0; }
const char* ride_stop_function(RIDEDebugger* debugger) { return debugger ? debugger->stop.function.c_str() : ""; }
const char* ride_stop_said(RIDEDebugger* debugger) { return debugger ? debugger->stop.said.c_str() : ""; }

int ride_stop_no_source(RIDEDebugger* debugger) {
    if (!debugger) return 0;
    return editor::dbg_stoppedWithNoSource(debugger->stop.said) ? 1 : 0;
}

const char* ride_stop_output(RIDEDebugger* debugger) {
    if (!debugger) return "";

    if (debugger->shm.ownsTheStop()) {
        debugger->output = debugger->stop.said;
        return debugger->output.c_str();
    }

    debugger->output = editor::dbg_programOutput(debugger->debugger.kind(), debugger->stop.said);
    return debugger->output.c_str();
}

int ride_locals_count(RIDEDebugger* debugger) {
    if (!debugger) return 0;
    return static_cast<int>(debugger->locals.size());
}

namespace {
bool holds(RIDEDebugger* debugger, int index) {
    return index >= 0 && static_cast<size_t>(index) < debugger->locals.size();
}
}

const char* ride_local_name(RIDEDebugger* debugger, int index) {
    if (!debugger) return "";
    return holds(debugger, index) ? debugger->locals[index].name.c_str() : "";
}
const char* ride_local_type(RIDEDebugger* debugger, int index) {
    if (!debugger) return "";
    return holds(debugger, index) ? debugger->locals[index].type.c_str() : "";
}
const char* ride_local_value(RIDEDebugger* debugger, int index) {
    if (!debugger) return "";
    return holds(debugger, index) ? debugger->locals[index].value.c_str() : "";
}

int ride_stack_count(RIDEDebugger* debugger) {
    if (!debugger) return 0;
    return static_cast<int>(debugger->stack.size());
}

namespace {
bool reaches(RIDEDebugger* debugger, int index) {
    return index >= 0 && static_cast<size_t>(index) < debugger->stack.size();
}
}

const char* ride_stack_function(RIDEDebugger* debugger, int index) {
    if (!debugger) return "";
    return reaches(debugger, index) ? debugger->stack[index].function.c_str() : "";
}
const char* ride_stack_file(RIDEDebugger* debugger, int index) {
    if (!debugger) return "";
    return reaches(debugger, index) ? debugger->stack[index].file.c_str() : "";
}
int ride_stack_line(RIDEDebugger* debugger, int index) {
    if (!debugger) return 0;
    return reaches(debugger, index) ? static_cast<int>(debugger->stack[index].line) : 0;
}

const char* ride_stack_text(RIDEDebugger* debugger, int index) {
    if (!debugger) return "";

    debugger->frameLine =
        reaches(debugger, index)
            ? editor::dbg_frameLine(debugger->stack[index],
                                    static_cast<size_t>(index) == debugger->looking)
            : std::string();
    return debugger->frameLine.c_str();
}

const char* ride_local_text(RIDEDebugger* debugger, int index) {
    if (!debugger) return "";
    debugger->variableLine = holds(debugger, index)
                                 ? editor::dbg_variableLine(debugger->locals[index])
                                 : std::string();
    return debugger->variableLine.c_str();
}

int ride_locals_on_line(RIDEDebugger* debugger, const char* line) {
    if (!debugger) return 0;
    size_t which = editor::dbg_variableOnLine(debugger->locals, line ? line : "");
    return which < debugger->locals.size() ? static_cast<int>(which) : -1;
}

int ride_set_variable(RIDEDebugger* debugger, const char* name, const char* value) {
    if (!debugger) return 0;
    debugger->complaint.clear();
    if (!debugger->debugger.setVariable(name ? name : "", value ? value : "",
                                        &debugger->complaint))
        return 0;

    debugger->locals = debugger->debugger.locals();
    return 1;
}

const char* ride_set_complaint(RIDEDebugger* debugger) { return debugger ? debugger->complaint.c_str() : ""; }

void ride_watch_add(RIDEDebugger* debugger, const char* expression) {
    if (!debugger) return;
    debugger->debugger.addWatch(expression ? expression : "");
}

int ride_watch_count(RIDEDebugger* debugger) {
    if (!debugger) return 0;
    return static_cast<int>(debugger->debugger.watches().size());
}

namespace {
bool watched(RIDEDebugger* debugger, int index) {
    return index >= 0 && static_cast<size_t>(index) < debugger->debugger.watches().size();
}
}

const char* ride_watch_text(RIDEDebugger* debugger, int index) {
    if (!debugger) return "";
    debugger->watchLine = watched(debugger, index)
                              ? editor::dbg_watchLine(debugger->debugger.watches()[index])
                              : std::string();
    return debugger->watchLine.c_str();
}

const char* ride_watch_expression(RIDEDebugger* debugger, int index) {
    if (!debugger) return "";
    return watched(debugger, index)
               ? debugger->debugger.watches()[index].expression.c_str()
               : "";
}

int ride_watch_on_line(RIDEDebugger* debugger, const char* line) {
    if (!debugger) return 0;
    size_t which = editor::dbg_watchOnLine(debugger->debugger.watches(), line ? line : "");
    return which < debugger->debugger.watches().size() ? static_cast<int>(which) : -1;
}

void ride_watch_set(RIDEDebugger* debugger, int index, const char* expression) {
    if (!debugger) return;
    if (!watched(debugger, index)) return;
    debugger->debugger.setWatch(static_cast<size_t>(index), expression ? expression : "");
}

int ride_debugger_look_at(RIDEDebugger* debugger, int which) {
    if (!debugger) return 0;
    if (!reaches(debugger, which)) return 0;

    if (debugger->shm.running()) {
        debugger->looking = 0;
        return which == 0 ? 1 : 0;
    }

    if (!debugger->debugger.selectFrame(static_cast<size_t>(which))) return 0;

    debugger->looking = static_cast<size_t>(which);
    debugger->locals = debugger->debugger.locals();
    return 1;
}

const char* ride_locals_none_because(RIDEDebugger* debugger) {
    if (!debugger) return "";
    debugger->refusal = debugger->shm.running()
                            ? "  (" + std::string(shalimar::saysWhereOnly()) + ")"
                            : std::string("  (nothing in scope here)");
    return debugger->refusal.c_str();
}

const char* ride_cannot_watch(RIDEDebugger* debugger) {
    if (!debugger) return "";

    debugger->refusal = debugger->shm.running()
                            ? std::string(shalimar::saysWhereOnly()) +
                                  " - nothing to watch with"
                            : std::string();
    return debugger->refusal.c_str();
}

const char* ride_cannot_walk_stack(RIDEDebugger* debugger) {
    if (!debugger) return "";

    debugger->refusal = debugger->shm.running() ? std::string(shalimar::saysHowDeepOnly())
                                                : std::string();
    return debugger->refusal.c_str();
}

const char* ride_stop_line_text(const char* file, int line, const char* function) {

    scratch() = editor::dbg_stopLine(file ? file : "",
                                     static_cast<size_t>(line < 0 ? 0 : line),
                                     function ? function : "");
    return scratch().c_str();
}

int ride_looking_at(RIDEDebugger* debugger) {
    if (!debugger) return 0;
    return static_cast<int>(debugger->looking);
}

const char* ride_looking_text(RIDEDebugger* debugger) {
    if (!debugger) return "";
    debugger->lookingLine =
        (debugger->looking > 0 && debugger->looking < debugger->stack.size())
            ? editor::dbg_lookingAt(debugger->stack[debugger->looking])
            : std::string();
    return debugger->lookingLine.c_str();
}

int ride_stack_on_line(RIDEDebugger* debugger, const char* line) {
    if (!debugger) return 0;
    size_t which = editor::dbg_frameOnLine(debugger->stack, line ? line : "");
    return which < debugger->stack.size() ? static_cast<int>(which) : -1;
}

int ride_begin_from_what_is_there(RIDEProject* project, const char* directory) {
    if (!project) return 0;
    project->last = editor::beginFromWhatIsThere(project->project, directory ? directory : "");
    project->answer = project->last.message;
    return project->last.ok ? 1 : 0;
}

const char* ride_last_project(void) {

    scratch() = editor::settings::lastProject();
    return scratch().c_str();
}

const char* ride_recent_project(int index) {
    std::vector<std::string> recent = editor::settings::recentProjects();
    scratch() = (index >= 0 && index < static_cast<int>(recent.size())) ? recent[static_cast<size_t>(index)]
                                                                         : std::string();
    return scratch().c_str();
}

const char* ride_recent_file(int index) {
    std::vector<std::string> recent = editor::settings::recentFiles();
    scratch() = (index >= 0 && index < static_cast<int>(recent.size())) ? recent[static_cast<size_t>(index)]
                                                                         : std::string();
    return scratch().c_str();
}

int ride_remember_file(const char* path) {
    return editor::settings::rememberFile(path ? path : "") ? 1 : 0;
}

int ride_remember_project(const char* directory) {
    return editor::settings::rememberProject(directory ? directory : "") ? 1 : 0;
}

const char* ride_demo_directory(void) {
    scratch() = editor::demoDirectory();
    return scratch().c_str();
}

int ride_project_builds(RIDEProject* project) {
    return project && project->project.builds() ? 1 : 0;
}

int ride_project_target_ready(RIDEProject* project) {
    if (!project) return 0;

    project->sources.clear();
    bool ok = project->project.targetParts(project->parts, project->why, &project->detail);
    if (ok)
        for (size_t i = 0; i < project->parts.size(); ++i)
            for (size_t f = 0; f < project->parts[i].sources.size(); ++f)
                project->sources.push_back(project->parts[i].sources[f]);

    project->language = ok && !project->parts.empty()
                            ? static_cast<int>(project->parts[0].lang)
                            : static_cast<int>(editor::LangPlain);
    project->program = ok ? project->project.targetProgram() : std::string();
    return ok ? 1 : 0;
}

int ride_project_target_parts(RIDEProject* project) {
    return project ? static_cast<int>(project->parts.size()) : 0;
}

const char* ride_project_part_group(RIDEProject* project, int index) {
    if (!project || index < 0 || index >= static_cast<int>(project->parts.size())) return "";
    return project->parts[static_cast<size_t>(index)].group.c_str();
}

int ride_project_part_language(RIDEProject* project, int index) {
    if (!project || index < 0 || index >= static_cast<int>(project->parts.size())) return 0;
    return static_cast<int>(project->parts[static_cast<size_t>(index)].lang);
}

int ride_project_part_toolchain(RIDEProject* project, int index, const char* cc1,
                               const char* cl, const char* shc, const char* cxx1, int kind) {
    if (!project || index < 0 || index >= static_cast<int>(project->parts.size()))
        return static_cast<int>(editor::ToolAuto);
    editor::Toolchain tool;
    tool.kind = static_cast<editor::ToolchainKind>(kind);
    if (cc1 && *cc1) tool.cc1 = cc1;
    if (cl && *cl) tool.cl = cl;
    if (shc && *shc) tool.shc = shc;
    if (cxx1 && *cxx1) tool.cxx1 = cxx1;
    return static_cast<int>(
        editor::toolchainOf(tool, project->parts[static_cast<size_t>(index)]));
}

const char* ride_project_target_why(RIDEProject* project) {
    return project ? project->why.c_str() : "";
}

const char* ride_project_target_detail(RIDEProject* project) {
    return project ? project->detail.c_str() : "";
}

int ride_project_target_language(RIDEProject* project) {
    return project ? project->language : 0;
}

int ride_project_target_sources(RIDEProject* project) {
    return project ? static_cast<int>(project->sources.size()) : 0;
}

const char* ride_project_target_source(RIDEProject* project, int index) {
    if (!project || index < 0 || index >= static_cast<int>(project->sources.size())) return "";
    return project->sources[static_cast<size_t>(index)].c_str();
}

const char* ride_project_target_program(RIDEProject* project) {
    return project ? project->program.c_str() : "";
}

int ride_project_debug_plan(RIDEProject* project, const char* cc1, const char* cl,
                           const char* shc, const char* cxx1, int kind, const char* arch) {
    if (!project) return 0;
    project->plan = editor::DebugPlan();
    project->whyNot.clear();
    if (!ride_project_target_ready(project)) {
        project->whyNot = project->why;
        return 0;
    }

    editor::Toolchain tool = toolFrom(project, cc1, cl, shc, cxx1);
    tool.kind = static_cast<editor::ToolchainKind>(kind);

    project->plan = editor::dbg_planFor(tool, project->parts, arch ? arch : "");
    if (project->plan.possible()) return 1;

    project->whyNot = editor::dbg_whyNot(project->plan.kind, arch ? arch : "");
    return 0;
}

int ride_project_debug_kind(RIDEProject* project) {
    return project ? static_cast<int>(project->plan.kind) : static_cast<int>(editor::ToolAuto);
}

const char* ride_project_why_not_debug(RIDEProject* project) {
    return project ? project->whyNot.c_str() : "";
}

int ride_project_blind_groups(RIDEProject* project) {
    return project ? static_cast<int>(project->plan.blind.size()) : 0;
}

const char* ride_project_blind_group(RIDEProject* project, int index) {
    if (!project || index < 0 || index >= static_cast<int>(project->plan.blind.size()))
        return "";
    return project->plan.blind[static_cast<size_t>(index)].c_str();
}

RIDEBuild* ride_build_target(RIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1,
                           int kind, const char* arch, int config) {
    if (!ride_project_target_ready(project)) return 0;

    editor::Toolchain tool = toolFrom(project, cc1, cl, shc, cxx1);

    tool.kind = static_cast<editor::ToolchainKind>(kind);

    RIDEBuild* out = new RIDEBuild();
    editor::BuildScope scope;
    editor::Built made = editor::buildParts(
        tool, project->parts, arch ? arch : "",
        static_cast<editor::Configuration>(config), project->program);
    out->stopped = editor::buildCancelled();

    out->built.ok = made.ok;
    out->built.diag = made.diag;
    out->built.output = made.output;
    return out;
}

RIDERan* ride_run_built(const char* program) {
    RIDERan* out = new RIDERan();
    editor::BuildScope scope;
    out->ran = editor::runBuilt(program ? program : "");
    out->stopped = editor::buildCancelled();
    return out;
}

struct RIDEConversion {
    editor::Conversion made;
    bool stopped = false;
};

int ride_converts_from(int language, int* toShalimar) {
    bool wanted = false;
    if (!editor::convertsFrom(static_cast<editor::Language>(language), &wanted)) {
        return 0;
    }
    if (toShalimar) *toShalimar = wanted ? 1 : 0;
    return 1;
}

char* ride_find_converter(void) {
    return give(editor::findConverter());
}

char* ride_converted_name(const char* source, int toShalimar) {
    return give(editor::convertedName(source ? source : "", toShalimar != 0));
}

RIDEConversion* ride_convert(const char* converter, const char* source,
                                   const char* output, int toShalimar) {
    RIDEConversion* out = new RIDEConversion();
    editor::BuildScope scope;
    out->made = editor::convert(converter ? converter : "", source ? source : "",
                                output ? output : "", toShalimar != 0);
    out->stopped = editor::buildCancelled();
    return out;
}

void ride_conversion_free(RIDEConversion* made) { delete made; }
int ride_conversion_ran(RIDEConversion* made) { return made && made->made.ran ? 1 : 0; }
int ride_conversion_ok(RIDEConversion* made) { return made && made->made.ok ? 1 : 0; }
const char* ride_conversion_produced(RIDEConversion* made) {
    if (!made) return "";
    return made->made.produced.c_str();
}
const char* ride_conversion_output(RIDEConversion* made) {
    if (!made) return "";
    return made->made.output.c_str();
}

RIDEBuild* ride_build(RIDEProject* project, const char* cc1, const char* cl, const char* shc, const char* cxx1, int kind, const char* source,
                    int language, const char* arch, int config) {
    editor::Toolchain tool = toolFrom(project, cc1, cl, shc, cxx1);

    RIDEBuild* out = new RIDEBuild();
    editor::BuildScope scope;
    out->built = editor::build(tool, static_cast<editor::ToolchainKind>(kind),
                               source ? source : "",
                               static_cast<editor::Language>(language),
                               arch ? arch : "",
                               static_cast<editor::Configuration>(config));
    out->assembly = join(out->built.asmLines);
    out->stopped = editor::buildCancelled();
    return out;
}

void ride_build_free(RIDEBuild* built) { delete built; }

int ride_build_ok(RIDEBuild* built) { return built && built->built.ok ? 1 : 0; }
const char* ride_build_output(RIDEBuild* built) { return built ? built->built.output.c_str() : ""; }
const char* ride_build_assembly(RIDEBuild* built) { return built ? built->assembly.c_str() : ""; }
int ride_build_assembly_lines(RIDEBuild* built) {
    if (!built) return 0;
    return static_cast<int>(built->built.asmLines.size());
}
int ride_build_has_error(RIDEBuild* built) { return built && built->built.diag.present ? 1 : 0; }
const char* ride_build_error_file(RIDEBuild* built) {
    return built ? built->built.diag.file.c_str() : "";
}

int ride_build_error_line(RIDEBuild* built) {
    if (!built) return 0;
    return static_cast<int>(built->built.diag.line);
}
int ride_build_error_column(RIDEBuild* built) {
    if (!built) return 0;
    return static_cast<int>(built->built.diag.col);
}
const char* ride_build_error_message(RIDEBuild* built) {
    if (!built) return "";
    return built->built.diag.message.c_str();
}

// ---- a program that runs while the window watches (bridge.h) --------------------------------

// Two locks: `state` for what the run has come to, which Stop takes and never waits in; `input`
// for the input's handle, which a send may hold while the program is slow to read.
struct RIDERunning {
    RIDEOutput onOutput;
    void* user;

    bool fromSource;
    editor::Toolchain tool;
    editor::ToolchainKind kind;
    std::string source;
    editor::Language language;
    std::string arch;
    editor::Configuration config;
    std::string program;

    editor::Process process;
    std::thread worker;
    std::mutex state;
    std::mutex input;
    std::condition_variable over;

    std::string waiting;         // sent before the program started
    bool closeWanted = false;
    bool stopWanted = false;
    bool started = false;
    bool finished = false;
    std::atomic<int> done{0};

    bool built = false;
    bool ran = false;
    int status = 0;
    bool stopped = false;
    editor::Diagnostic diag;
    std::string buildOutput;
};

namespace {

void runningLine(void* context, const std::string& line) {
    RIDERunning* running = static_cast<RIDERunning*>(context);
    std::string text = line + "\n";
    if (running->onOutput)
        running->onOutput(running->user, text.data(), static_cast<int>(text.size()), RIDE_STREAM_BUILD);
}

void runningChunk(void* context, const char* bytes, size_t size, bool isStderr) {
    RIDERunning* running = static_cast<RIDERunning*>(context);
    if (running->onOutput)
        running->onOutput(running->user, bytes, static_cast<int>(size),
                          isStderr ? RIDE_STREAM_ERR : RIDE_STREAM_OUT);
}

void runTheProgram(RIDERunning* running) {
    editor::BuildScope scope;
    bool stopFirst = false;
    {
        std::lock_guard<std::mutex> held(running->state);
        stopFirst = running->stopWanted;
    }
    editor::Built made;
    bool shalimar = false;
    std::string program = running->program;
    if (running->fromSource && !stopFirst) {
        made = editor::buildProgram(running->tool, running->kind, running->source, running->language,
                                    running->arch, running->config, runningLine, running);
        running->buildOutput = made.output;
        running->diag = made.diag;
        program = made.ok ? made.program : std::string();
        shalimar = made.shalimar;
    }

    {
        std::lock_guard<std::mutex> held(running->state);
        std::lock_guard<std::mutex> in(running->input);
        running->built = !program.empty() && !stopFirst;
        if (running->built && !running->stopWanted && !editor::buildCancelled()) {
            running->ran = editor::startProgram(running->process, program, shalimar);
            if (running->ran) {
                if (!running->waiting.empty())
                    running->process.send(running->waiting.data(), running->waiting.size());
                if (running->closeWanted) running->process.closeInput();
            }
        }
        running->waiting.clear();
        running->started = true;
    }

    // Read until the output closes, then wait - with no lock held, so Stop can still end a program
    // that closed its output and went on - and only then let the process go, under both locks.
    if (running->ran) {
        editor::pumpProgram(running->process, runningChunk, running);
        while (!running->process.ended(0)) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (running->fromSource) editor::removeProgram(made);

    {
        std::lock_guard<std::mutex> held(running->state);
        std::lock_guard<std::mutex> in(running->input);
        running->status = running->ran ? running->process.finish() : 0;
        running->stopped = running->stopWanted;
        running->finished = true;
        running->done = 1;
    }
    running->over.notify_all();
    if (running->onOutput) running->onOutput(running->user, NULL, 0, RIDE_STREAM_OUT);
}

RIDERunning* startRunning(RIDERunning* running) {
    try {
        running->worker = std::thread(runTheProgram, running);
    } catch (...) {
        delete running;
        return 0;
    }
    return running;
}

}

RIDERunning* ride_run_start(RIDEProject* project, const char* cc1, const char* cl, const char* shc,
                            const char* cxx1, int kind, const char* source, int language,
                            const char* arch, int config, RIDEOutput onOutput, void* user) {
    RIDERunning* running = new RIDERunning();
    running->onOutput = onOutput;
    running->user = user;
    running->fromSource = true;
    running->tool = toolFrom(project, cc1, cl, shc, cxx1);
    running->kind = static_cast<editor::ToolchainKind>(kind);
    running->source = source ? source : "";
    running->language = static_cast<editor::Language>(language);
    running->arch = arch ? arch : "";
    running->config = static_cast<editor::Configuration>(config);
    return startRunning(running);
}

RIDERunning* ride_run_built_start(const char* program, RIDEOutput onOutput, void* user) {
    RIDERunning* running = new RIDERunning();
    running->onOutput = onOutput;
    running->user = user;
    running->fromSource = false;
    running->kind = editor::ToolAuto;
    running->language = editor::LangPlain;
    running->config = editor::ConfigDebug;
    running->program = program ? program : "";
    return startRunning(running);
}

int ride_running_send(RIDERunning* running, const char* bytes, int size) {
    if (!running || !bytes || size < 0) return 0;
    std::lock_guard<std::mutex> in(running->input);
    if (running->finished || running->closeWanted) return 0;
    if (!running->started) {
        running->waiting.append(bytes, static_cast<size_t>(size));
        return 1;
    }
    if (!running->ran) return 0;
    return running->process.send(bytes, static_cast<size_t>(size)) ? 1 : 0;
}

void ride_running_close_input(RIDERunning* running) {
    if (!running) return;
    std::lock_guard<std::mutex> in(running->input);
    if (running->finished || running->closeWanted) return;
    running->closeWanted = true;
    if (running->started && running->ran) running->process.closeInput();
}

void ride_running_stop(RIDERunning* running) {
    if (!running) return;
    std::lock_guard<std::mutex> held(running->state);
    if (running->finished) return;
    running->stopWanted = true;
    if (!running->started) editor::cancelBuilds();
    else if (running->ran) running->process.kill();
}

int ride_running_wait(RIDERunning* running, int timeoutMs) {
    if (!running) return 1;
    std::unique_lock<std::mutex> held(running->state);
    if (timeoutMs < 0) {
        running->over.wait(held, [running] { return running->done.load() != 0; });
        return 1;
    }
    return running->over.wait_for(held, std::chrono::milliseconds(timeoutMs),
                                  [running] { return running->done.load() != 0; }) ? 1 : 0;
}

int ride_running_done(RIDERunning* running) { return running && running->done.load() ? 1 : 0; }

namespace {
bool isOver(RIDERunning* running) { return running && running->done.load() != 0; }
}

int ride_running_built(RIDERunning* running) { return isOver(running) && running->built ? 1 : 0; }
int ride_running_ran(RIDERunning* running) { return isOver(running) && running->ran ? 1 : 0; }
int ride_running_status(RIDERunning* running) { return isOver(running) ? running->status : 0; }
int ride_running_stopped(RIDERunning* running) { return isOver(running) && running->stopped ? 1 : 0; }
const char* ride_running_build_output(RIDERunning* running) {
    return isOver(running) ? running->buildOutput.c_str() : "";
}
int ride_running_has_error(RIDERunning* running) {
    return isOver(running) && running->diag.present ? 1 : 0;
}
const char* ride_running_error_file(RIDERunning* running) {
    return isOver(running) ? running->diag.file.c_str() : "";
}
int ride_running_error_line(RIDERunning* running) {
    return isOver(running) ? static_cast<int>(running->diag.line) : 0;
}
int ride_running_error_column(RIDERunning* running) {
    return isOver(running) ? static_cast<int>(running->diag.col) : 0;
}
const char* ride_running_error_message(RIDERunning* running) {
    return isOver(running) ? running->diag.message.c_str() : "";
}

void ride_running_free(RIDERunning* running) {
    if (!running) return;
    ride_running_stop(running);
    if (running->worker.joinable()) running->worker.join();
    delete running;
}

void ride_cancel_builds(void) { editor::cancelBuilds(); }
int ride_build_stopped(RIDEBuild* built) { return built && built->stopped ? 1 : 0; }
int ride_ran_stopped(RIDERan* ran) { return ran && ran->stopped ? 1 : 0; }
int ride_conversion_stopped(RIDEConversion* made) { return made && made->stopped ? 1 : 0; }

}
