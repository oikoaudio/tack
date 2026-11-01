#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Desktop Tack must render byte for byte as before and the firmware must pass its checks; a failed build stops here.
#   firmware/fm1/tests/verify.sh OUTDIR
# Every step's exit code counts: a crash, a hang or a build that fails is a failure, whatever the step printed. The log is searched too,
# as a script's expect line prints FAIL and carries on. The last line is the verdict, and so is the exit code.
set -uo pipefail
out=$(realpath -m "$1")
cd "$(dirname "$0")/../../.."
root=$PWD
sim=${VERIFY_SIM:-$root/firmware/fm1/build/host/tack_host}   # (another binary: to see a crash or a hang fail this)
mkdir -p "$out/renders" "$out/sim"
exec > >(tee "$out/verify.log") 2>&1
source firmware/fm1/tests/verify_lib.sh
run "verify's own self-test" bash firmware/fm1/tests/verify_selftest.sh
run "warnings in the firmware's own code" bash firmware/fm1/tests/warnings.sh
stop_if_failed() {
    if [ "$fails" != 0 ]; then
        echo "$1"
        echo "verify: FAILED"
        exit 1
    fi
}

cmake --build build/host > "$out/desktop.log" 2>&1 || fail "desktop build, see $out/desktop.log"
stop_if_failed "desktop build failed"
for v in 0 1 2 3 4 5 6; do run "render voice $v" build/host/lxr_render "$out/renders/voice$v.wav" $v > /dev/null; done
run "render kit" build/host/lxr_render "$out/renders/kit.wav" > /dev/null
run "render kit at 48k" build/host/lxr_render -r 48000 "$out/renders/kit48k.wav" > /dev/null
renders() { (cd "$out/renders" && sha256sum -c "$root/firmware/fm1/tests/desktop_renders.sha256" 2>&1 | awk '{print $NF}' | sort | uniq -c); }
run "desktop renders" renders
smoke() { bash host/tools/smoke_test.sh build/host 2>&1 | tail -1; }
run "smoke test" smoke
mutate() { timeout 120 build/host/mutate_test | tail -1; }
run "mutate test" mutate
lfo() { timeout 60 build/host/lfo_test | tail -1; }
run "lfo test" lfo
export JIELI_TOOLCHAIN=$HOME/.jieli/toolchain AC79_SDK=$HOME/.jieli/ac79-sdk JIELI_DOCKER=0
build_firmware() { python3 third_party/fm1-base/tools/build.py firmware/fm1 2>&1 | quiet_grep '^  (ok|FAIL|fail)|error|^package|ERROR|failed'; }
run "firmware build" build_firmware
build_sim() { CC=clang python3 third_party/fm1-base/tools/build.py firmware/fm1 --host 2>&1 | quiet_grep ' error|undefined|did not|^host'; }
run "simulator build" build_sim
stop_if_failed "a build failed"
python3 -c "import sys; sys.stdout.buffer.write(b'KITONE  ' + bytes([40]) * 248 + b'KITTWO  ' + bytes([50]) * 248)" > "$out/sim/kits.bin"
python3 -c "import sys; sys.stdout.buffer.write(b''.join(b'KIT%d    ' % i + bytes([40 + i]) * 248 for i in range(4)))" > "$out/sim/kits4.bin"
python3 -c "import sys; sys.stdout.buffer.write(b'KITONE  ' + bytes([40]) * 248 + b'\xff' * 768)" > "$out/sim/kits1.bin"
script() { (cd "$out/sim" && timeout 60 "$sim" "$root/firmware/fm1/tests/$1.txt" . 2>&1 | grep -v -E '^  ok' | tail -3); }
for t in keys fxswitch sequencer song note kits recur rows voice clock fxdc pages shapes lanes crush morph patkit mutate bars lfosync reverb; do
    run "script $t" script "$t"
done
for f in firmware/fm1/tests/regress/*.txt; do    # one script a bug, each from a fresh start
    regress() { (cd "$out/sim" && timeout 60 "$sim" "$root/$1" . 2>&1 | grep -v -E '^  ok' | tail -3); }
    run "regress $(basename "$f" .txt)" regress "$f"
done
# a misspelt expect name must fail, or a test can pass without looking at anything
misspelt() { (cd "$out/sim" && ! timeout 60 "$sim" "$root/firmware/fm1/tests/misspelt_expect.txt" . > /dev/null 2>&1); }
run "a misspelt expect name is an error" misspelt
midi_once() { (cd "$out/sim" && timeout 60 "$sim" "$root/firmware/fm1/tests/midi_once.txt" . 2>&1 | quiet_grep '^  out (80|90)'); }
notes=$(midi_once); rc=$?
[ "$rc" = 0 ] || fail "script midi_once (exit $rc)"
[ -n "$notes" ] || fail "midi_once sent no notes"
[ -z "$(printf '%s\n' "$notes" | uniq -d)" ] || fail "a note-on went out twice"
python3 -I - "$out/sim/verb.wav" <<'PY' || fail "the reverb has no tail"
import array, sys, wave
w = wave.open(sys.argv[1])
n = w.getnframes()
a = array.array("h", w.readframes(n))
sys.exit(max(abs(x) for x in a[n // 2:]) < 200)
PY
python3 -I - "$out/sim/fxswitch.wav" <<'PY' || fail "the reverb played what the delay left"
import array, sys, wave
w = wave.open(sys.argv[1])
a = array.array("h", w.readframes(w.getnframes()))
sys.exit(not a or max(abs(x) for x in a) > 2)
PY
python3 -I - "$out/sim/fxdc.wav" "$out/sim/fxdc2.wav" <<'PY' || fail "the distortion left a steady level"
import array, sys, wave
for f in sys.argv[1:]:
    w = wave.open(f)
    n = w.getnframes()
    a = array.array("h", w.readframes(n))
    s = a[n:]
    if not s or max(abs(x) for x in s) > 2:
        sys.exit(1)
PY
python3 -I - "$out/sim/crush_off.wav" "$out/sim/crush_on.wav" "$out/sim/bits_on.wav" "$out/sim/bits_quiet.wav" <<'PY' || fail "RATE or BITS did not reach the audio"
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
PYTHONPATH=firmware/fm1/tools python3 - <<'PY' || fail "fm1 picks the wrong port"
from fm1_port import pick, TACK, LOADER
assert pick(["Midi Through:Midi Through Port-0 14:0", "TACK FM-1:TACK FM-1 MIDI 1 20:0"]) == TACK
assert pick(["Midi Through:Midi Through Port-0 14:0", "Felucca Update:Felucca Update MIDI 1 20:0"]) == LOADER
assert pick(["TACK FM-1:TACK FM-1 MIDI 1 20:0", "Felucca Update:Felucca Update MIDI 1 24:0"]) == TACK
assert pick(["Midi Through:Midi Through Port-0 14:0"]) == TACK
assert pick([]) == TACK
PY
check_tool() {                                   # (a stale magic in the tool would pass every current backup and fail none)
    grep -q "\"$(sed -n 's/^PATTERN_MAGIC = b"\(KPT[0-9]*\)".*/\1/p' firmware/fm1/tools/tack_check.py)\"" firmware/fm1/src/tack_engine.c
}
run "tack_check knows the pattern format" check_tool
pages_current() {                                # gen_pages.c drops a slot whose name no longer matches: this shows it
    bash firmware/fm1/tools/gen_pages.sh "$out/tack_pages.h" && cmp "$out/tack_pages.h" firmware/fm1/src/tack_pages.h
}
run "tack_pages.h matches its generator" pages_current
pages() { python3 -I firmware/fm1/tests/pages.py "$sim" "$out/sim"; }
run "the panel's map" pages
random_panel() { python3 -I firmware/fm1/tests/panel_random.py "$sim" "$out/sim" 20; }
run "random playing" random_panel
grid() { python3 -I firmware/fm1/tests/grid.py "$sim" "$out/sim" 2>&1 | quiet_grep '^FAIL|Error'; }
run "grid" grid
names() {
    ${CC:-cc} -std=c11 -O2 -Wall -Wextra -Wno-unused-function -Iengine -o "$out/names_test" firmware/fm1/tests/names_test.c && timeout 60 "$out/names_test" | tail -1
}
run "kit names" names
locks() { timeout 300 bash firmware/fm1/tests/lock_test.sh "$out/lock" 2>&1 | quiet_grep 'FAIL|failed|passed'; }
run "lock test" locks
compare() { timeout 300 bash firmware/fm1/tests/compare_seq.sh "$out/seq" 2>&1 | tail -1; }
run "sequencer against the LXR" compare
sleep 0.2
if [ "$fails" != 0 ] || grep -qE 'FAIL|Traceback|Error|failed at|failed, see' "$out/verify.log"; then
    echo "verify: FAILED"
    exit 1
fi
echo "verify: passed"
