#!/usr/bin/env python3
"""Washout: a copy of the RIDE tree that holds source and nothing else.

    python3 washout.py DEST              copy RIDE's sources into DEST
    python3 washout.py DEST --workspace  and each project RIDE drives, beside it
    python3 washout.py DEST --force      replace DEST if it is already there

Kept: the source directories (src, the macOS and Windows front ends), the
examples, projects, programs and help that the builds copy into the app, the
build files (Makefile, workspace.mk, product.props, RIDE.pro), the Visual
Studio 2022 solution and projects, the Xcode projects and workspace, .json
files, and the two icon files. Dropped: tests, docs, tools, packaging, dist,
every build directory, and any file that is binary - by its extension (.exe,
.com, .obj, .o, .lib, .a, .dll, ...) or by its contents (a NUL byte, or bytes
that are not UTF-8). The icons are the one binary kept on purpose.

With --workspace, each program's project is laid out beside the copy exactly
as workspace.mk expects (../VM6747/Compiler-Ci and so on), each washed to what
its tools/seal.json names as its source: the whole workspace builds from it.
"""
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))

# What RIDE is made of, beside its build files.
RIDE_DIRS = ["src", "macos", "winforms", "examples", "projects", "programs", "help",
             "Editor.xcodeproj", "RIDE.xcworkspace"]
RIDE_FILES = ["Makefile", "workspace.mk", "product.props", "RIDE.pro", "RIDE.sln",
              "RIDEConsole.vcxproj", "build.bat", "README.md"]

# The projects workspace.mk builds, at the paths it builds them from.
WORKSPACE = ["../VM6747/Compiler-Ci", "../VM6747/Compiler-Cppi", "../VM6747/Compiler-Si",
             "../VM6747/Emulator", "../Converter-C2S", "../ASM6x", "../MASM", "../LINK",
             "../LNK6x"]

BINARY_EXT = {".exe", ".com", ".obj", ".o", ".lib", ".a", ".dll", ".dylib", ".so", ".pdb",
              ".ilk", ".exp", ".idb", ".pch", ".ipch", ".out", ".hex", ".bin", ".elf",
              ".zip", ".gz", ".tgz", ".pkg", ".run", ".dmg", ".msi", ".cab", ".class",
              ".jar", ".pyc", ".png", ".jpg", ".jpeg", ".gif", ".bmp", ".pdf", ".res",
              ".tlog", ".lastbuildstate", ".suo", ".db", ".sdf", ".xcuserstate"}
KEEP_BINARY_EXT = {".ico", ".icns"}
SKIP_DIRS = {"build", "bin", "obj", "x64", "x86", "Debug", "Release", "DerivedData",
             "xcuserdata", ".vs", ".git", "dist", "tests", "test", "__pycache__"}


def is_binary(path):
    """Binary by extension, or by contents: a NUL byte, or bytes that are not UTF-8."""
    ext = os.path.splitext(path)[1].lower()
    if ext in KEEP_BINARY_EXT:
        return False
    if ext in BINARY_EXT:
        return True
    with open(path, "rb") as fh:
        data = fh.read()
    if b"\0" in data:
        return True
    try:
        data.decode("utf-8")
    except UnicodeDecodeError:
        return True
    return False


def tracked(project):
    """Files git tracks in the project, or None when it is not a checkout."""
    try:
        out = subprocess.run(["git", "ls-files", "-z"], cwd=project, stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, check=True).stdout
    except (OSError, subprocess.CalledProcessError):
        return None
    return {n for n in out.decode().split("\0") if n}


def candidates(project, dirs, files):
    """Every file under dirs, and the named files, relative to project."""
    out = []
    for d in dirs:
        top = os.path.join(project, d)
        for where, subdirs, names in os.walk(top):
            subdirs[:] = sorted(s for s in subdirs if s not in SKIP_DIRS
                                and not s.endswith(".dSYM") and not s.startswith("."))
            for n in sorted(names):
                if not n.startswith("."):
                    out.append(os.path.relpath(os.path.join(where, n), project))
    for f in files:
        if os.path.isfile(os.path.join(project, f)):
            out.append(f)
    return out


def wash(project, dest, dirs, files):
    """Copy project's source files into dest; return (kept, dropped)."""
    known = tracked(project)
    kept, dropped = 0, []
    for rel in candidates(project, dirs, files):
        rel_posix = rel.replace(os.sep, "/")
        if known is not None and rel_posix not in known:
            continue                     # an untracked file is a build product or a stray
        src = os.path.join(project, rel)
        if is_binary(src):
            dropped.append(rel_posix)
            continue
        target = os.path.join(dest, rel)
        os.makedirs(os.path.dirname(target), exist_ok=True)
        shutil.copy2(src, target)
        kept += 1
    return kept, dropped


def seal_conf(project):
    """What a project's tools/seal.json names as its source, or cxx1's own seal's list."""
    import json
    conf = os.path.join(project, "tools", "seal.json")
    if os.path.exists(conf):
        c = json.load(open(conf))
        return c["dirs"], c.get("files", [])
    # cxx1 (Compiler-Cppi) carries its own seal tool: src include lib examples and three build files.
    return (["src", "include", "lib", "examples", "msvc", "ide", "cxx1.xcodeproj", "cxx1.xcworkspace"],
            ["Makefile", "cxx1.sln", "cxx1.vcxproj", "README.md"])


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    if len(args) != 1:
        print(__doc__)
        return 2
    dest = os.path.abspath(args[0])
    if os.path.abspath(dest).startswith(ROOT + os.sep) or dest == ROOT:
        print("washout: the copy cannot go inside the tree it washes"); return 2
    workspace = "--workspace" in argv
    ride_dest = os.path.join(dest, "RIDE-4.5") if workspace else dest
    if os.path.exists(dest) and os.listdir(dest):
        if "--force" not in argv:
            print("washout: %s is not empty - give --force to replace it" % dest); return 2
        shutil.rmtree(dest)

    total_kept, total_dropped = 0, []
    kept, dropped = wash(ROOT, ride_dest, RIDE_DIRS, RIDE_FILES)
    print("  %-26s %4d files kept, %d binaries dropped" % ("RIDE-4.5", kept, len(dropped)))
    total_kept += kept; total_dropped += ["RIDE-4.5/" + d for d in dropped]
    if workspace:
        for rel in WORKSPACE:
            project = os.path.normpath(os.path.join(ROOT, rel))
            if not os.path.isdir(project):
                print("  %-26s MISSING - not beside RIDE here" % rel); continue
            name = os.path.relpath(project, os.path.dirname(ROOT))
            dirs, files = seal_conf(project)
            kept, dropped = wash(project, os.path.join(dest, name), dirs, files)
            print("  %-26s %4d files kept, %d binaries dropped" % (name, kept, len(dropped)))
            total_kept += kept; total_dropped += [name + "/" + d for d in dropped]
    for d in total_dropped:
        print("    dropped %s" % d)
    print("washout: %d source files in %s" % (total_kept, dest))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
