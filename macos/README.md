# RIDE on macOS - the window

The third front end over RIDE's one core. The terminal editor is `../Makefile`'s,
the Windows Forms window is `../winforms/`, and this is an AppKit window written
in Objective-C++ (`.mm`: C++ and Objective-C in one file). Like the Windows
window it compiles `../src`'s core and `../winforms/bridge.cpp` - the C interface
both windows speak - and none of the terminal's drawing. `../src` is not touched.

```
+------------------------------------------------------------------+
| RIDE 4.5 - hello - hello.c                          (title bar)  |
| File Edit View Project Build Target Option Help     (menu bar)   |
+---------------+--------------------------------------------------+
| HELLO         | hello  >  src/hello.c                       c90  |
|  v Sources    |  1  #include <stdio.h>                           |
|     hello.c   |  2  int main(void)                               |
|  v Headers    |  3  {                                            |
| OPEN FILES    |  4      printf("hi\n");                          |
|   hello.c     |  5      return 0;                                |
|  (navigator)  |  6  }                   (editor, line numbers)  |
|               +--------------------------------------------------+
|               | [Errors (1)] [Progress] [Output]   (bottom 1/4)  |
|               | x  expected ';'            hello.c   4:20        |
+---------------+--------------------------------------------------+
| (spinner) 1 issue - hello.c:4:20 ...   C  debug  c90*  arm64  Ln 4|
+------------------------------------------------------------------+
```

## Building

```
cd macos
make            # ../../build/RIDE-4.5/RIDE.app
make run        # build and open it
```

or `open RIDEMac.xcodeproj` and Run. The Xcode project is generated from this
Makefile's source lists by `python3 make-xcodeproj.py`; run it again after
adding or removing a file.

Either way the compilers in `../bin` (c90.exe, cpp11.exe, shalimar.exe, c2s.exe,
masm, link, lnk6x, ...) and the Shalimar runtime in `../bin/lib` are copied into
`RIDE.app/Contents/MacOS`, beside the editor, which is where it looks first -
after the `C90`, `CPP11` and `SHALIMAR` environment variables. The manual goes to
`Contents/Resources/help`.

## The files

| file | what it is |
| --- | --- |
| `main.mm` | the application: arguments, the Dock, quitting |
| `RIDEWindowController.mm` | the window - navigator, editor, panel, status bar, the eight menus and every action |
| `RIDECodeView.mm` | the text: lays C, C++ and Shalimar out as it is typed, through the core's indent rules |
| `RIDELineNumbers.mm` | the gutter: line numbers, and red/orange marks where the last build complained |
| `RIDEStrings.h` | NSString to UTF-8 and back, in one place |

## What it does

* **Navigator** (left) - the project's groups and files, then the open files.
  Right-click for New, Add, Rename, Move to Group, Remove, Delete, Show in Finder.
* **Editor** (middle) - one text view, each open file's text swapped into it, so
  each file keeps its own undo and place. Layout as you type, Tab to re-lay a
  line, Ctrl-I to re-indent a selection or the file, colouring from the core's
  highlighter, applied as layout attributes so it never touches undo.
* **Bottom panel** - a quarter of the window, three tabs:
  * **Errors** - every diagnostic the build printed, read line by line with the
    core's own parser; click one to go there.
  * **Progress** - each step with its time, a bar, and how long the build took.
  * **Output** - the command, what the compiler said, the program's output and
    return value, and the assembly after a compile.
* **Status line** (bottom) - the last thing that happened; what the next build
  will use (language, debug/release, compiler, target); line and column.
* Builds run off the main thread; the window stays live and the Build and
  Target menus wait until it is done.

## Menus

| menu | holds |
| --- | --- |
| File | New, New File in Project, Open, Open Recent, Close, Save, Save As, Save All, Revert, Show in Finder |
| Edit | Undo/Redo, Cut/Copy/Paste, Find (the find bar), Go to Line, Re-indent, Shift Left/Right, Comment |
| View | Navigator, Bottom Panel, Errors/Progress/Output, Line Numbers, font size, next/previous file, Full Screen |
| Project | New/Open/Recent/Save As/Close project, New/Add/Remove/Rename/Move/Delete file, project include paths and libraries |
| Build | Compile File (Cmd-B), Run File (Cmd-R), Build Project (Shift-Cmd-B), Run Project (Shift-Cmd-R), Debug/Release, Convert C to/from Shalimar, next issue |
| Target | the four targets, Compiler (by language, c90, cpp11, shalimar, host c++), Language |
| Option | Font, header directories, shared include paths and libraries, the assembler, linkers and TI compiler, the tools in use |
| Help | the manual, Keys, the Shalimar reference, About |

## Not here yet

The debugger (breakpoints, stepping, locals, the stack) that the Windows window
has; `bridge.h` already carries everything it needs.
