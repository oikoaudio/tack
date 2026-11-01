#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Every distinct kit under KITDIR, one voice at a time from the first block, through the builds compare_audio.sh left
# in OUTDIR: A desktop arithmetic at 44140, B the FM-1's at 44140, C the FM-1's at REAL_FS 44100.
#   firmware/fm1/tests/compare_kits.sh OUTDIR KITDIR
# Starting each voice at once keeps a free-running LFO's phase out of the comparison. A zip made on a Mac holds
# __MACOSX/._*.SND metadata beside each kit: those are not kits, and are skipped.
set -euo pipefail
trap 'echo "compare_kits: failed at line $LINENO" >&2' ERR
out=$(realpath "$1"); kits=$(realpath "$2")
cd "$(dirname "$0")/../../.."
clang -O2 -Wno-everything firmware/fm1/tests/engine_render.c "$out"/obj/A/*.o -lm -o "$out/render_A"
clang -O2 -Wno-everything firmware/fm1/tests/engine_render.c "$out"/obj/B/*.o -lm -o "$out/render_B"
clang -O2 -Wno-everything firmware/fm1/tests/engine_render.c "$out"/obj/C/*.o -lm -o "$out/render_C"
# R: desktop arithmetic with another noise seed. The LXR's noise comes from a hardware RNG and never repeats, so A
# against R is how far two renders of the same sound can differ: the yardstick for everything else here.
mkdir -p "$out/obj/R" "$out/seed"
cp host/shim/*.h "$out/seed/"
sed -i 's/s=2463534242u/s=88172645u/' "$out/seed/stm32f7xx_hal.h"
grep -q 88172645u "$out/seed/stm32f7xx_hal.h"
for src in Core/Src/Soundengine/DSPAudio/*.c Core/Src/Soundengine/DSPAudio/FX/*.c Core/Src/Soundengine/DSPAudio/FX/VerbTools/*.c \
    Core/Src/Soundengine/FxBus.c Core/Src/Soundengine/MIDI/ParameterArray.c Core/Src/Soundengine/MIDI/MidiVoiceControl.c \
    Core/Src/Soundengine/MIDI/MidiMessages.c Core/Src/Soundengine/MIDI/MidiParser.c Core/Src/Parameters.c \
    Core/Src/Preset/initKit.c Core/Src/menu/Cc2Text.c host/src/lxr_core.c host/src/stubs.c; do
    clang -O2 -ffp-contract=off -std=gnu11 -Wno-everything -fcommon -ICore/Inc -Ihost/src -DLXR_HOST_BUILD -I"$out/seed" \
        -c "$src" -o "$out/obj/R/$(echo "$src" | tr / _).o" &
    while [ "$(jobs -r | wc -l)" -ge 16 ]; do wait -n; done
done
wait
clang -O2 -Wno-everything firmware/fm1/tests/engine_render.c "$out"/obj/R/*.o -lm -o "$out/render_R"
work="$out/kits"; rm -rf "$work"; mkdir -p "$work"
declare -A seen
n=0
while IFS= read -r -d '' f; do
    case "$f" in */__MACOSX/*|*/._*) continue ;; esac
    h=$(sha256sum "$f" | cut -c1-16)
    [ -n "${seen[$h]:-}" ] && continue
    seen[$h]=1
    cp "$f" "$work/$n.snd"; chmod u+w "$work/$n.snd"
    n=$((n+1))
done < <(find "$kits" -iname '*.SND' -print0 | sort -z)
echo "$n distinct kits"
for ((k = 0; k < n; k++)); do
    for v in 0 1 2 3 4 5 6; do
        (
            ENGINE_RENDER_ONLY=$v "$out/render_A" "$work/${k}_${v}_A.wav" 44140 "$work/$k.snd"
            ENGINE_RENDER_ONLY=$v "$out/render_B" "$work/${k}_${v}_B.wav" 44140 "$work/$k.snd"
            ENGINE_RENDER_ONLY=$v "$out/render_C" "$work/${k}_${v}_C.wav" 44100 "$work/$k.snd"
            ENGINE_RENDER_ONLY=$v "$out/render_R" "$work/${k}_${v}_R.wav" 44140 "$work/$k.snd"
        ) &
        while [ "$(jobs -r | wc -l)" -ge 24 ]; do wait -n; done
    done
done
wait
python3 -I firmware/fm1/tests/compare_kits.py "$work" "$n"
