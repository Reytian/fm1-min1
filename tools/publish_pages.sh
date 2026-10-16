#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the GitHub Pages site for release VERSION (tools/make_pages.py) and push it to the gh-pages
# branch of origin, replacing what was there (the branch holds only the generated site); the first time,
# switch GitHub Pages on for it. From AMB-1's.
#   tools/publish_pages.sh 0.2          (after ./build.sh --release 0.2)
#   tools/publish_pages.sh -n 0.2       build build/pages only, to look at before it goes out
# The site comes from the last commit (REV=... for another), not from the working tree: work in
# progress stays out of it. Needs Emscripten (emcc) and Python with Pillow (the emulator's font).
set -e
cd "$(dirname "$0")/.."
PUSH=1
[ "$1" = -n ] && { PUSH=; shift; }
V="${1:?usage: tools/publish_pages.sh [-n] VERSION}"
PKG="build/min-$V.fwsc"
[ -f "$PKG" ] || { echo "no $PKG: run ./build.sh --release $V first"; exit 1; }
command -v emcc >/dev/null || { echo "no emcc: install Emscripten (brew install emscripten)"; exit 1; }
python3 -c 'import PIL' 2>/dev/null || { echo "python3 has no Pillow (pip3 install Pillow)"; exit 1; }
SHA="$(git rev-parse --short "${REV:-HEAD}")"
rm -rf build/pages-src
mkdir -p build/pages-src
git archive "$SHA" | tar -x -C build/pages-src
EN_VERSION="$V" sh build/pages-src/web/emu/build.sh
python3 build/pages-src/tools/make_pages.py "$PKG" "$V" build/pages
if [ -z "$PUSH" ]; then echo "built build/pages for $V from $SHA (not pushed)"; exit 0; fi
# one commit of build/pages alone, made in this repository (so with its identity), leaving the
# checked-out branch and the index alone
GIT_DIR="$(git rev-parse --absolute-git-dir)"
IDX="$PWD/build/pages.index"
rm -f "$IDX"
COMMIT="$(cd build/pages && export GIT_DIR GIT_WORK_TREE=. GIT_INDEX_FILE="$IDX" &&
          git add -A . && git commit-tree "$(git write-tree)" -m "Site for MIN-1 $V (from $SHA)")"
rm -f "$IDX"
git push -q -f origin "$COMMIT:refs/heads/gh-pages"
REPO="$(git remote get-url origin | sed -E 's#(git@github.com:|https://github.com/)##; s#\.git$##')"
if gh api "repos/$REPO/pages" >/dev/null 2>&1; then
    # ask GitHub Pages to build it now: a push alone has left a deploy stuck on "building"
    gh api -X POST "repos/$REPO/pages/builds" >/dev/null 2>&1 || echo "(could not ask GitHub Pages to build: check the repository's Pages settings)"
elif gh api -X POST "repos/$REPO/pages" -f "source[branch]=gh-pages" -f "source[path]=/" >/dev/null; then
    echo "GitHub Pages switched on for $REPO, serving gh-pages"
else
    echo "(could not switch GitHub Pages on: Settings > Pages > Deploy from a branch, gh-pages)"
fi
echo "pushed the site for $V ($SHA) to gh-pages: $(gh api "repos/$REPO/pages" --jq .html_url 2>/dev/null || echo "see the repository's Pages settings")"
