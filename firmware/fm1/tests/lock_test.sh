#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Builds lock_test against the engine and sequencer in the FM-1's configuration and runs it.
#   firmware/fm1/tests/lock_test.sh OUTDIR
set -euo pipefail
trap 'echo "lock_test: failed at line $LINENO" >&2' ERR
out=$(realpath -m "$1")
cd "$(dirname "$0")/../../.."
mkdir -p "$out/obj"
CC=${CC:-clang}
mapfile -t src < <(python3 firmware/fm1/tests/fm1_sources.py sources)
mapfile -t flags < <(python3 firmware/fm1/tests/fm1_sources.py flags)
src+=(firmware/fm1/tests/lock_probe.c)
for s in "${src[@]}"; do
    "$CC" -O1 -g -ffp-contract=off -std=gnu11 -fcommon -Ihost/src -DPLAT_HOST "${flags[@]}" -c "$s" -o "$out/obj/$(echo "$s" | tr / _).o" &
    while [ "$(jobs -r | wc -l)" -ge 16 ]; do wait -n; done
done
wait
"$CC" -O1 -g -Wno-everything -Werror=implicit-function-declaration -Ifirmware/fm1/src firmware/fm1/tests/lock_test.c "$out"/obj/*.o -lm -o "$out/lock_test"
timeout 90 "$out/lock_test"
