# SPDX-License-Identifier: GPL-3.0-only
# How verify.sh keeps score: a step's exit code is its result, whatever it printed. verify_selftest.sh proves it.
fails=0
fail() { echo "FAIL $*"; fails=$((fails + 1)); }
run() {                                          # run NAME COMMAND...
    local name=$1 rc
    shift
    "$@"
    rc=$?
    [ "$rc" = 0 ] || fail "$name (exit $rc)"
}
quiet_grep() { grep -E "$1" || true; }           # a filter finds nothing when all is well: only what comes before it is the verdict
