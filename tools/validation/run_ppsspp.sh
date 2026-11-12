#!/bin/sh
# HUSH — runtime validation in PPSSPP's headless runner (software renderer).
# Requires PPSSPPHeadless (set PPSSPP=/path/to/PPSSPPHeadless). Outputs to build/validation/.
set -e
PPSSPP=${PPSSPP:-/opt/emu/ppsspp/build-headless/PPSSPPHeadless}
OUT=$(pwd)/build/validation
rm -rf $OUT && mkdir -p $OUT/script $OUT/campaign $OUT/release
cp build/autotest/EBOOT.PBP $OUT/script/
cp build/autotest-campaign/EBOOT.PBP $OUT/campaign/
cp build/psp/EBOOT.PBP $OUT/release/
echo "== scripted tour (title, drift x4 districts, console, cameras, mission 6, save/load)"
$PPSSPP --graphics=software --timeout-emulated=400 $OUT/script/EBOOT.PBP > $OUT/script.log 2>&1 || true
grep -E "AUTOTEST|HUSH:" $OUT/script.log
echo "== full campaign (all 12 missions, autopiloted)"
$PPSSPP --graphics=software --timeout-emulated=2400 $OUT/campaign/EBOOT.PBP > $OUT/campaign.log 2>&1 || true
grep -E "mission|campaign|clean" $OUT/campaign.log
echo "== release build driven only by injected button presses, with audio capture"
P="3.0:0x4000,4.0:0x40,4.5:0x4000,5.0:0x40,5.3:0x40,5.6:0x40,5.9:0x40,6.2:0x40,6.8:0x4000,8.0:0x10:4,10:0x200:3,14:0x8,15.5:0x40,16:0x4000,18:0x2000,19:0x1"
S="2.5:$OUT/release/r0_title.png,3.8:$OUT/release/r1_menu.png,6.5:$OUT/release/r2_drift_setup.png,13:$OUT/release/r3_flying.png,15:$OUT/release/r4_pause.png,17.5:$OUT/release/r5_console.png,21:$OUT/release/r6_cockpit.png"
$PPSSPP --graphics=software --timeout-emulated=24 --press="$P" --screenshot-at="$S" --dump-audio=$OUT/release/audio.wav $OUT/release/EBOOT.PBP > $OUT/release.log 2>&1 || true
grep -E "HUSH:|Screenshot" $OUT/release.log
