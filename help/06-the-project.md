# 6. The project

A project is one file — **`prime.pro`**, named after the program it builds —
and there does not have to be one. Ordinary JSON inside; the suffix says what
the file is *for* rather than what it is made of, the way `.vcxproj` and
`.xcodeproj` do — and the editor colours it as JSON, keys apart from values, so
opening one tells you what it says.

**`docs/sample.pro` is the template**: every key there is, filled in, to read
and copy. It is not a project and nothing opens it — a `.pro` is only looked
for in the directory you actually open, never one below, which is what keeps a
template a template. `examples/example.pro` is the opposite: a real, minimal
one that leaves four things to their defaults.

| key | left out means |
| --- | --- |
| `name` | the directory's own name |
| `arch` | this machine |
| `toolchain` | `auto` — the language chooses: C to c90, C++ to cpp11, Shalimar to shalimar |
| a group's `toolchain` | the project's, and then the language |
| `build` | no project program; Ctrl-B still builds the file in front of you |

**Debug or release is not in here.** Which of the two you are building is what
*you* are doing today, not a property of the program — and a project file
travels, so one arriving with a configuration in it would put everyone who
opened it into release. It lives in `~/.ride/state.json` with the rest of
what this machine had, `--config` overrides it for one run, and a `"config"`
key left in a project file is read by nothing.

**A directory may hold several.** `prime.pro` and `sums.pro` side by side is
the case the naming is for: opening the directory takes the first by name and
says so, and `Project ▸ Open` lists them to choose from. Whichever you
opened is the one reopened next time.

**`Project ▸ Save as...`** writes one out under a name of its own.
That is the only thing that converts a project — see below.

## A CCS project, opened as it is

A Code Composer Studio 7.4 or 5.5 project for the TMS320C6747 opens without
being converted. RIDE reads it, builds it with its own tools and writes nothing
into its folder, so the same folder still opens in CCS.

**1. Turn the CCS switch on.** In `settings.json` beside the programs:

```json
"ccs": { "enabled": true, "root": "C:/ti/ccsv7" }
```

`root` is the CCS install, used to resolve `${CG_TOOL_ROOT}` in the project's
paths. It may be left out on a machine with no CCS; the TI compiler directory a
tms6747 build already links against stands in. On the console, `--ccs` does the
same for one run, and `--ccs-root dir` names the install.

**2. Open the folder.** `Project ▸ Open...` and choose the folder that holds
`.project`, `.ccsproject` and `.cproject` - the folder, not a file in it. On the
console, give the folder: `ride Sample --ccs --run`. RIDE reads the three files
afresh every time the project is opened or built, so an edit made in CCS is
seen at once. What it remembers - which configuration you built last - goes
into `settings.json` under the project's path, never into the project.

**3. Pick Debug or Release.** CCS's two configurations are RIDE's two:
`Ctrl-D` or **Build ▸ Debug / Release**, `--config debug|release` on the
console. Each takes its own defines, optimisation level and link options from
`.cproject`.

**4. Build and run** - `F4` builds, **Build ▸ Run project** builds and runs,
as `--build` and `--run` do on the console. The target is tms6747 whatever the
editor was set to. RIDE compiles with cpp11 (C++) or c90 (C), assembles with
asm6x, and runs the program on vm6747, the C6747 emulator, in the console pane.

**What the first lines say.** Before anything is compiled, one line names every
option of the project that RIDE's tools do not take, and that RIDE's default
stands in for:

    CCS project Sample (Debug): no unsupported options; RIDE's own instead of:
    -mv6740, --abi=eabi, -o${ProjName}.out, -m${ProjName}.map, ...

*No unsupported options* means every option that changes the program was
honoured; the list after it is options RIDE's tools already behave as - the
device, the ABI, the output and map file names, the diagnostic format. What is
honoured: the optimisation level (`-O3` as `-O2`), `-g`, defines, undefines,
include paths, `--no_compress`, and for the link the project's `.cmd` file,
heap and stack sizes, `-i` paths, `-l` libraries (`libc.a`, TI's index, becomes
`rts6740_elf_eh.lib`) and the memory model. Anything else is named there as
*not supported* - `--opt_for_speed=5`, say - and takes RIDE's default.

    CCS project Sample: TI's option definitions are not here - lib/ccs beside the program, ...

means RIDE could not find TI's option definitions, which give an option the
project never stored its CCS default. They ship in `docs/ccs-reference` of the
install; this line means that folder is missing, and the build goes on with
RIDE's defaults - reinstall to put it back.

**A `.out` for the board.** The emulator runs the assembly; a file CCS can load
onto a C6747 needs a link against TI's runtime. Name TI's compiler directory
under **Tools ▸ TI compiler for tms6747...** (`ti-cgt-c6000_x.y.z`, the one with
`bin/lnk6x`) and a directory holding `rts6740_elf_eh.lib` - CCS ships only the
build without exceptions; `bin/ti/ti-build` makes the other - or pass
`--ti dir --tilib dir` on the console. Release builds then end with
`[linked <program>.out]`. The installed `settings.json` links with RIDE's own
lnk6x; with TI's, RIDE passes `--rom_model`, CCS's default, where the project
does not say.

**What does not open.** Only C6000 C674x devices: any other is refused with its
device named. The sources are every file in the folder by extension, the linked
files from `.project`, minus what `.cproject` excludes. The **Compiler Options**
dialog shows what was read and changes nothing - edit the project in CCS.

**The four samples** in `examples/ccs` of the install - a C one, two C++ ones,
one with a subfolder and an excluded file - say in their `README.md` what each
prints in Debug and in Release. `Sample` prints through `std::cout`: built by
CCS itself and run on TI's simulator it prints nothing, because TI's
`<iostream>` writes no console output there; its values are CCS's all the same,
and its `-D_STD_IO_` switch prints them through `printf`.

## One kind of project file

A project is a `.pro` file and nothing else. Older releases also read a
whole-directory project file under two earlier names; RIDE 4.0 reads neither.
Its configuration is `settings.json` beside the programs, and what it remembers
between sessions is `state.json` in `.ride` under your home directory.

With no project at all, the pane on the left shows the files you have open, and
nothing at all when none are.

`Project ▸ Close` is how you get there from a project. It closes the
view and not the project: the project file is left exactly as it was, nothing is
taken out of it, and every file you have open stays open.

**A file's name decides which group it is offered to.** A `.h` or `.hpp` goes
to Headers, a `.shl` to Shalimar, and the rest of what this editor compiles to
Sources - the same rule whether the project was written from a directory, the
file was added with **Add this file**, or it was made with **New file**. The
three used to agree only by accident, and a header added by hand landed among
the sources. Type a different group name over the one offered and that wins.

**There is no project file extension.** A project is a directory with an
a `.pro` in it, and that is the whole of what being one consists of — there
is nothing to look for called `.proj`. `Project ▸ Open...` lists the
directories under the one you are in and opens the one you pick; a directory
that holds a project file is opened, and one that does not is stepped into, so
you can walk down to where the project actually is.

```json
{
  "name": "mixed",
  "toolchain": "auto",
  "config": "debug",
  "arch": "arm64-darwin",
  "indent": 4,
  "tabs": false,

  "groups": {
    "Sources": ["src/main.c", "src/util.c"],
    "Legacy":  { "files": ["src/old.c"], "toolchain": "c++" },
    "Engine":  ["src/engine.cpp"]
  },

  "build": { "target": "mixed", "groups": ["Sources", "Legacy", "Engine"] }
}
```

Seven keys, flat except the groups, and every one has a default — so `{}` is a
valid project file. Comments with `//` are allowed, because a file people edit
by hand is a file people leave notes in.

## Groups

A group is the project's own arrangement and has nothing to do with
directories: moving a file between groups changes two lists and nothing on
disk. The Project menu makes a file and puts it in a group (New File), takes
a file already on disk into one (Add File — the file in front of you, once it
has a name), and takes it back out again, leaving it on disk (Remove File).
Renaming, moving between groups and deleting came off the menu on 2026-08-24
and are done outside the editor now; the editor follows a rename when the
project is next opened.

**A group is a list of files, or an object that also names a compiler.** The
plain list is not deprecated: a group with nothing to say is written back as a
list, so adding a file to a project written before any of this leaves the file
looking the way its author left it.

## `"build"` — what the project makes

```json
"build": { "target": "mixed", "groups": ["Sources", "Legacy", "Engine"] }
```

- **`target`** is the program's name, without `.exe`. It lands beside
  the project file, so it is still there when the editor is not.
- **`groups`** is which groups go into it — deliberately not all of them, so a
  project's own tests, examples, headers and notes stay out of its program.

It also **sets the order**: groups are compiled in the order this list names
them, and that is the order the objects reach the linker. For Shalimar it
additionally **picks the program**, since every `.shl` has a `main()` and
nothing inside a file can say it is the one being built.

Saying nothing is not an error. It means the project builds nothing, and
`Ctrl-B` still compiles the file in front of you.

## A compiler per group

**C and C++ have the same decision in them, and Shalimar has none.** C goes
to **c90** and C++ to **cpp11** — the compilers this editor was written for,
and the defaults — and each can go instead to the machine's own compiler,
`cl` on Windows and `clang++` or `g++` elsewhere, when a group says so.
Shalimar goes to `shalimar`, the only thing that reads it.

So a group naming a compiler is a group of C or C++ saying it wants the
host's. That is why `Legacy` above is the only group with a `"toolchain"` in
it, and why a group of C++ that is happy with cpp11 needs none. (Until 3.0
C++ had no cpp11 to go to and went to the host's on its own; a project written
then still builds, with its C++ now going to cpp11 - name `"c++"` on the group
to have it go where it went.)

The words are `c90`, `cpp11`, `cl` (or `msvc`), `shalimar`, `c++`, and `auto`.
`"c++"` means *this machine's* C++ compiler rather than g++ specifically —
which one that is is a fact about a machine, and a project file does not get
to have an opinion about it. For the same reason the *paths* to the compilers
are not in here either; they come from `--c90`, `--cpp11`, `--cl`, `--cxx`,
`$C90`, `$CPP11`, `$CXX`, or PATH.

**A group under `auto` holding two languages is split**, one part per language,
rather than refused. A group that names a compiler is one part and that
compiler takes all of it — which is the only way to make `cl` compile C as C++
on purpose.

## Two limits worth knowing

**Shalimar cannot share a target with C or C++.** In one group, because no
compiler takes both. In a group of its own beside them, because of what a
Shalimar object is — see [the Shalimar page](shalimar.md).

**One flat list cannot say "these files on Linux, those on Windows".** This
project's own sources are the example: `terminal.cpp` and `terminal_win.cpp`
are never built together. A project that differs by platform wants a group per
platform and a `build` entry naming the one you are on.

## Where a file may sit

The root, or one directory under it, and no deeper. As many directories as you
like may sit side by side — `src`, `tests`, `examples`, `docs` — but none of
them holds another. It is a rule the project keeps rather than a habit anyone
is asked to remember, because a structure nobody has to explore is one anyone
can read at a glance.
