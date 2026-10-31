#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
#
# Runs the command-line tests against a build: renders, event timing, independent instances, every plugin in
# Tack.clap and a state round trip, then the same for Tack.vst3 if the build has it. CI runs it on every
# platform; it works locally too.
#
#   host/tools/smoke_test.sh build/host            # Linux, macOS
#   host/tools/smoke_test.sh build/win wine        # the Windows build, through Wine
#
# Exits non-zero at the first failure.
set -euo pipefail

build=$1
run=${2:-}
exe=""
[ "$run" = wine ] && exe=.exe && export WINEDEBUG=-all
plugin=$build/Tack.clap
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

step() { echo "== $*"; }
tool() { local t=$1; shift; $run "$build/$t$exe" "$@"; }

step "engine render"
tool lxr_render -r 48000 "$out/render.wav" >/dev/null

for rate in 44100 48000 96000; do
	step "event timing at $rate Hz"
	tool timing_test $rate
done

step "kits for the tests"
tool make_kit "$out/a.snd" KITA 37=60 8=40
tool make_kit "$out/b.snd" KITB 43=100 74=90
tool make_kit "$out/c.snd" KITC 197=120

step "independent instances"
tool multi_test "$plugin" "$out/a.snd" "$out/b.snd" "$out/c.snd"

for id in com.oikoaudio.tack com.oikoaudio.tack.drum com.oikoaudio.tack.snare com.oikoaudio.tack.cymbal \
	com.oikoaudio.tack.hihat; do
	step "$id"
	TACK_PLUGIN_ID=$id tool clap_render "$plugin" "$out/p.wav" 48000 -p "$out/a.snd" 37=80 37~-20@0.3 >/dev/null
done

# the state should bring back everything a render depends on
step "state round trip"
tool clap_render "$plugin" "$out/s1.wav" 48000 -p "$out/b.snd" 37=70 1000=1 >/dev/null
tool clap_render "$plugin" "$out/s2.wav" 48000 -p "$out/b.snd" 37=70 1000=1 -s >/dev/null
cmp "$out/s1.wav" "$out/s2.wav"

if [ -d "$build/Tack.vst3" ]; then
	step "Tack.vst3"
	tool vst3_test "$build/Tack.vst3" 2> >(grep -v "AUDIO in" >&2)
fi

echo "all passed"
