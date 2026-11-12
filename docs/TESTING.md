# HUSH — Testing Report

Everything below was actually run during development on 2026-11-12. Logs are in `docs/validation/`.
**What was not possible:** testing on a physical PSP. All runtime results come from PPSSPP; PPSSPP's timing is not
cycle-accurate, so its frame-rate and CPU figures are *indicative only*.

## Environment
- Ubuntu 24.04 x86-64, PSPDEV prebuilt toolchain (psp-gcc 15.2.0, pspsdk 837f38d).
- PPSSPPHeadless built from hrydgard/ppsspp master (commit 1a6d0c1), `--graphics=software`.

## 1. Build tests
| Check | Result |
|---|---|
| `make psp` compiles with `-Wall`, no warnings (format warnings disabled for `uint32_t`=`long` on PSP) | pass |
| `EBOOT.PBP` produced with PARAM.SFO, ICON0, PIC1 (≈ 0.98 MB) | pass |
| `make host` (renderer + tests) compiles with `-Wall -Wextra`, no warnings | pass |
| Repeatable from clean (`rm -rf build && make`) | pass |

## 2. Host test-suite (`make test`: 187 checks; `make test-full` adds the 30-minute run — `docs/validation/host_tests_*.log`)
- **Handedness** (added after a player reported mirrored strafe/turn): stick-right moves toward the camera's screen-right,
  R turns the nose toward screen-right, banking matches, `craft_right == forward × up`.
- **Crowd**: no pedestrian inside geometry after 60 s, the crowd gathers around the player, umbrellas in rain, flight from a
  low Moth, nobody in the flooded Undertow. **Street lanes** are covered by the lane-clearance test.
- **Signal hunting**: every district has 8 valid signals (free sphere, clear shaft, spacing); the autopilot, steered only by
  the hunt's bearing/strength outputs, captures **all 32**; strength rises when facing the source; `signals_found` survives a
  save round trip and an old save without the field loads with 0. Collected motifs: same mask → identical audio, empty mask →
  bit-identical to the pre-feature fingerprints.
- **City reactions**: a street car stops for a Moth hovering 15 m ahead in its lane, honks, and drives on once it leaves;
  a flying car swerves > 4 m around a Moth in its path; pedestrians across the street stop to watch and some take pictures.
- **Saves**: round trip of every field; corruption, truncation, wrong magic and missing file are rejected and fall back to defaults.
- **World**: deterministic generation for all four districts; spawn points free; every traffic/mission lane sampled every 4 m is
  collision-free (this test found and fixed three real routing conflicts during development); each district has a pad and a landmark.
- **Flight**: 30 fps and 60 fps produce the same path (< 0.5 m); craft settles into a hover and holds altitude; collisions stop
  the craft at walls and damage the hull.
- **Env bridge**: landmark proximity, weather and altitude mapping.
- **Missions**: the autopilot completes **all 12 missions** using controller-equivalent inputs through the real flight,
  collision and mission code; hull-loss and time-limit failure paths verified.
- **Audio**: bit-identical output for the same seed; different seeds differ; identical event streams at 44.1 and 22.05 kHz;
  no NaN; peak/RMS in range; different districts/inputs change the music; all 7 presets differ pairwise and stay unclipped;
  PURSUIT busier than DRIFT; console mode (no world) still makes music; the Silent Exchange lowers level and event count;
  fast flight produces ≥ 1.3× more percussion than hovering.
- **30-minute run** (DRIFT, district and weather changes at 10 and 20 minutes): no NaN, no clipping, 27 section changes,
  ≥ 4 section types, 56 chord changes, max 22 voices (limit 33), 0 duplicated 8-bar windows out of 58.

## 3. Runtime tests in PPSSPP (`sh tools/validation/run_ppsspp.sh`)
### 3a. Scripted tour (`build/autotest/EBOOT.PBP`) — `docs/validation/ppsspp_scripted_tour.log`
Boot → title (attract flight) → main menu → Drift (Stacks, heavy rain) with analogue flight → Signal Console overlay
(percussion lowered from 0.80 to 0.78 by a held D-pad press) → shape and seeds pages → cockpit and cinematic cameras →
Patrol mission 6 (autopiloted, **completed in 46.6 s**, autosave) → **save reloaded from the Memory Stick** and verified →
Drift in The Works, The Undertow (fog), Civic Centre (twilight) → standalone console → briefing → **clean exit**.
Screenshots of every step are in `artwork/screenshots/`. 2732 audio blocks rendered, **0 late**.

### 3a-bis. Street level and story (same run)
The Moth is placed on a Stacks street in heavy rain: chase, cockpit and cinematic screenshots show shopfronts, neon signs,
lane markings, street traffic, pedestrians with lit umbrellas and wet-street reflections (`artwork/screenshots/HUSH_14..16`).
Then the Operator's Log, prologue and Case File screens (`HUSH_17..19`); the prologue sets `story_flags=1` in the save.
A display-list overflow found here (crowd geometry ~460 KB) was fixed by capping rendered figures.

### 3a-ter. Signal hunting and city reactions (same run)
Hovering 18 m above a Stacks street: **27 pedestrians stop to watch**, crowd murmur at full level (`HUSH_20`). Signal hunt
from 45 m with the source behind the Moth: trace strength 0.31, bearing −3.14 rad, chevron shown (`HUSH_21`); flown into
range, the transmission is **captured in 1.6 s**, autosaved, music event fired (`HUSH_22`); the save reload later confirms
`signals 0x00010000` persisted; the Case File shows the intercept text (`HUSH_23`). 0 late audio blocks for the whole run.

### 3b. Full campaign (`build/autotest-campaign/EBOOT.PBP`) — `docs/validation/ppsspp_campaign.log`
All 12 missions played in order in the PSP build, autopiloted and fast-forwarded ×8 in simulation:
**12/12 complete**, `missions_done=0xfff`, all four districts unlocked, all five landmarks discovered, hull upgrades applied
(100 → 140 → 180), clean exit.

### 3c. Release build driven only by injected buttons — `docs/validation/ppsspp_release_input.log`
Title → menu → Drift setup → begin → ascend and turn → pause → Signal Console → close → camera switch. All screens reached
as expected. Audio output captured with `--dump-audio`: continuous for 40 s, RMS −25 to −27 dBFS after the intro, peak
≈ −12 dBFS, largest sample-to-sample step 0.061 (no discontinuities).

### 3d. Cross-platform determinism
| Check | Host x86-64 | PSP build in PPSSPP |
|---|---|---|
| PATROL seed 4817, 60 s, 22.05 kHz: events / fingerprint | 105 / `713db168` | 105 / `713db168` |
| Full event trace (step, layer, note, velocity) | — | identical (`diff` empty) |
| World fingerprints (Civic, Works, Stacks, Undertow) | c82d5275 41ee45ef 906ea7ce 1b66b71d | identical |

This check found a real bug: two random draws inside one function-call argument list were evaluated in different orders by
GCC on MIPS and x86-64, so arpeggio velocities differed. Six such expressions (three in the composer, three in world
generation) were rewritten; a rule was added to CLAUDE.md.

## 4. Performance observations (PPSSPP emulated clock — not hardware measurements)
| Scene | fps | CPU ms/frame (update+list build) | GE wait ms | triangles | audio thread load |
|---|---|---|---|---|---|
| Title (Stacks, attract) | 29.9 | 0.6 | 1.4 | 9 320 | 16 % |
| Drift Stacks, heavy rain | 30.0 | 0.7 | 0.1 | 2 434 | 17 % |
| Patrol (Stacks rooftops) | 30.0 | 1.0 | 1.9–6.5 | 13 654 | 22 % |
| Drift Civic, twilight | 29.8 | 0.9 | 0.6 | 6 238 | 25 % |

District generation 1–3 ms, mesh build 22–61 ms; offline render of 60 s of music took 10.7 s of emulated time
(≈ 18 % of real time, consistent with the measured thread load). Memory: 10.5 MB static of ~24 MB user RAM.
These numbers suggest comfortable headroom, but PPSSPP does not model Allegrex/GE cycle costs; **real-hardware frame rate is unverified**.

## 5. Visual checks (screenshots inspected)
Title with attract-mode flight, menus, Drift in all four districts, rain, fog, twilight, chase/cockpit/cinematic cameras,
HUD (compass, radar, speed/altitude, signal meter, objective marker with distance, transmissions), Signal Console pages
with live oscilloscope/meters/voice activity, briefing and debrief. Issues found and fixed: cockpit panel labels overlapped.

## 5b. First physical-hardware report (PSP-3000) and fixes
Reported by the player: (1) frame rate became jittery/choppy at times, (2) window-light textures on roofs, (3) textures
"dropping out": a blue ground and an invisible overhead railway; flying traffic and controls were good.
- (3) Cause: the PSP GE rejects a primitive outright when, after near-plane clipping, any vertex lies more than 2048 px from
  the viewport centre (PPSSPP models this rule in its software renderer, but the scenes it was tested with never showed it).
  77 m ground tiles under a low camera and the
  single-quad tops of the 320-600 m rail/highway decks hit this. Fixes: horizontal faces split into <= 20 m cells;
  `guardclip.h` clips risky triangles on the CPU against planes at 80 % of the guard band (ground, water, skyline, signs,
  and replacements for static geometry within 55 m); near plane raised to 1.5 m. Host tests reproduce the rejection with a
  model of the GE rule and show 0 of the clipped output rejected and no visible triangle lost (4000-triangle fuzz).
- (2) Cause: roofs and sidewalks used the concrete facade texture, which contains windows. Fix: dedicated bitumen-roof and
  paving textures.
- (1) Mitigations: adaptive detail governor (draw distance, light/sign/crowd/rain budgets, wet reflections) driven by the
  measured frame time; frustum culling and a screen-size cap for glow sprites; smaller sign glows; audio economy mode when
  the audio thread's measured load exceeds 55 %; Options › Detail (Auto/High/Medium/Low); the performance overlay now shows
  real frame ms, clipped triangles, audio load and detail level. **These need re-testing on the PSP-3000.**

## 5c. Second hardware report (PSP-3000): crash entering the prologue and Drift, audio dropouts on the menu
- Finding: the PSP FPU starts with invalid/divide-by-zero/overflow **traps enabled** (FCR31 `0x00000E00`, read at boot
  and logged). PPSSPP reports this value but never raises the exceptions, so a NaN or division by zero that is harmless
  in the emulator powers the console off. Fix: traps disabled and flush-to-zero enabled on the main and audio threads
  (FCR31 `0x01000000`); the audio determinism hash is unchanged (`713db168`).
- Not reproduced off-device: the whole PSP game was also built for x86 against stub SDK headers with the same traps
  enabled (plus slow-frame timing, display-list overflow, vertex and texture checks) and ran the full scripted tour clean;
  display-list peak is 427 KB of 1 MB; stack frames are < 3 KB.
- `hush_log.txt` now records the first stage at which an FPU flag is raised on hardware, a non-finite craft state is
  reset to spawn, and per-second audio load / late blocks are logged for the menu dropout report.
  Result on the PSP-3000: **no more crash**, but the opening sequence and the prologue ran at a crawl. Cause: the crash log
  itself, which opened/appended/closed the file on the Memory Stick for ~30 stage markers per frame during the first frames
  after a load or state change (and once a second otherwise) — tens of ms per write on hardware, near-free in PPSSPP.
  The log is now buffered in RAM and written only at loads, menus, pause and exit. The audio output block was doubled to
  2048 samples (46 ms) to ride out hitches; audio pops are otherwise awaiting the per-second audio-load numbers from the log.

## 6. Known issues and limitations
- Not tested on PSP hardware; the GE's handling of large near-plane triangles is mitigated by facade tessellation but unverified on device.
- PPSSPP software renderer only; hardware-accelerated backends were not exercised.
- Saves are plain files beside the EBOOT, not system savedata (they don't appear in the XMB savedata list).
- The ground/street plane uses 77 m tiles; on hardware a very low camera over open ground might drop a tile at the screen edge.
- The scripted tour reads "fps 30" in PPSSPP even under heavy load because the emulated GE is effectively free.
- The PDF manual requires pandoc and Chromium (via Node Playwright) at release time.
