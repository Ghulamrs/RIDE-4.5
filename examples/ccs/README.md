# Sample CCS 7.4 projects

Three Code Composer Studio 7.4 projects for the TMS320C6747, made by CCS
itself and shipped as CCS left them: `.project`, `.ccsproject`, `.cproject`,
the linker command file and the sources. RIDE opens a CCS project folder as
it is and writes nothing into it; they are here to try that on.

| folder     | what it is                                                        |
|------------|-------------------------------------------------------------------|
| `K6747c`   | C: `main.c`, `inc/config.h`, and `util.c` linked from `../src`     |
| `K6747cpp` | C++: `main.cpp` (a class and `printf`), built with exceptions     |
| `P7misc`   | C: `lib/util.c` in a subfolder with its own options, and `extra.c` excluded from the build |

## Opening one

In the window, turn the CCS switch on in `settings.json` and open the folder:

    "ccs": { "enabled": true }

`"root"` may name a CCS install (`C:/ti/ccsv7`); on a machine without CCS it
is left out. On the console, `--ccs` does the same for one run:

    ride K6747c --ccs --run
    ride K6747cpp --ccs --config release --run

RIDE takes the project's device, sources, include paths, defines and
configurations (Debug and Release) and builds with its own cpp11 or c90,
asm6x and lnk6x for tms6747, running the program on vm6747. Every option of
the project it does not honour is named in the console on the first line of
the build, with RIDE's default used instead.

## What each prints

| project    | Debug                               | Release                             |
|------------|-------------------------------------|-------------------------------------|
| `K6747c`   | `K6747c: level 0, twice(21) = 42`   | `K6747c: level 2, twice(21) = 42`   |
| `K6747cpp` | `K6747cpp: level 0, counter 42`     | `K6747cpp: level 3, counter 42`     |
| `P7misc`   | `K6747c: level 0, twice(21) = 42`   | `K6747c: level 0, twice(21) = 42`   |

LEVEL comes from each configuration's defines in `.cproject`. P7misc says
`K6747c` because it was made from that project's sources; its `extra.c`
would not link if it were built, so a correct run is itself the check that
the exclusion was read. Release builds also report the TI objects made: a
`.out` for the real board needs TI's linker, named under Tools.

The one change from the projects CCS wrote: K6747c's linked `util.c` was
recorded with an absolute path on the machine that made it, and is written
here as `PARENT-1-PROJECT_LOC/src/util.c`, the relative form CCS itself uses.
The unchanged originals are in `docs/ccs-reference`.
