#!/bin/sh
# The Linux installer: one self-extracting RIDE-<ver>-linux-x86_64.run for
# Ubuntu, Debian, RHEL and Amazon Linux alike. Run on a Linux x86-64 machine
# with the workspace built into bin/ (tools/to-linux.sh does that on the box):
#
#   sh packaging/linux/build-run.sh [version]      -> dist/RIDE-<ver>-linux-x86_64.run
#
# Build it on the oldest glibc it is meant for: the programs need the glibc and
# libstdc++ they were linked against, and Amazon Linux 2023's 2.34 and GCC 11
# are the floor of every distribution named above. The layout is the Windows
# install's - bin include lib help examples, settings.json - and the header,
# install-header.sh, checks the machine, installs and links the commands.
set -eu

VER=${1:-4.5}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
CPP=${CPP:-$ROOT/../VM6747/Compiler-Cppi}
CC=${CC:-$ROOT/../VM6747/Compiler-Ci}
OUT=${OUT:-$ROOT/dist}
BIN=$ROOT/bin
STAGE=$(mktemp -d "${TMPDIR:-/tmp}/ride-run.XXXXXX")
trap 'rm -rf "$STAGE" "$STAGE.tgz"' EXIT

[ "$(uname -s)" = Linux ] || { echo "build-run.sh: build this on Linux"; exit 1; }
for t in RIDE c90 cpp11 shalimar c2s vm6747 asm6x masm link lnk6x; do
    [ -x "$BIN/$t.exe" ] || { echo "build-run.sh: no $BIN/$t.exe - build the workspace first"; exit 1; }
done

mkdir -p "$STAGE/bin" "$STAGE/examples"
for t in RIDE c90 cpp11 shalimar c2s vm6747 asm6x masm link lnk6x; do
    cp -p "$BIN/$t.exe" "$STAGE/bin/"
    strip "$STAGE/bin/$t.exe" 2>/dev/null || true
done
cp -rp "$BIN/lib" "$STAGE/bin/lib"
cp -rp "$CPP/include" "$STAGE/include"
cp -p "$CPP"/lib/*.h "$STAGE/include/"
cp -rp "$CC/lib" "$STAGE/lib"
cp -rp "$ROOT/help" "$STAGE/help"
for e in c h cpp shl pro; do cp -p "$ROOT"/examples/*."$e" "$STAGE/examples/" 2>/dev/null || true; done
cat > "$STAGE/settings.json" <<'EOF'
{
  "include": "include",
  "lib": "lib",
  "compiler": "auto",
  "askNative": true,
  "indent": 4,
  "tabs": false,
  "font": "",
  "includes": [],
  "libraries": []
}
EOF

# The staged compilers, before anything is packed: each must find its own
# headers beside it, not the build tree's compiled into it.
probe=$STAGE.probe; mkdir -p "$probe"
printf '#include <no_such_header.h>\n' > "$probe/bad.cpp"
if "$STAGE/bin/cpp11.exe" "$probe/bad.cpp" -o /dev/null 2>&1 | grep -q "$CPP"; then
    echo "build-run.sh: the staged cpp11 looked in the build tree"; rm -rf "$probe"; exit 1
fi
printf '#include <string>\n#include <cstdio>\nint main() { std::string s("cpp"); std::printf("%%s %%d\\n", s.c_str(), 6 * 7); return 0; }\n' > "$probe/p.cpp"
printf '#include <stdio.h>\nint main(void) { printf("c %%d\\n", 6 * 7); return 0; }\n' > "$probe/p.c"
printf 'fun <> = main() {\n  ? 42\n}\n' > "$probe/p.shl"
for c in cpp11:p.cpp:cpp42 c90:p.c:c42 shalimar:p.shl:42; do
    t=${c%%:*}; rest=${c#*:}; f=${rest%%:*}; want=${rest#*:}
    "$STAGE/bin/$t.exe" "$probe/$f" -o "$probe/a.out" >/dev/null
    got=$("$probe/a.out" | tr -d ' ')
    [ "$got" = "$want" ] || { echo "build-run.sh: staged $t printed '$got', wanted '$want'"; rm -rf "$probe"; exit 1; }
    echo "  ok  $t $f"
done
rm -rf "$probe"

mkdir -p "$OUT"
RUN=$OUT/RIDE-$VER-linux-x86_64.run
tar -C "$STAGE" -czf "$STAGE.tgz" .
sed "s/@VER@/$VER/g" "$HERE/install-header.sh" > "$RUN"
cat "$STAGE.tgz" >> "$RUN"
chmod 755 "$RUN"
echo "build-run.sh: $RUN ($(du -h "$RUN" | cut -f1))"
