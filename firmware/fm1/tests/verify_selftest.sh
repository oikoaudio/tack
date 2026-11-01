#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# verify.sh once passed after a crash, a hang and a failed build. Each way a step can fail must count as one, and a filter
# that finds nothing must not.
set -uo pipefail
source "$(dirname "$0")/verify_lib.sh"
bad=0
crash() { sh -c 'kill -SEGV $$'; }
want() {                                         # want COUNT NAME COMMAND...
    local count=$1 name=$2
    shift 2
    fails=0
    run "$name" "$@" > /dev/null 2>&1
    if [ "$fails" != "$count" ]; then
        echo "FAIL selftest: $name counted $fails failures, not $count"
        bad=1
    fi
}
exit_1() { return 1; }
piped_crash() { crash | tail -1; }
filtered_crash() { crash | quiet_grep nothing; }
hang() { timeout 1 sleep 10 | tail -1; }
piped_exit() { (exit 1) | tail -1; }
missing() { sha256sum /nonexistent/file | awk '{print $1}'; }
found_nothing() { echo hello | quiet_grep nothing; }
found_something() { echo hello | quiet_grep hello; }
want 1 "an exit code" exit_1
want 1 "a crash" crash
want 1 "a crash through a pipe" piped_crash
want 1 "a crash through a filter" filtered_crash
want 1 "a hang" hang
want 1 "a failure through a pipe" piped_exit
want 1 "a missing file" missing
want 0 "a filter that finds nothing" found_nothing
want 0 "a filter that finds something" found_something
[ "$bad" = 0 ] && echo "selftest passed"
exit "$bad"
