#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Rewrites firmware/fm1/src/tack_pages.h. Needs the desktop build in build/host.
set -euo pipefail
cd "$(dirname "$0")/../../.."
out=$(mktemp -d)
c++ -xc -Ihost/src -c firmware/fm1/tools/gen_pages.c -o "$out/gen.o" -Wno-everything 2>/dev/null || cc -Ihost/src -c firmware/fm1/tools/gen_pages.c -o "$out/gen.o"
c++ "$out/gen.o" build/host/liblxr_engine.a -lm -o "$out/gen"
"$out/gen" > firmware/fm1/src/tack_pages.h
rm -rf "$out"
