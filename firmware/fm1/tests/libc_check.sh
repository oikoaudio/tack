#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# The app as the FM-1 compiles it: device/unity.c with the device's include paths and no system headers, checked by the host's
# clang. The simulator compiles the app beside plat_host.c's <string.h> and <stdio.h>, so a call the FM-1's libc doesn't have
# (it has memset, memcpy and memcmp) builds and runs there and fails only when JieLi's linker meets it. Here it is an error.
#   firmware/fm1/tests/libc_check.sh        (from anywhere; needs the generated headers: build.py firmware/fm1 --host first)
set -euo pipefail
cd "$(dirname "$0")/../../.."
base=third_party/fm1-base
[ -d firmware/fm1/build/gen ] || { echo "libc check: no firmware/fm1/build/gen: build the simulator first" >&2; exit 2; }
# ELF, as the device: a Mach-O target rejects the device's section names. USB as fm1.toml has it (usb_audio_in, not out)
clang --target=i386-unknown-none-elf -fsyntax-only -nostdlibinc -ffreestanding -fno-builtin -w \
    -Werror=implicit-function-declaration \
    -I"$base/bsp/chip" -I"$base/bsp/boards/fm1" -I"$base/device" -I"$base/include" -I"$base/lib" \
    -Ifirmware/fm1 -Ifirmware/fm1/build/gen \
    -DFELUCCA_ID='"TACK"' -DFM1B_UAC=1 -DFM1B_UAC_OUT=0 -DFELUCCA_UAC_MIDI_LAST=1 \
    "$base/device/unity.c"
echo "libc check passed"
