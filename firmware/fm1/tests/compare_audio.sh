#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Builds the engine four ways and renders kits through each, then compares them (compare_audio.py).
#   firmware/fm1/tests/compare_audio.sh OUTDIR [kit.snd ...]
# A desktop arithmetic, 44140 | B FM-1 arithmetic, 44140 | C FM-1 arithmetic, REAL_FS 44100
# D is C's engine at 44140 written as 44100: what running the engine unadapted at the FM-1's rate would give.
set -euo pipefail
trap 'echo "compare_audio: failed at line $LINENO" >&2' ERR
out=$(realpath -m "$1"); shift
cd "$(dirname "$0")/../../.."
mkdir -p "$out"
CC=${CC:-clang}
src=(Core/Src/Soundengine/DSPAudio/*.c Core/Src/Soundengine/DSPAudio/FX/*.c Core/Src/Soundengine/DSPAudio/FX/VerbTools/*.c
     Core/Src/Soundengine/FxBus.c Core/Src/Soundengine/MIDI/ParameterArray.c Core/Src/Soundengine/MIDI/MidiVoiceControl.c
     Core/Src/Soundengine/MIDI/MidiMessages.c Core/Src/Soundengine/MIDI/MidiParser.c Core/Src/Parameters.c
     Core/Src/Preset/initKit.c Core/Src/menu/Cc2Text.c engine/lxr_core.c host/src/stubs.c)
fm1=(-include firmware/fm1/shim/fm1_prefix.h -DPLAT_HOST -Ifirmware/fm1/shim -Ithird_party/fm1-base/include
     -Xclang -cl-single-precision-constant)
desk=(-DLXR_HOST_BUILD -Ihost/shim -fcommon)
build() {   # NAME FLAGS...
    local name=$1; shift
    mkdir -p "$out/obj/$name"
    local objs=()
    for s in "${src[@]}"; do
        local o="$out/obj/$name/$(echo "$s" | tr / _).o"
        objs+=("$o")
        ( "$CC" -O2 -ffp-contract=off -std=gnu11 -Wno-everything -fcommon -ICore/Inc -Iengine -Ihost/src "$@" -c "$s" -o "$o" ) &
        while [ "$(jobs -r | wc -l)" -ge 16 ]; do wait -n; done
    done
    wait
    "$CC" -O2 -Wno-everything firmware/fm1/tests/engine_render.c "${objs[@]}" -lm -o "$out/render_$name"
}
build A "${desk[@]}"
build B "${fm1[@]}"
build C "${fm1[@]}" '-DREAL_FS=44100.f'
kits=("-" "$@")
i=0
for k in "${kits[@]}"; do
    "$out/render_A" "$out/k${i}_A.wav" 44140 "$k"
    "$out/render_A" "$out/check.wav" 44140 "$k"              # a reference that moves between runs proves nothing
    cmp "$out/k${i}_A.wav" "$out/check.wav"
    "$out/render_B" "$out/k${i}_B.wav" 44140 "$k"
    "$out/render_C" "$out/k${i}_C.wav" 44100 "$k"
    "$out/render_B" "$out/k${i}_D.wav" 44100 "$k"
    i=$((i+1))
done
python3 -I firmware/fm1/tests/compare_audio.py "$out" "$i"
