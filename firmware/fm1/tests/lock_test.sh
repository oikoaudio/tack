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
src=(Core/Src/Soundengine/DSPAudio/*.c Core/Src/Soundengine/DSPAudio/FX/*.c Core/Src/Soundengine/DSPAudio/FX/VerbTools/*.c
     Core/Src/Soundengine/FxBus.c Core/Src/Soundengine/MIDI/ParameterArray.c Core/Src/Soundengine/MIDI/MidiVoiceControl.c
     Core/Src/Soundengine/MIDI/MidiMessages.c Core/Src/Soundengine/MIDI/MidiParser.c Core/Src/Parameters.c
     Core/Src/Preset/initKit.c Core/Src/menu/Cc2Text.c engine/lxr_core.c engine/kit_mutate.c engine/lfo_speed.c Core/Src/Soundengine/Sequencer/sequencer.c
     Core/Src/Soundengine/Sequencer/clockSync.c Core/Src/Soundengine/Sequencer/EuklidGenerator.c
     firmware/fm1/src/tack_engine.c firmware/fm1/src/seq_platform.c firmware/fm1/tests/lock_probe.c)
for s in "${src[@]}"; do
    "$CC" -O1 -g -ffp-contract=off -std=gnu11 -Wno-everything -Werror=implicit-function-declaration -fcommon -ICore/Inc \
        -Iengine -Ihost/src -Ifirmware/fm1/src -include firmware/fm1/shim/fm1_prefix.h -DPLAT_HOST -DNUM_PATTERN=16 \
        '-DREAL_FS=44100.f' -Ifirmware/fm1/shim -Ithird_party/fm1-base/include -Xclang -cl-single-precision-constant \
        -c "$s" -o "$out/obj/$(echo "$s" | tr / _).o" &
    while [ "$(jobs -r | wc -l)" -ge 16 ]; do wait -n; done
done
wait
"$CC" -O1 -g -Wno-everything firmware/fm1/tests/lock_test.c "$out"/obj/*.o -lm -o "$out/lock_test"
timeout 90 "$out/lock_test"
