#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""README.md -> the site's manual page (tools/make_pages.py puts it at manual/index.html): the README
is the manual, so the two never disagree.

  tools/manual_html.py [IN.md [OUT.html]]      (default: README.md -> build/manual.html)

From AMB-1's tools/manual_html.py. Handles what the README uses: headings, paragraphs, **bold**,
*italics*, `code`, links, lists, tables, fenced code, pictures and callouts (> ...). Links to files in
the repository go to GitHub, links to the site become relative, pictures come from the site's img/
(docs/images). Also holds the site's colours, fonts and header, for the other pages."""
import functools
import html
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
NAME = "MIN-1"
REPO = "https://github.com/Reytian/fm1-min1"
SITE = "https://reytian.github.io/fm1-min1/"
IMAGES = "docs/images/"                               # the README's pictures; on the site, img/

# Barlow, as the emulator. Loaded without holding up the page: where Google Fonts can't be reached
# (mainland China) a blocking stylesheet leaves the page blank until it times out.
FONTS = """<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Barlow:wght@400;600&family=Barlow+Semi+Condensed:wght@600;700&display=swap" media="print" onload="this.media='all'">
<meta name="theme-color" content="#ecf0f2" media="(prefers-color-scheme: light)">
<meta name="theme-color" content="#0f151c" media="(prefers-color-scheme: dark)">"""

# the colours of MIN-1's screen (firmware/src/app/ui.c), as the emulator: cool paper, dark blue ink,
# cobalt; a dark variant
THEME = """
:root { --paper: #ecf0f2; --panel: #fafcfd; --line: #c8d2d8; --muted: #4f5d68; --ink: #16202c; --accent: #2463c4; --on-accent: #fafcfd;
        --body: "Barlow", "Helvetica Neue", Helvetica, Arial, sans-serif; --display: "Barlow Semi Condensed", "Arial Narrow", Arial, sans-serif;
        --mono: ui-monospace, Menlo, Consolas, monospace; color-scheme: light; }
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) { --paper: #0f151c; --panel: #18212b; --line: #2b3742; --muted: #9eabb6; --ink: #e4eaee; --accent: #7aa7ec; --on-accent: #0f151c; color-scheme: dark; }
}
:root[data-theme="dark"] { --paper: #0f151c; --panel: #18212b; --line: #2b3742; --muted: #9eabb6; --ink: #e4eaee; --accent: #7aa7ec; --on-accent: #0f151c; color-scheme: dark; }
body { background: var(--paper); color: var(--ink); margin: 0; padding-inline: 16px; padding-block: 28px 64px; font: 400 17px/1.6 var(--body); }
a { color: var(--ink); text-decoration-color: var(--accent); text-decoration-thickness: 2px; text-underline-offset: 3px; }
a:hover { color: var(--accent); }
a:focus-visible { outline: 2px solid var(--accent); outline-offset: 2px; }
.site { display: flex; flex-wrap: wrap; align-items: baseline; gap: .3rem 1.2rem; margin: 0 0 1.8rem; padding-bottom: .6rem; border-bottom: 1px solid var(--line); }
.site .home { font: 700 1.35rem/1.1 var(--display); letter-spacing: .08em; text-decoration: none; }
.site nav { margin-left: auto; display: flex; flex-wrap: wrap; gap: .2rem 1rem; }
.site nav a[aria-current] { text-decoration: none; color: var(--accent); }
"""

CSS = """
main { max-width: 46rem; margin: 0 auto; }
h1, h2, h3 { font-family: var(--display); line-height: 1.15; text-wrap: balance; }
h1 { font-size: 2.6rem; font-weight: 700; letter-spacing: .06em; margin: 0 0 .4rem; }
h2 { font-size: 1.55rem; font-weight: 600; letter-spacing: .03em; margin: 2.8rem 0 .8rem; padding-top: .9rem; border-top: 1px solid var(--line); }
h3 { font-size: 1.25rem; font-weight: 600; letter-spacing: .03em; margin: 2rem 0 .6rem; }
p, li { max-width: 68ch; }
p { margin: 0 0 1rem; }
.lede { color: var(--muted); font-size: 1.1rem; margin-bottom: 1.6rem; }
ul, ol { padding-left: 1.3rem; margin: 0 0 1rem; }
li { margin: .3rem 0; }
li::marker { color: var(--accent); }
strong { font-weight: 600; }
code, pre { font-family: var(--mono); font-size: .86em; }
code { background: var(--panel); border: 1px solid var(--line); border-radius: 4px; padding: .05em .35em; overflow-wrap: anywhere; }
pre { background: var(--panel); border: 1px solid var(--line); border-radius: 6px; padding: 14px 16px; overflow-x: auto; line-height: 1.45; margin: 0 0 1.2rem; }
pre code { background: none; border: 0; padding: 0; font-size: inherit; overflow-wrap: normal; }
.table { overflow-x: auto; margin: 0 0 1.4rem; }
table { border-collapse: collapse; width: 100%; font-size: .95rem; line-height: 1.5; }
th, td { text-align: left; vertical-align: top; padding: .5rem .9rem .5rem 0; border-bottom: 1px solid var(--line); }
th { font: 600 .85rem var(--display); letter-spacing: .08em; text-transform: uppercase; color: var(--muted); border-bottom-color: var(--ink); }
td:first-child { font-weight: 600; min-width: 7.5rem; }
figure { margin: .4rem 0 1.8rem; }
figure img { display: block; width: 100%; height: auto; border-radius: 8px; }
figcaption { color: var(--muted); font-size: .9rem; margin-top: .45rem; }
.callout { border-left: 4px solid var(--accent); background: var(--panel); padding: 10px 16px; margin: 0 0 1.2rem; max-width: 68ch; }
.callout p { margin: 0; }
@media (prefers-reduced-motion: no-preference) { html { scroll-behavior: smooth; } }
"""

CJK = re.compile(r"[　-〿㐀-鿿＀-￯]")


def header(current, home="../"):
    """the site's header: MIN-1, home; the pages; the current one marked"""
    pages = [("emu/", "Emulator"), ("install/", "Install"), ("manual/", "Manual")]
    nav = "".join(f'<a href="{home}{h}"' + (' aria-current="page"' if t == current else "") + f">{t}</a>" for h, t in pages)
    return f'<header class="site"><a class="home" href="{home}">{NAME}</a><nav>{nav}<a href="{REPO}">Source</a></nav></header>'


def href(url):
    """a README link on the site: the site's own pages relative, files of the repository on GitHub"""
    if url.startswith(SITE):
        return "../" + url[len(SITE):]
    if re.match(r"[a-z][a-z0-9+.-]*:|#", url, re.I):
        return url
    path, _, frag = url.partition("#")
    return f"{REPO}/blob/main/{path}" + (f"#{frag}" if frag else "")


def inline(t):
    codes = []                                        # code first, kept away from the rest

    def keep(m):
        codes.append(f"<code>{html.escape(m.group(1), quote=False)}</code>")
        return f"\0{len(codes) - 1}\0"
    t = re.sub(r"`([^`]+)`", keep, t)
    t = html.escape(t, quote=False)
    t = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", t)
    t = re.sub(r"(?<![\w*])\*([^*\s][^*]*)\*(?![\w*])", r"<em>\1</em>", t)
    t = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", lambda m: f'<a href="{href(m.group(2))}">{m.group(1)}</a>', t)
    t = re.sub(r"&lt;(https?://\S+?)&gt;", lambda m: f'<a href="{href(m.group(1))}">{m.group(1)}</a>', t)
    return re.sub(r"\0(\d+)\0", lambda m: codes[int(m.group(1))], t)


def lang(text):
    return ' lang="zh-Hans"' if CJK.search(text) else ""


def join(a, b):
    """two lines of one paragraph: a space between them, but none between Chinese characters (a browser
    shows the line break there as a space)"""
    a, b = a.rstrip(), b.strip()
    return a + b if a and b and CJK.match(a[-1]) and CJK.match(b[0]) else f"{a} {b}".strip()


def figure(alt, src):
    if not src.startswith(IMAGES):
        raise SystemExit(f"README picture {src}: the site has only {IMAGES} (as img/)")
    return (f'<figure><img src="../img/{html.escape(src[len(IMAGES):])}" alt="{html.escape(alt)}" loading="lazy">'
            f"<figcaption>{inline(alt)}</figcaption></figure>")


def convert(md):
    lines = md.splitlines()
    out, i, ids = [], 0, set()

    def slug(title):                                 # GitHub's: the README's own links keep working
        s = re.sub(r"[^\w\- ]", "", title.strip().lower()).replace(" ", "-")
        base, n = s, 0
        while s in ids:
            n += 1
            s = f"{base}-{n}"
        ids.add(s)
        return s
    while i < len(lines):
        ln = lines[i]
        if ln.startswith("```"):
            j = i + 1
            while j < len(lines) and not lines[j].startswith("```"):
                j += 1
            out.append("<pre><code>" + html.escape("\n".join(lines[i + 1:j]), quote=False) + "</code></pre>")
            i = j + 1
            continue
        if ln.startswith("!["):
            m = re.match(r"!\[(.*)\]\((\S+)\)\s*$", ln)
            if not m:
                raise SystemExit(f"README line {i + 1}: one picture a line: {ln}")
            out.append(figure(m.group(1), m.group(2)))
        elif ln.startswith("> "):                     # a callout: the important bit
            para = []
            while i < len(lines) and lines[i].startswith(">"):
                para.append(lines[i][1:].strip())
                i += 1
            text = functools.reduce(join, para)
            out.append(f'<div class="callout"{lang(text)}><p>{inline(text)}</p></div>')
            continue
        elif ln.startswith("# "):                     # the README's title: the site's header, then Manual
            out.append(header("Manual"))
            out.append("<h1>Manual</h1>")
        elif ln.startswith("## "):
            t = ln[3:].strip()
            out.append(f'<h2 id="{slug(t)}"{lang(t)}>{inline(t)}</h2>')
        elif ln.startswith("### "):
            t = ln[4:].strip()
            out.append(f'<h3 id="{slug(t)}"{lang(t)}>{inline(t)}</h3>')
        elif ln.strip() == "---":
            out.append("<hr>")
        elif ln.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append([c.strip() for c in lines[i].strip().strip("|").split("|")])
                i += 1
            head, body = rows[0], [r for r in rows[1:] if not set("".join(r)) <= set("-: ")]
            thead = "" if not "".join(head) else \
                "<thead><tr>" + "".join(f"<th>{inline(c)}</th>" for c in head) + "</tr></thead>"
            out.append('<div class="table"><table>' + thead + "<tbody>" +
                       "".join("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>" for r in body) +
                       "</tbody></table></div>")
            continue
        elif re.match(r"(\d+\.|-) ", ln):
            ordered = ln[0].isdigit()
            items = []
            while i < len(lines) and (re.match(r"(\d+\.|-) ", lines[i]) or (lines[i].startswith("  ") and items)):
                if lines[i].startswith("  "):
                    items[-1] = join(items[-1], lines[i])
                else:
                    items.append(re.sub(r"^(\d+\.|-) ", "", lines[i]))
                i += 1
            tag = "ol" if ordered else "ul"
            out.append(f"<{tag}>" + "".join(f"<li{lang(t)}>{inline(t)}</li>" for t in items) + f"</{tag}>")
            continue
        elif ln.strip():
            para = [ln]
            while i + 1 < len(lines) and lines[i + 1].strip() and not re.match(r"(#|\||```|---|\d+\. |- |!\[|> )", lines[i + 1]):
                i += 1
                para.append(lines[i])
            text = functools.reduce(join, para)
            if out and out[-1].startswith("<h1>"):
                out.append(f'<p class="lede"{lang(text)}>{inline(text)}</p>')
            else:
                out.append(f"<p{lang(text)}>{inline(text)}</p>")
        i += 1
    return "\n".join(out)


def page(md):
    body = convert(md)
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{NAME} Manual</title>
<meta name="description" content="The manual for {NAME}, minimalist music for the M-VAVE FM-1: the four models, the keys, the pages, songs.">
{FONTS}
<style>{THEME}{CSS}</style>
</head>
<body>
<main>
{body}
</main>
</body>
</html>
"""


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else SRC / "README.md"
    dst = Path(sys.argv[2]) if len(sys.argv) > 2 else SRC / "build" / "manual.html"
    dst.parent.mkdir(parents=True, exist_ok=True)
    text = page(src.read_text(encoding="utf-8"))
    dst.write_text(text, encoding="utf-8")
    print(f"{dst}: {len(text)} bytes")


if __name__ == "__main__":
    main()
