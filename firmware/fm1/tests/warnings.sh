#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Tack's own firmware code stays free of warnings. The real build turns them off, for the sake of the engine it shares a
# configuration with, so this reads the same sources again with them on and writes nothing.
set -euo pipefail
cd "$(dirname "$0")/../../.."
for f in firmware/fm1/src/tack_engine.c firmware/fm1/src/seq_platform.c engine/kit_mutate.c engine/kit_name.c engine/lfo_speed.c; do
    ${CC:-clang} -fsyntax-only -Wall -Wextra -Werror -Wno-unused-parameter -Wno-incompatible-library-redeclaration -std=gnu11 -fcommon -ICore/Inc -Iengine -Ihost/src -Ifirmware/fm1/src \
        -include firmware/fm1/shim/fm1_prefix.h -DPLAT_HOST -DNUM_PATTERN=16 '-DREAL_FS=44100.f' -Ifirmware/fm1/shim \
        -Ithird_party/fm1-base/include -Xclang -cl-single-precision-constant "$f"
done
