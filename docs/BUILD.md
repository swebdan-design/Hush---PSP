# Building HUSH

## Requirements
| Purpose | Tool |
|---|---|
| PSP executable | PSPDEV toolchain (psp-gcc 15.2, PSPSDK) at `/usr/local/pspdev` |
| Host renderer and tests | any C11 compiler (`cc`), make |
| Regenerating fonts/billboards/artwork (optional) | Python 3 with Pillow and NumPy; DejaVu and Inter fonts |
| MP3 previews (optional) | ffmpeg with libmp3lame |
| Runtime validation (optional) | PPSSPPHeadless |
| PDF manual (optional) | pandoc + Node.js with Playwright/Chromium |

### Installing PSPDEV (Linux x86-64)
The prebuilt toolchain is published as a GitHub release asset:
```bash
curl -L -o pspdev.tar.gz https://github.com/pspdev/pspdev/releases/download/latest/pspdev-ubuntu-latest-x86_64.tar.gz
```
```bash
sudo tar xzf pspdev.tar.gz -C /usr/local
```
```bash
export PSPDEV=/usr/local/pspdev; export PATH=$PATH:$PSPDEV/bin
```
(The tarball contains a top-level `pspdev/` directory.) The Makefile sets `PSPDEV`/`PATH` itself if the toolchain lives at the default path.
The build used for this release reported: `psp-gcc (GCC) 15.2.0`, pspsdk commit `837f38d` (2026-11-11).

## Targets
| Command | Output |
|---|---|
| `make` / `make psp` | `build/psp/EBOOT.PBP` (release game) |
| `make host` | `build/host/hush-render`, `build/host/hush-tests` |
| `make test` | runs the host suite (quick: ~2 minutes) |
| `make test-full` | adds the 30-minute audio stability/repetition test |
| `make autotest` | `build/autotest/EBOOT.PBP` — scripted tour for PPSSPP |
| `make autotest-campaign` | `build/autotest-campaign/EBOOT.PBP` — plays all 12 missions |
| `make assets` | regenerates `src/assets/generated/*.h` (committed, so not needed for a normal build) |
| `make release` | everything below, packaged |
| `make clean` | removes `build/` |

The PSP build compiles every file in `src/audio`, `src/game`, `src/render`, `src/ui` and `src/platform/psp`
(`tools/psp.mk`, using PSPSDK's `build.mak`). Flags: `-O2 -G0 -ffast-math -fsingle-precision-constant`, except the
deterministic modules (composer, theory, profiles, music_engine, world, traffic) which use
`-fno-fast-math -ffp-contract=off` so that their decisions match the host renderer bit-for-bit.

## The host renderer
```bash
./build/host/hush-render --help
```
Key options: `--seed N`, `--preset patrol|pursuit|drift|archive|reservoir|choir|null_zone`, `--minutes M` or `--seconds S`,
`--output FILE.wav`, `--rate 44100|22050`, `--district civic|works|stacks|undertow`, `--altitude/--speed/--rain/--fog/--pursuit/--traffic/--canyon X`,
`--landmark choir|station9|archive|reservoir|exchange --prox X`, `--flight` (simulated deterministic flight), `--console`,
`--tempo --key --mode --density --darkness --motion --complexity --melody --rhythm`, `--vol LAYER=X`, `--events`, `--trace`, `--quiet`.

It prints the section timeline, per-minute peak/RMS, event counts per layer, structural statistics, voice usage,
clipping/NaN counts, render speed and a final `SUMMARY` line with the event fingerprint.

## Reference audio renders
```bash
sh tools/render_previews.sh
```
renders the five reference pieces to `build/audio/*.wav` (+ logs) and MP3 previews to `audio/preview/`.

## Runtime validation in PPSSPP
```bash
make psp autotest autotest-campaign
```
```bash
PPSSPP=/path/to/PPSSPPHeadless sh tools/validation/run_ppsspp.sh
```
Building PPSSPPHeadless on Ubuntu 24.04: clone `hrydgard/ppsspp` with `git submodule update --init --recursive`,
install `libgl-dev libglu1-mesa-dev libegl-dev libfreetype-dev libfontconfig-dev`, build and install SDL3 and SDL3_ttf
(release-3.2.x) from source, then `cmake .. -DHEADLESS=ON -DUNITTEST=OFF -DCMAKE_BUILD_TYPE=Release && make PPSSPPHeadless`.

## Release package
```bash
make release
```
Produces in `release/`:
- `HUSH_by_Synth_Centric/PSP/GAME/HUSH/EBOOT.PBP` — copy the `PSP` folder to the root of the Memory Stick.
- `HUSH_by_Synth_Centric/` also holds the manual, docs, artwork and the five WAV renders.
- `HUSH_Release_Package.zip`, `HUSH_Source.zip` (git archive of HEAD), `checksums.txt` (SHA-256), `RELEASE_REPORT.md`.

## Installing
- **PSP with custom firmware**: copy `PSP/GAME/HUSH/` to `ms0:/PSP/GAME/HUSH/` (PSP Go: `ef0:`). Launch from the XMB.
  Saves are written beside the EBOOT as `hush.sav` (+ `hush.bak`).
- **PPSSPP**: open `EBOOT.PBP` directly, or place the folder under PPSSPP's `memstick/PSP/GAME/`.
