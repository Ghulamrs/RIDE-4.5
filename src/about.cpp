#include "about.h"
#include "path.h"
#include "product.h"
#include "settings.h"
#include "toolchain.h"

#include <cstdio>
#include <string>

#if defined(_WIN32)
#define POPEN  _popen
#define PCLOSE _pclose
#else
#define POPEN  popen
#define PCLOSE pclose
#endif

namespace editor {
namespace about {

const char* name() { return product::kName; }

const char* version() { return "4.51"; }

namespace {

// **The compilers are asked rather than listed.** Their numbers written here would be a second
// copy, and the stale one: the editor does not build them. What the box answers is "what is it
// driving now", and absence is an ordinary state, so a missing compiler is a row that says so.
std::string askVersion(const std::string& program) {
    const std::string found = path::besideProgram(program);
    if (found.empty()) return std::string();

    // Quoted, because a path may hold a space. `--version` is the flag all four answer: cc1, shc
    // and c2s write one line and stop, cxx1 writes its banner first and its version second, so
    // the line kept is the last one naming a version - and the first when none does.
    const std::string command = "\"" + found + "\" --version 2>&1";
    FILE* pipe = POPEN(command.c_str(), "r");
    if (!pipe) return std::string();

    // The first line is the banner, which is what About shows: every one of the four opens with
    // one, and cxx1's version line beneath is detail. Its own copyright is taken off, since the box
    // ends with it once - so a row reads "cpp11 - ISO C++ 11" beside "c90 - ISO C 90".
    char buffer[256];
    std::string first;
    while (std::fgets(buffer, sizeof buffer, pipe)) {
        std::string line = buffer;
        while (!line.empty() && (line[line.size() - 1] == '\n' || line[line.size() - 1] == '\r'))
            line.resize(line.size() - 1);
        if (first.empty() && !line.empty()) first = line;
    }
    PCLOSE(pipe);

    const std::string owner = "\xC2\xA9""2026 G. R. Akhtar - ";
    if (first.compare(0, owner.size(), owner) == 0) first.erase(0, owner.size());
    return first;
}

// Named by the row when the answer does not name itself: cxx1's version line
// is "Version 1.2, sealed ..." with no cxx1 in it, and a row that could be
// any of the four is a row that says nothing.
std::string cell(const std::string& program) {
    const std::string answer = askVersion(program);
    std::string stem = program;
    if (stem.size() > 4 && stem.compare(stem.size() - 4, 4, ".exe") == 0) stem.resize(stem.size() - 4);
    if (answer.empty()) return stem + " - not beside this program";
    return stem + " - " + answer;
}

// **One per line, since 3.0.** Three answers shared a row while the box was seven lines and a
// fourth row would have left the panel; Editor::fitPanelTo grows the panel to what About puts in
// it now. cxx1's --version banner is longer than a column, and a row that wraps reads worse.
void tool(std::vector<std::string>& said, const std::string& program) {
    said.push_back("  " + cell(program));
}

}

namespace {

// One row: what, the file it resolved to, and where that answer came from.
void row(std::vector<std::string>& said, const std::string& what, const std::string& file, const std::string& from) {
    std::string left = "  " + what;
    if (left.size() < 22) left.resize(22, ' ');
    said.push_back(left + (file.empty() ? std::string("-") : file) + (from.empty() ? std::string() : "   (" + from + ")"));
}

// A tool beside RIDE, the way the build finds it: beside this program first, then PATH.
void besideOrPath(std::vector<std::string>& said, const std::string& what, const std::string& program) {
    std::string found = path::besideProgram(program);
    if (!found.empty()) { row(said, what, found, "beside RIDE"); return; }
    found = path::onPath(program);
    row(said, what, found, found.empty() ? "not found" : "on PATH");
}

// A tool settings.json may name: named, else what the build uses in its place.
void named(std::vector<std::string>& said, const std::string& what, const std::string& file,
           const std::string& otherwise) {
    if (!file.empty()) row(said, what, file, "settings.json");
    else row(said, what, otherwise, "settings.json names none");
}

}

std::vector<std::string> environment() {
    std::vector<std::string> said;
    said.push_back(std::string(name()) + " " + version() + " - the environment in force");
    const std::string install = settings::installFile();
    row(said, "settings.json", install, path::exists(install) ? "read" : "absent - every answer below is a default");
    said.push_back("");
    said.push_back("Compilers and tools");
    besideOrPath(said, "c90", "c90.exe");
    besideOrPath(said, "cpp11", "cpp11.exe");
    besideOrPath(said, "shalimar", "shalimar.exe");
    besideOrPath(said, "c2s", "c2s.exe");
    besideOrPath(said, "vm6747", "vm6747.exe");
    besideOrPath(said, "asm6x", "asm6x.exe");
    said.push_back("");
    said.push_back("x86_64-windows");
    named(said, "assembler", settings::assembler(), "cpp11's own choice: masm.exe beside it, else clang");
    named(said, "linker", settings::linker(), "Microsoft's link.exe");
#ifdef _WIN32
    const std::string vs = settings::vcvars();
    if (!vs.empty()) row(said, "Visual Studio", vs, "settings.json");
    else {
        const std::string found = visualStudioVcvars();
        row(said, "Visual Studio", found, found.empty() ? "none found - cl, ml64, link.exe and the Windows libraries need it"
                                                        : "found by vswhere");
    }
#else
    row(said, "Visual Studio", "", "not on this machine - Windows programs link on Windows");
#endif
    said.push_back("");
    said.push_back("tms6747");
    const std::string namedTi = settings::namedTi();
    if (!namedTi.empty()) row(said, "TI compiler", namedTi, "settings.json");
    else {
        const std::string found = settings::detectedTi();
        row(said, "TI compiler", found, found.empty() ? "none found - a .out needs CCS's ti-cgt-c6000; Tools names it"
                                                      : "detected - CCS's newest");
    }
    const std::string ti = settings::ti();
    named(said, "C6000 linker", settings::tilinker(), ti.empty() ? std::string() : path::join(path::join(ti, "bin"), "lnk6x"));
    const std::string tilib = settings::tilib();
    const bool eh = (!ti.empty() && path::exists(path::join(path::join(ti, "lib"), "rts6740_elf_eh.lib"))) ||
                    (!tilib.empty() && path::exists(path::join(tilib, "rts6740_elf_eh.lib")));
    row(said, "runtime", ti.empty() ? std::string() : path::join(ti, "lib"),
        eh ? "with rts6740_elf_eh.lib, which C++ programs need" : "without rts6740_elf_eh.lib - tilib names a directory holding it");
    if (!tilib.empty()) row(said, "tilib", tilib, "settings.json");
    said.push_back("");
    said.push_back("Headers and libraries");
    row(said, "cpp11 headers", settings::includeDir(), "settings.json \"include\"");
    row(said, "c90 headers", settings::libDir(), "settings.json \"lib\"");
    const std::vector<std::string> inc = settings::includes();
    for (size_t i = 0; i < inc.size(); ++i) row(said, "extra headers", inc[i], "settings.json \"includes\"");
    const std::vector<std::string> libs = settings::libraries();
    for (size_t i = 0; i < libs.size(); ++i) row(said, "extra library", libs[i], "settings.json \"libraries\"");
    if (inc.empty() && libs.empty()) row(said, "extra", "", "none - a project adds its own in its .pro");
    return said;
}

std::vector<std::string> lines() {
    std::vector<std::string> said;
    said.push_back(std::string(name()) + " " + version());
    // Asked one at a time; the heading is the user's wording, and the list says which compilers are actually here.
    said.push_back("Compiler's version list as follows:");
    // The VM6747 line since 3.5 - what the editor actually looks for, so
    // that a copy standing beside the sealed originals says so. Shalimar
    // first, the converter fourth: the user's order.
    tool(said, "shalimar.exe");
    tool(said, "c90.exe");
    tool(said, "cpp11.exe");
    tool(said, "c2s.exe");
    said.push_back("");
    // The sign docked to the year, the word left out: the user's wording.
    said.push_back("\xC2\xA9""2026 G. R. Akhtar");
    said.push_back("Islamabad, Pakistan");
    return said;
}

}
}
