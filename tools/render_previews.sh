#!/bin/sh
# HUSH — renders the five reference pieces with the shared engine.
# WAVs go to build/audio (large, not in git); MP3 previews to audio/preview.
set -e
R=./build/host/hush-render
OUT=build/audio
mkdir -p $OUT audio/preview
render() { name=$1; shift; echo "== $name: $*"; $R "$@" --output $OUT/$name.wav > $OUT/$name.log; tail -1 $OUT/$name.log; }
render 01_Civic_Centre     --seed 4817 --preset patrol  --district civic    --minutes 6  --altitude 0.55 --speed 0.4 --rain 0.3 --flight
render 02_The_Works        --seed 9113 --preset patrol  --district works    --minutes 6  --altitude 0.2  --speed 0.65 --canyon 0.7 --traffic 0.5 --darkness 0.65
render 03_The_Stacks       --seed 2146 --preset drift   --district stacks   --minutes 7  --altitude 0.45 --speed 0.3 --rain 0.7 --landmark choir --prox 0.35
render 04_The_Undertow     --seed 333  --preset reservoir --district undertow --minutes 7 --altitude 0.15 --speed 0.1 --fog 0.9 --landmark reservoir --prox 0.6
render 05_Drift            --seed 2146 --preset drift   --district stacks   --minutes 20 --rain 0.4 --flight
if command -v ffmpeg >/dev/null; then
  for f in $OUT/*.wav; do b=$(basename $f .wav); ffmpeg -loglevel error -y -i $f -codec:a libmp3lame -q:a 7 audio/preview/HUSH_$b.mp3; done
fi
