#!/bin/sh
# HUSH — release packaging. Run via `make release` (which builds psp, autotest and host first).
set -e
cd "$(dirname "$0")/.."
REL=release
PKG=$REL/HUSH_by_Synth_Centric
rm -rf $PKG $REL/*.zip
mkdir -p $PKG/PSP/GAME/HUSH $PKG/docs $PKG/artwork $PKG/audio

echo "== tests"
./build/host/hush-tests --quick > $REL/host_tests.log 2>&1 && tail -1 $REL/host_tests.log

echo "== audio renders"
[ -f build/audio/05_Drift.wav ] || sh tools/render_previews.sh

echo "== manual"
if command -v pandoc >/dev/null && command -v node >/dev/null; then sh tools/docs/build_manual.sh || echo "PDF build failed (markdown manual still included)"; fi

echo "== assemble"
cp build/psp/EBOOT.PBP $PKG/PSP/GAME/HUSH/
cp README.md LICENSE $PKG/
cp docs/*.md $PKG/docs/; [ -f docs/HUSH_Manual.pdf ] && cp docs/HUSH_Manual.pdf $PKG/docs/
cp -r artwork/* $PKG/artwork/
for f in build/audio/*.wav; do cp "$f" "$PKG/audio/HUSH_$(basename "$f")"; done
cp build/audio/*.log $PKG/audio/ 2>/dev/null || true
cat > $PKG/INSTALL.txt <<'TXT'
HUSH - by Synth Centric

PSP (homebrew-enabled firmware):
  Copy the PSP folder in this package to the root of your Memory Stick, so that
  the game is at ms0:/PSP/GAME/HUSH/EBOOT.PBP (PSP Go: ef0:). Launch HUSH from
  Game > Memory Stick. Saves are written next to EBOOT.PBP (hush.sav, hush.bak).

PPSSPP:
  File > Load... and choose PSP/GAME/HUSH/EBOOT.PBP.

Manual: docs/HUSH_Manual.pdf (or .md). Audio: audio/*.wav (44.1 kHz, 16-bit).
TXT

echo "== archives"
git archive --format=zip --prefix=HUSH_Source/ -o $REL/HUSH_Source.zip HEAD
(cd $REL && zip -qr HUSH_Release_Package.zip HUSH_by_Synth_Centric)

echo "== checksums"
(cd $REL && sha256sum HUSH_Release_Package.zip HUSH_Source.zip HUSH_by_Synth_Centric/PSP/GAME/HUSH/EBOOT.PBP HUSH_by_Synth_Centric/audio/*.wav > checksums.txt)

echo "== report"
{
  echo "# HUSH release report"
  echo
  echo "- Date: $(date -u +%Y-%m-%dT%H:%MZ)"
  echo "- Source commit: $(git rev-parse --short HEAD)$(git diff --quiet || echo ' (working tree had uncommitted changes)')"
  echo "- Toolchain: $(psp-gcc --version | head -1)"
  echo "- EBOOT.PBP: $(stat -c %s build/psp/EBOOT.PBP) bytes"
  echo "- Host tests: $(tail -1 $REL/host_tests.log)"
  echo
  echo "## Contents"
  echo '```'
  (cd $REL && find HUSH_by_Synth_Centric -type f | sort | sed 's/^/  /')
  echo '```'
  echo
  echo "## Archive sizes"
  echo '```'
  (cd $REL && ls -l *.zip | awk '{print $5, $9}')
  echo '```'
  echo
  echo "## SHA-256"
  echo '```'
  cat $REL/checksums.txt
  echo '```'
  echo
  echo "Runtime validation: see docs/TESTING.md and docs/validation/ (PPSSPP). Physical PSP hardware: not tested."
} > $REL/RELEASE_REPORT.md
echo "release written to $REL/"
ls -la $REL
