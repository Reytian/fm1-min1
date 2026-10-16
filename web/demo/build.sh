#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# MIN-1's demo video, the page: the emulator (web/emu) with the demo's driver, captions and storyboard.
#   web/demo/build.sh  ->  build/demo/site/{index.html, min.wasm, demo.js, demo.css, storyboard.js}
# then  node web/demo/record.mjs  (see there). Same sources and flags as web/emu/build.sh.
set -e
cd "$(dirname "$0")/../.."
mkdir -p build/gen build/demo/site
[ -f build/gen/felucca_font.h ] || python3 tools/gen_font.py build/gen/felucca_font.h >/dev/null
emcc -O2 -ffp-contract=off -std=gnu99 -Wall -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable \
    -DEN_HOST -DEN_WEB "-DEN_VERSION=\"$(printf %s "${EN_VERSION:-0.2}" | tr a-z A-Z)\"" -Ifirmware/src -Ifirmware/src/dsp -Ibuild/gen \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o build/demo/site/min.wasm web/demo/demo_web.c firmware/src/dsp/min.c
# the page as the emulator has it, with the demo's stylesheet and scripts after its own, and its fonts
# loaded before it shows (the site loads them on the side)
sed -e 's# media="print" onload="this.media=.all."##' \
    -e 's#</head>#<link rel="stylesheet" href="demo.css">\n</head>#' \
    -e 's#</body>#<script src="storyboard.js"></script>\n<script src="demo.js"></script>\n</body>#' \
    web/emu/index.html > build/demo/site/index.html
cp web/demo/demo.js web/demo/demo.css web/demo/storyboard.js build/demo/site/
echo "demo: build/demo/site ($(wc -c < build/demo/site/min.wasm) B wasm)"
