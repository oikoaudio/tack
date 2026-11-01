#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Desktop Tack must render byte for byte as before and the firmware must pass its checks; a failed build stops here.
#   firmware/fm1/tests/verify.sh OUTDIR
set -uo pipefail
out=$(realpath -m "$1")
cd "$(dirname "$0")/../../.."
root=$PWD
mkdir -p "$out/renders" "$out/sim"
cmake --build build/host > "$out/desktop.log" 2>&1 || { echo "desktop build failed, see $out/desktop.log"; exit 1; }
for v in 0 1 2 3 4 5 6; do build/host/lxr_render "$out/renders/voice$v.wav" $v > /dev/null; done
build/host/lxr_render "$out/renders/kit.wav" > /dev/null
build/host/lxr_render -r 48000 "$out/renders/kit48k.wav" > /dev/null
(cd "$out/renders" && sha256sum -c "$root/firmware/fm1/tests/desktop_renders.sha256" 2>&1 | awk '{print $NF}' | sort | uniq -c)
bash host/tools/smoke_test.sh build/host 2>&1 | tail -1
export JIELI_TOOLCHAIN=$HOME/.jieli/toolchain AC79_SDK=$HOME/.jieli/ac79-sdk JIELI_DOCKER=0
python3 third_party/fm1-base/tools/build.py firmware/fm1 2>&1 | grep -E '^  (ok|FAIL|fail)|error|^package|ERROR|failed'
CC=clang python3 third_party/fm1-base/tools/build.py firmware/fm1 --host 2>&1 | grep -E ' error|undefined|did not|^host'
python3 -c "import sys; sys.stdout.buffer.write(b'KITONE  ' + bytes([40]) * 248 + b'KITTWO  ' + bytes([50]) * 248)" > "$out/sim/kits.bin"
for t in keys sequencer song note kits recur rows voice; do
    (cd "$out/sim" && timeout 60 "$root/firmware/fm1/build/host/tack_host" "$root/firmware/fm1/tests/$t.txt" . 2>&1 | grep -v -E '^  ok' | tail -3)
done
dup=$(cd "$out/sim" && timeout 60 "$root/firmware/fm1/build/host/tack_host" "$root/firmware/fm1/tests/midi_once.txt" . 2>&1 | grep -E '^  out (80|90)' | uniq -d | wc -l)
[ "$dup" = 0 ] || echo "FAIL a note-on went out twice"
timeout 300 bash firmware/fm1/tests/lock_test.sh "$out/lock" 2>&1 | tail -1
