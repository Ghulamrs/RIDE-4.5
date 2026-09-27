#!/usr/bin/env bash
#
# The Windows box as two boxes at once: one leg for Windows itself and one for
# CCS 7.4's C6000 toolchain under C:\ti, each in a tree of its own on the box so
# the two builds never share a file, run in parallel, and reported together.
#
#   win  tools/to-windows.sh into $WIN_ROOT: the solution built, both suites run
#   ti   tools/to-windows.sh solution into $TI_ROOT, then TriLab's ccs leg against
#        the RIDEConsole.exe and vm6747.exe that tree built: RIDE's tms6747
#        programs on vm6747 held to cl6x's, and both sides through TI's lnk6x
#
#   ./tools/to-windows-both.sh            both legs
#   ./tools/to-windows-both.sh win        one of them
#   ./tools/to-windows-both.sh ti
#
# The roots default to C:\ride-verify\win and C:\ride-verify\ti - away from
# C:\Users\GRA\source, which hand-run work and to-windows.sh's own default use,
# and from C:\cxx1, which the compiler's verify-three uses. Logs go to $LOGS.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
WIN_ROOT="${WIN_ROOT:-C:\\ride-verify\\win}"
TI_ROOT="${TI_ROOT:-C:\\ride-verify\\ti}"
LOGS="${LOGS:-$(mktemp -d "${TMPDIR:-/tmp}/to-windows-both.XXXXXX")}"
mkdir -p "$LOGS" || exit 2
TRILAB="${TRILAB:-../VM6747/TriLab}"
WHICH="${1:-both}"

fwd() { echo "$1" | sed 's|\\|/|g'; }

leg_win() {
    ED1_WINDOWS_ROOT="$WIN_ROOT" ./tools/to-windows.sh > "$LOGS/win.log" 2>&1
}

leg_ti() {
    ED1_WINDOWS_ROOT="$TI_ROOT" ./tools/to-windows.sh solution > "$LOGS/ti-build.log" 2>&1 || return 1
    local t; t=$(fwd "$TI_ROOT")
    BOXLAB="$t/VM6747/TriLab" BOXRIDE="$t/RIDE-4.5/bin/RIDEConsole.exe" BOXVM="$t/RIDE-4.5/bin/vm6747.exe" \
        OUT="$LOGS/trilab" sh "$TRILAB/trilab.sh" ccs > "$LOGS/ti.log" 2>&1
}

pids=(); names=()
start() { "leg_$1" & pids+=($!); names+=("$1"); echo "started the $1 leg"; }
case "$WHICH" in
    both) start win; start ti ;;
    win|ti) start "$WHICH" ;;
    *) echo "usage: $0 [both|win|ti]" >&2; exit 2 ;;
esac

status=0
for i in "${!pids[@]}"; do
    if wait "${pids[$i]}"; then r=passed; else r=FAILED; status=1; fi
    echo "== ${names[$i]}: $r"
    case "${names[$i]}" in
        win) tail -15 "$LOGS/win.log" ;;
        ti)  [ -f "$LOGS/ti.log" ] && tail -20 "$LOGS/ti.log" || tail -15 "$LOGS/ti-build.log" ;;
    esac
done
echo "logs: $LOGS"
exit $status
