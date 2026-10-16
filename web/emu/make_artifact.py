#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The emulator as a claude.ai Artifact: build/emu (web/emu/build.sh) -> build/artifact/{index.html,
worklet.js}. The page loses its document wrapper (the Artifact host adds one) and its site links (the
title's home link, the nav); min.wasm goes into it as base64, so nothing is fetched; the fonts load as
a plain stylesheet (no inline handler); the Saving note says how a recording is saved there."""
import base64
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EMU, OUT = ROOT / "build/emu", ROOT / "build/artifact"
OUT.mkdir(parents=True, exist_ok=True)
s = (EMU / "index.html").read_text()
s = re.sub(r"<!doctype html>\s*<html[^>]*>\s*<head>\s*", "", s)
s = re.sub(r'<meta charset="utf-8">\s*<meta name="viewport"[^>]*>\s*', "", s)
s = s.replace("</head>\n<body>\n", "").replace("</body>\n</html>\n", "")
s = re.sub(r"\s*<nav>.*?</nav>", "", s)
s, n = re.subn(r'<h1><a href="\.\./"[^>]*>(.*?)</a></h1>', r"<h1>\1</h1>", s)
assert n == 1, "the title's home link"
s = s.replace(""" media="print" onload="this.media='all'">""", ">", 1)
old = "running at the FM-1's 44.1 kHz.\n"
assert old in s
s = s.replace(old, "running at the FM-1's 44.1 kHz. In claude.ai a recording is saved as a zip with the WAV inside, "
                   "since a WAV can't be saved from there directly.\n", 1)
wasm = base64.b64encode((EMU / "min.wasm").read_bytes()).decode()
old = 'fetch("min.wasm").then((r) => { if (!r.ok) throw new Error(`min.wasm: ${r.status}`); return r.arrayBuffer(); })'
assert old in s
s = s.replace(old, "Promise.resolve(Uint8Array.from(atob(MIN_WASM), (c) => c.charCodeAt(0)).buffer)")
s = s.replace('<script>\n"use strict";', f'<script>\n"use strict";\nconst MIN_WASM = "{wasm}";', 1)
s = s.replace("<title>MIN-1 in the browser</title>", "<title>MIN-1 Emulator</title>", 1)
(OUT / "index.html").write_text(s)
(OUT / "worklet.js").write_text((EMU / "worklet.js").read_text())
print(f"artifact: {OUT / 'index.html'} ({len(s)} B), worklet.js")
