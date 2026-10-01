#ifndef EDITOR_COMPILE_H
#define EDITOR_COMPILE_H

#include <cstddef>
#include <string>
#include <vector>

#include "process.h"
#include "project.h"
#include "toolchain.h"

namespace editor {

struct Diagnostic {
    bool present = false;
    std::string file;
    size_t line = 0;
    size_t col = 0;
    std::string message;
};

struct Build {
    bool ok = false;
    Diagnostic diag;
    std::string output;
    std::vector<std::string> asmLines;
};

// The four targets, in the order the Target menu lists them. The fourth is
// the TI TMS320C6747, which the VM6747 emulator runs (toolchain.h).
const size_t kArchCount = 4;
extern const char* const kArches[kArchCount];

typedef void (*LineSink)(void* context, const std::string& line);

// **The question a build asks when the project's own tools failed it.** masm, link and lnk6x
// beside the editor are the tools by default; when one did not build something, the compilers
// found no fault, and settings.json says "askNative": true, the front end asks and a yes builds again with ml64 and link.exe, or TI's lnk6x. Decided by nativeFallbackWanted, so a test can hold it.
typedef bool (*AskNative)(void* context, const std::string& question);
void setAskNative(AskNative ask, void* context);
// question holds the question when the answer is yes - and, when it is no
// because the vendor's tools are not on this machine, the line that says so.
bool nativeFallbackWanted(bool ok, bool sourceFault, const std::string& arch,
                          const std::string& output, std::string& question);

int runCaptured(const std::string& command, std::string& output,
                LineSink sink = 0, void* context = 0);

// What runCaptured answers for a command it was stopped from running, or that was killed.
const int kStoppedStatus = 130;
// Kills every command runCaptured is running, on any thread; a build inside a BuildScope that
// began before this runs nothing more and fails with "[stopped]". Outside one, only the command.
void cancelBuilds();
struct BuildScope {
    BuildScope();
    ~BuildScope();
private:
    BuildScope(const BuildScope&);
    BuildScope& operator=(const BuildScope&);
};
// Whether this thread's BuildScope has been cancelled since it began.
bool buildCancelled();

Build build(const Toolchain& tool, ToolchainKind kind, const std::string& sourcePath,
            Language lang, const std::string& arch, Configuration config,
            LineSink sink = 0, void* context = 0);

struct Ran {
    bool built = false;
    bool ran = false;
    int status = 0;
    Diagnostic diag;
    std::string output;
};

struct Built {
    bool ok;
    Diagnostic diag;
    std::string output;
    std::string program;
    std::vector<std::string> leftovers;
    // A Shalimar program, which the emulator runs beside its runtime.
    bool shalimar;

    Built() : ok(false), shalimar(false) {}
};

Built buildProgram(const Toolchain& tool, ToolchainKind kind, const std::string& sourcePath,
                   Language lang, const std::string& arch, Configuration config,
                   LineSink sink = 0, void* context = 0);

Built buildTarget(const Toolchain& tool, ToolchainKind kind,
                  const std::vector<std::string>& sources, Language lang,
                  const std::string& arch, Configuration config,
                  const std::string& program, LineSink sink = 0, void* context = 0);

// **Which C6000 linker a tms6747 build links with, and what to say about it.** Chosen here so the
// suite can hold every case: nothing named, one named and there, one named and gone - where TI's
// would otherwise stand in unsaid. `path` empty means the build cannot go on, and `say` is why.
struct LinkerChoice {
    std::string path;
    std::string say;
};

LinkerChoice tiLinker(const std::string& chosen, const std::string& named,
                      const std::string& tiDir);

Built buildParts(const Toolchain& tool, const std::vector<Part>& parts,
                 const std::string& arch, Configuration config,
                 const std::string& program, LineSink sink = 0, void* context = 0);

Ran runBuilt(const std::string& program, LineSink sink = 0, void* context = 0,
             bool shalimar = false,
             const std::vector<std::string>& args = std::vector<std::string>());

void removeProgram(const Built& built);

Ran runProgram(const Toolchain& tool, ToolchainKind kind, const std::string& sourcePath,
               Language lang, const std::string& arch, Configuration config,
               LineSink sink = 0, void* context = 0);

// A built program run with a real input (README.md, "Input"): started on a Process another thread
// may send to, close or kill, and read here until its output closes; then Process::finish.
typedef void (*ChunkSink)(void* context, const char* bytes, size_t size, bool isStderr);
bool startProgram(Process& process, const std::string& program, bool shalimar = false,
                  const std::vector<std::string>& args = std::vector<std::string>());
void pumpProgram(Process& process, ChunkSink sink, void* context);

Diagnostic parseDiagnostic(const std::string& text, const std::string& source = std::string());

}

#endif
