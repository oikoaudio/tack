#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Desktop Tack must render byte for byte as before and the firmware must pass its checks; a failed build stops here.
#   firmware/fm1/tests/verify.sh OUTDIR
set -uo pipefail
out=$(realpath -m "$1")
cd "$(dirname "$0")/../../.."
root=$PWD
mkdir -p "$out/renders" "$out/sim"
# the last line is the verdict, and so is the exit code: a test that stops early says so in its own words
exec > >(tee "$out/verify.log") 2>&1
cmake --build build/host > "$out/desktop.log" 2>&1 || { echo "desktop build failed, see $out/desktop.log"; exit 1; }
for v in 0 1 2 3 4 5 6; do build/host/lxr_render "$out/renders/voice$v.wav" $v > /dev/null; done
build/host/lxr_render "$out/renders/kit.wav" > /dev/null
build/host/lxr_render -r 48000 "$out/renders/kit48k.wav" > /dev/null
(cd "$out/renders" && sha256sum -c "$root/firmware/fm1/tests/desktop_renders.sha256" 2>&1 | awk '{print $NF}' | sort | uniq -c)
bash host/tools/smoke_test.sh build/host 2>&1 | tail -1
timeout 120 build/host/mutate_test | tail -1 | sed 's/^FAILED$/FAIL mutate/'
timeout 60 build/host/lfo_test | tail -1 | sed 's/^FAILED$/FAIL lfo/'
export JIELI_TOOLCHAIN=$HOME/.jieli/toolchain AC79_SDK=$HOME/.jieli/ac79-sdk JIELI_DOCKER=0
python3 third_party/fm1-base/tools/build.py firmware/fm1 2>&1 | grep -E '^  (ok|FAIL|fail)|error|^package|ERROR|failed'
CC=clang python3 third_party/fm1-base/tools/build.py firmware/fm1 --host 2>&1 | grep -E ' error|undefined|did not|^host'
python3 -c "import sys; sys.stdout.buffer.write(b'KITONE  ' + bytes([40]) * 248 + b'KITTWO  ' + bytes([50]) * 248)" > "$out/sim/kits.bin"
for t in keys sequencer song note kits recur rows voice clock fxdc pages shapes lanes crush morph patkit mutate bars lfosync reverb; do
    (cd "$out/sim" && timeout 60 "$root/firmware/fm1/build/host/tack_host" "$root/firmware/fm1/tests/$t.txt" . 2>&1 | grep -v -E '^  ok' | tail -3)
done
dup=$(cd "$out/sim" && timeout 60 "$root/firmware/fm1/build/host/tack_host" "$root/firmware/fm1/tests/midi_once.txt" . 2>&1 | grep -E '^  out (80|90)' | uniq -d | wc -l)
[ "$dup" = 0 ] || echo "FAIL a note-on went out twice"
python3 -I - "$out/sim/verb.wav" <<'PY' || echo "FAIL the reverb has no tail"
import array, sys, wave
w = wave.open(sys.argv[1])
n = w.getnframes()
a = array.array("h", w.readframes(n))
sys.exit(max(abs(x) for x in a[n // 2:]) < 200)
PY
python3 -I - "$out/sim/fxdc.wav" "$out/sim/fxdc2.wav" <<'PY' || echo "FAIL the distortion left a steady level"
import array, sys, wave
for f in sys.argv[1:]:
    w = wave.open(f)
    n = w.getnframes()
    a = array.array("h", w.readframes(n))
    s = a[n:]
    if not s or max(abs(x) for x in s) > 2:
        sys.exit(1)
PY
python3 -I - "$out/sim/crush_off.wav" "$out/sim/crush_on.wav" "$out/sim/bits_on.wav" "$out/sim/bits_quiet.wav" <<'PY' || echo "FAIL RATE or BITS did not reach the audio"
import array, sys, wave
def repeats(f):
    w = wave.open(f)
    a = array.array("h", w.readframes(w.getnframes()))[::2]
    return sum(x == y for x, y in zip(a, a[1:])) / len(a)
off, on = repeats(sys.argv[1]), repeats(sys.argv[2])
def samples(f):
    w = wave.open(f)
    return array.array("h", w.readframes(w.getnframes()))
levels, full = len(set(samples(sys.argv[3]))), len(set(samples(sys.argv[1])))
quiet = samples(sys.argv[4])
print(f"rate: {off:.3f} of samples repeat at 127, {on:.3f} at 10; bits: {full} levels at 16, {levels} at about 4; quiet peak {max(map(abs, quiet))}")
sys.exit(0 if on > off + 0.3 and levels < 40 and full > 1000 and max(map(abs, quiet)) == 0 else 1)
PY
PYTHONPATH=firmware/fm1/tools python3 - <<'PY' || echo "FAIL fm1 picks the wrong port"
from fm1_port import pick, TACK, LOADER
assert pick(["Midi Through:Midi Through Port-0 14:0", "TACK FM-1:TACK FM-1 MIDI 1 20:0"]) == TACK
assert pick(["Midi Through:Midi Through Port-0 14:0", "Felucca Update:Felucca Update MIDI 1 20:0"]) == LOADER
assert pick(["TACK FM-1:TACK FM-1 MIDI 1 20:0", "Felucca Update:Felucca Update MIDI 1 24:0"]) == TACK
assert pick(["Midi Through:Midi Through Port-0 14:0"]) == TACK
assert pick([]) == TACK
PY
python3 -I firmware/fm1/tests/grid.py "$root/firmware/fm1/build/host/tack_host" "$out/sim" 2>&1 | grep -E '^FAIL|Error'
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Wno-unused-function -Iengine -o "$out/names_test" firmware/fm1/tests/names_test.c && timeout 60 "$out/names_test" | tail -1 | sed 's/^FAILED$/FAIL a kit name/'
timeout 300 bash firmware/fm1/tests/lock_test.sh "$out/lock" 2>&1 | grep -E 'FAIL|failed|passed'
timeout 300 bash firmware/fm1/tests/compare_seq.sh "$out/seq" 2>&1 | tail -1 | sed 's/^FAILED$/FAIL the sequencer no longer matches the LXR/'
sleep 0.2
if grep -qE 'FAIL|Traceback|Error|failed at|failed, see' "$out/verify.log"; then
    echo "verify: FAILED"
    exit 1
fi
echo "verify: passed"
