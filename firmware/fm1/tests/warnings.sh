#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Tack's own firmware code stays free of warnings. The real build turns them off, for the sake of the engine it shares a
# configuration with, so this reads the same sources again with them on and writes nothing.
set -euo pipefail
cd "$(dirname "$0")/../../.."
mapfile -t flags < <(python3 firmware/fm1/tests/fm1_sources.py flags)
for f in firmware/fm1/src/tack_engine.c firmware/fm1/src/seq_platform.c engine/kit_mutate.c engine/kit_name.c engine/lfo_speed.c; do
    ${CC:-clang} -fsyntax-only -std=gnu11 -fcommon -Ihost/src -DPLAT_HOST "${flags[@]}" -Wall -Wextra -Werror -Wno-unused-parameter \
        -Wno-incompatible-library-redeclaration "$f"
done
