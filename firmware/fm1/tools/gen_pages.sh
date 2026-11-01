#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Rewrites firmware/fm1/src/tack_pages.h, or writes it to OUT. Needs the desktop build in build/host.
#   firmware/fm1/tools/gen_pages.sh [OUT]
set -euo pipefail
dest=$(realpath -m "${1:-$(dirname "$0")/../src/tack_pages.h}")
cd "$(dirname "$0")/../../.."
out=$(mktemp -d)
c++ -xc -Iengine -Ihost/src -c firmware/fm1/tools/gen_pages.c -o "$out/gen.o" -Wno-everything 2>/dev/null || cc -Iengine -Ihost/src -c firmware/fm1/tools/gen_pages.c -o "$out/gen.o"
c++ "$out/gen.o" build/host/liblxr_engine.a -lm -o "$out/gen"
"$out/gen" > "$dest"
rm -rf "$out"
