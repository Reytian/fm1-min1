#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make the MIN-1 site for GitHub Pages (https://reytian.github.io/fm1-min1/):

  index.html                  what MIN-1 is, and the ways in: try, install, manual, download, source
  img/                        the README's pictures (docs/images)
  emu/                        MIN-1 in the browser (build/emu, from web/emu/build.sh)
  install/index.html          the web installer (web/min_installer.html, with fm1pkg.js, fm1ota.js and
                              the package's metadata inlined; Chrome or Edge, Web MIDI)
  manual/index.html           the manual: README.md (tools/manual_html.py)
  firmware/min-VERSION.fwsc   the package the installer writes; also the download

  tools/make_pages.py build/min-0.2.fwsc 0.2 OUT_DIR

From AMB-1's. The package must be the release build of VERSION (./build.sh --release X.Y): its identity,
FM-1_5XXYYZZ, is what the installer checks the download against and what the FM-1 reports after the
install. tools/publish_pages.sh builds the emulator and runs this from the last commit."""
import html
import json
import re
import shutil
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SRC / "web"))
sys.path.insert(0, str(SRC / "tools"))
from make_site import product_of, strip_module  # noqa: E402  (Felucca's: the package format)
from manual_html import FONTS, IMAGES, REPO, SITE, THEME, page as manual_page  # noqa: E402

DEMO = "https://github.com/Reytian/fm1-min1/releases/download/v0.2/min1-demo.mp4"   # recorded with 0.2

LANDING = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>MIN-1</title>
<meta name="description" content="Minimalist music for the M-VAVE FM-1: a small ensemble that plays itself from the moment it's switched on, and that anyone can take over. Try it in your browser, or install it from Chrome or Edge.">
<meta property="og:title" content="MIN-1: minimalist music for the M-VAVE FM-1">
<meta property="og:description" content="A small ensemble that plays itself, and that anyone can take over. Try it in your browser.">
<meta property="og:image" content="__SITE__img/min1.png">
__FONTS__
<style>__THEME__
main { max-width: 46rem; margin: 0 auto; }
h1 { font: 700 3.6rem/1 var(--display); letter-spacing: .1em; margin: 0 0 .5rem; }
.sub { color: var(--muted); margin: 0 0 1.4rem; padding-bottom: 1rem; border-bottom: 1px solid var(--line); }
.lede { font-size: 1.15rem; margin: 0 0 1.6rem; max-width: 40rem; }
.hero { display: block; width: 100%; height: auto; border-radius: 10px; margin: 0 0 1.8rem; }
.ways { display: grid; gap: 10px; margin: 0 0 1rem; }
.ways a { display: block; text-decoration: none; color: var(--ink); background: var(--panel); border: 1px solid var(--line); border-radius: 8px; padding: 12px 16px; }
.ways a:hover { border-color: var(--accent); color: var(--ink); }
.ways a:first-child { background: var(--accent); border-color: var(--accent); }
.ways a:first-child, .ways a:first-child span { color: var(--on-accent); }
.ways strong { display: block; font: 600 1.25rem/1.3 var(--display); letter-spacing: .06em; text-transform: uppercase; }
.ways span { color: var(--muted); font-size: .95rem; }
.ways em.v { font-style: normal; text-transform: none; letter-spacing: .02em; }
.ways code { font: .88em var(--mono); overflow-wrap: anywhere; }
h2 { font: 600 1.55rem/1.2 var(--display); letter-spacing: .03em; margin: 2.8rem 0 .9rem; padding-top: .9rem; border-top: 1px solid var(--line); }
.models { display: block; width: 100%; max-width: 34rem; height: auto; border-radius: 10px; margin: 0 0 1.2rem; }
ul { padding-left: 1.2rem; margin: 0 0 1rem; }
li { margin: 0 0 .5rem; max-width: 68ch; }
li::marker { color: var(--accent); }
p { max-width: 68ch; }
p.small { color: var(--muted); font-size: .9rem; margin-top: 2.4rem; }
</style>
</head>
<body>
<main>
<h1>MIN-1</h1>
<p class="sub">Minimalist music for the M-VAVE FM-1, by Reytian. Inspired by <a href="https://github.com/charlesvestal/fm1-amb">FM1-AMB</a> and Philip Glass.</p>
<p class="lede">MIN-1 turns the FM-1 into a small minimalist ensemble, in the manner of the music of the
1960s to 80s that was made from very little: repeating figures, slow cycles of chords, processes you can
hear. It plays itself from the moment it's switched on; then you take over as much as you like.</p>
<img class="hero" src="img/min1.png" width="1920" height="1080" alt="MIN-1 in the browser emulator: the FM-1, and its screen at twice the size">
<nav class="ways" aria-label="Get MIN-1">
  <a href="emu/"><strong>Try it in your browser</strong><span>The same code the FM-1 runs, with sound. Mouse, touch or keyboard; no FM-1 needed.</span></a>
  <a href="install/"><strong>Install <em class="v">v__VERSION__</em></strong><span>From Chrome or Edge, with the FM-1 connected by USB. Nothing to install on the computer.</span></a>
  <a href="manual/"><strong>Manual</strong><span>The keys, the pages, from listening to making: everything in one page.</span></a>
  <a href="firmware/__PKG__"><strong>Download <em class="v">__PKG__</em></strong><span>For the command-line installer: <code>python3 tools/fm1_install.py __PKG__</code></span></a>
  <a href="__DEMO__"><strong>Watch the demo</strong><span>3:49, recorded from the browser emulator, no voice-over (MP4, 21 MB).</span></a>
  <a href="__REPO__"><strong>Source</strong><span>GitHub, GPL-3.0. Built on Felucca by Hügelton Instruments, by way of X0X, FoMni and AMB-1.</span></a>
</nav>

<h2>Four models, twenty pieces</h2>
<img class="models" src="img/models.png" width="1080" height="1000" loading="lazy" alt="The four models on the FM-1's screen: Arpeggio, Phase, Patterns, Bells">
<ul>
  <li><b>Arpeggio.</b> Figures of the chord's tones, two against three; the additive process grows and shrinks them a note at a time.</li>
  <li><b>Phase.</b> Two parts play one figure; the lower slowly drifts ahead, locks in a step ahead, drifts on, round to unison.</li>
  <li><b>Patterns.</b> A chain of short patterns, each a little changed from the last; three players move along it at their own pace over a high pulse.</li>
  <li><b>Bells.</b> A melody walks the scale by step in phrases that grow from one note; a bell answers each note with the chord's nearest tone.</li>
</ul>
<p>No music theory needed: in EASY keys every white key is a chord that fits. EDIT rolls ideas and keeps
the ones you like, SEL + REC learns your own chord cycle, and REC records a song that PLAY plays back exactly.</p>

<h2 lang="zh-Hans">中文简介</h2>
<p lang="zh-Hans">MIN-1 是 M-VAVE FM-1 的极简主义音乐固件，受 FM1-AMB 与 Philip Glass 启发。开机就自己演奏：重复的音型、缓慢的和弦循环、听得见的过程；然后交给你：按住白键就是和弦（不需要乐理），EDIT 随机出点子，教它你自己的和弦循环，亲手让声部进出，录下一首歌并原样回放。四种模型（琶音、相位、模式、钟声），二十首曲子。<a href="emu/">在浏览器里试玩</a>，或用 Chrome / Edge <a href="install/">安装</a>。</p>

<p class="small">Installing firmware is at your own risk. To go back to the official firmware, use the
installer's "Back to the stock firmware", or M-VAVE's updater, M-UPGRADE, from
<a href="https://www.m-vave.com/download">m-vave.com/download</a>. M-VAVE and FM-1 are trademarks of their
owners. MIN-1 is not affiliated with M-VAVE, Hügelton Instruments, Charles Vestal, Philip Glass, or any composer.</p>
</main>
</body>
</html>
"""


def identity(version):
    """the release build's identity for VERSION (tools/build.py --release): FM-1_5 + XX YY ZZ"""
    m = re.fullmatch(r"(\d{1,2})\.(\d{1,2})(?:\.(\d{1,2}))?(-[A-Za-z0-9]+)?", version)
    if not m:
        raise SystemExit(f"version {version!r}: X.Y[.Z][-suffix], as ./build.sh --release takes")
    return "FM-1_5%02d%02d%02d" % (int(m[1]), int(m[2]), int(m[3] or 0))


def main(pkg, version, out):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if product != identity(version):
        raise SystemExit(f"{pkg}: identity {product!r}, not {identity(version)!r}: make the release build "
                         f"(./build.sh --release {version})")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: no update loader in it")
    emu = SRC / "build" / "emu"
    if not (emu / "min.wasm").exists():
        raise SystemExit(f"no {emu / 'min.wasm'}: run web/emu/build.sh (needs Emscripten)")
    name = f"min-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    if out.exists():
        shutil.rmtree(out)
    for d in ("install", "manual", "firmware"):
        (out / d).mkdir(parents=True)
    shutil.copy(pkg, out / "firmware" / name)
    # the installer
    page = (SRC / "web" / "min_installer.html").read_text(encoding="utf-8")
    lib = strip_module((SRC / "web" / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((SRC / "web" / "fm1ota.js").read_text(encoding="utf-8"))
    meta = json.dumps({"version": version, "product": product, "pkg": "../firmware/" + name})
    for mark in ("/*LIB*/", "/*META*/"):
        if page.count(mark) != 1:
            raise SystemExit(f"min_installer.html must contain {mark} once")
    (out / "install" / "index.html").write_text(page.replace("/*LIB*/", lib).replace("/*META*/", meta), encoding="utf-8")
    # the manual (the README) and the pictures, for it and the landing page
    (out / "manual" / "index.html").write_text(manual_page((SRC / "README.md").read_text(encoding="utf-8")), encoding="utf-8")
    shutil.copytree(SRC / IMAGES, out / "img")
    # the landing page
    fill = {"__FONTS__": FONTS, "__THEME__": THEME, "__SITE__": SITE, "__VERSION__": html.escape(version),
            "__PKG__": name, "__DEMO__": DEMO, "__REPO__": REPO}
    landing = LANDING
    for k, v in fill.items():
        landing = landing.replace(k, v)
    (out / "index.html").write_text(landing, encoding="utf-8")
    # the emulator
    shutil.copytree(emu, out / "emu")
    (out / ".nojekyll").write_text("")
    print(f"site: {out}: index.html, install/ ({product}), manual/, emu/, firmware/{name} ({len(raw)} B)")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:4])
