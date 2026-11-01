#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Event traces of the LXR sequencer, three ways, then compare_seq.py.
#   firmware/fm1/tests/compare_seq.sh OUTDIR
# O the original configuration: desktop arithmetic, 64 patterns, 44140 Hz
# F the FM-1's: single precision, 16 patterns, the same rate (only the arithmetic and the bank differ)
# G the FM-1's at its real rate, 44100 Hz
set -euo pipefail
trap 'echo "compare_seq: failed at line $LINENO" >&2' ERR
out=$(realpath -m "$1")
cd "$(dirname "$0")/../../.."
mkdir -p "$out/O" "$out/F"
CC=${CC:-clang}
src=(Core/Src/Soundengine/Sequencer/sequencer.c Core/Src/Soundengine/Sequencer/clockSync.c
     Core/Src/Soundengine/Sequencer/EuklidGenerator.c firmware/fm1/src/seq_platform.c firmware/fm1/tests/seq_trace.c)
common=(-O2 -ffp-contract=off -std=gnu11 -Wno-everything -Werror=implicit-function-declaration -fcommon -ICore/Inc -Ihost/src
        -Ifirmware/fm1/src)
for s in "${src[@]}"; do
    "$CC" "${common[@]}" -DLXR_HOST_BUILD -Ifirmware/fm1/tests/original_inc -Ihost/shim -c "$s" -o "$out/O/$(basename "$s").o"
    "$CC" "${common[@]}" -include firmware/fm1/shim/fm1_prefix.h -DPLAT_HOST -DNUM_PATTERN=16 -Ifirmware/fm1/shim \
        -Ithird_party/fm1-base/include -Xclang -cl-single-precision-constant -c "$s" -o "$out/F/$(basename "$s").o"
done
"$CC" -O2 -Wno-everything firmware/fm1/tests/seq_trace_io.c "$out"/O/*.o -lm -o "$out/trace_O"
"$CC" -O2 -Wno-everything firmware/fm1/tests/seq_trace_io.c "$out"/F/*.o -lm -o "$out/trace_F"
timeout 60 "$out/trace_O" "$out/O.txt" 44140
timeout 60 "$out/trace_F" "$out/F.txt" 44140
timeout 60 "$out/trace_F" "$out/G.txt" 44100
python3 -I firmware/fm1/tests/compare_seq.py "$out"
