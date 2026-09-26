#include "about.h"
#include "path.h"
#include "product.h"

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

const char* version() { return "4.5"; }

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
