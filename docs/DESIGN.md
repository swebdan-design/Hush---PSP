# HUSH — Design and Technical Architecture

## Pillars
1. **A bootable, stable PSP game** first; everything else second.
2. **Flight that feels good slowly**: hovering is the default state, not a failure state.
3. **The city as an instrument**: every system that moves (craft, traffic, weather, missions) feeds the composer.
4. **Restraint**: melancholy, sparse, investigation over combat, silence as a musical state.

## Code architecture
```
src/core        math, PCG32, FNV                         ┐
src/audio       DSP, voices, FX, theory, composer,      │ platform independent,
                engine, presets, SFX                      │ compiled into the PSP game,
src/game        world gen + collision, flight, cameras,   │ the host renderer and the
                traffic, weather, missions, saves,        │ host test-suite
                env bridge, autopilot                    ┘
src/render      GU renderer: textures, chunked meshes, lights, sky, effects, Moth   ┐ PSP only
src/ui          app state, gameplay session, HUD, menus, Signal Console            │
src/platform/psp main loop, GU wrapper, audio thread, input, save I/O              ┘
```
The main loop (30 Hz, vblank-locked) updates the current state, renders the 3D scene (if any) and overlays the 2D UI.
The audio thread is independent and higher priority.

## World
- Districts span 1240 × 1240 m (`WORLD_HALF` 620 m), ceiling 460 m. One district is resident at a time; switching shows a
  loading card while it is generated (1–3 ms in PPSSPP) and tessellated (20–60 ms).
- Generation: explicit landmarks, pads, mission buildings and routes are placed first and **reserve** their surroundings; a
  street grid is then filled with lots subdivided into setback towers whose height/texture distributions are per district.
  Seeds are fixed, so every player gets the same city, and mission geometry never moves. The generated city is identical on
  PSP and host (quantised geometry fingerprint compared in PPSSPP).
- Collision: AABBs in a 32 × 32 uniform grid plus analytic cylinders (chimneys, spires, pylons, hollow cooling towers);
  sphere push-out with sliding response; impact damage above 9 m/s.
- Rendering: 8 × 8 chunks, per-texture batches, baked vertex lighting with district colour grading, frustum + fog-distance
  culling, hardware fog, facades split into ≤ 34 m panels so near walls are never dropped by the GE's guard-band culling.
  A two-ring procedural skyline beyond the fog makes the city read as endless. Emissive lights (aviation, beacons, windows,
  traffic, engine glow) are camera-facing additive sprites with blink patterns; water mirrors them in the Undertow.
- Districts:

| District | Character | Landmark(s) | Special structures |
|---|---|---|---|
| Civic Centre | towering glass, cold light, advertising | Memory Archive | ring highway with ground traffic, surveillance spires, MSA platform |
| The Works | low industry, smog, red warnings | Station Nine | cooling towers (enterable), chimneys, pipe networks, steam-vent corridor |
| The Stacks | dense residential canyons, gardens | Choir Tower | hanging walkways, illegal rooftop transmitters, narrow traffic lanes |
| The Undertow | flooded lower city, heavy fog | Black Reservoir, Silent Exchange | water with reflections, ruins, pylons, the hollow exchange shaft |

## Street level (`world.c`, `crowd.c`, `render_world.c`, `render_crowd.c`)
- Every city block gets a raised sidewalk slab and is recorded as a pedestrian loop. Ground-floor facades get a 7 m band
  of lit shopfronts (a 512x64 atlas of four fronts). Lots are decorated by a *separate* random stream seeded from their
  position: hanging blade signs, flat shop signs, roof neon outlines and corner strips, so decor never moves a building.
- Billboards on tall towers in every district; neon tubes drawn as additive lines; dashed lane markings generated around a
  low camera; rain-slick streets reflect street-level lights (the reflection is placed where the eye→mirror ray meets the
  street, so it depth-tests correctly).
- Street traffic: candidate rectangles on the street grid are kept only if a car-sized sphere at every sample (centre and
  both lane offsets) is collision-free.
- Pedestrians: up to 200 per district walk the sidewalk loops 0.9 m from the kerb (buildings sit ≥ 1.5 m back), loiter at
  windows, raise umbrellas in rain and flee a Moth below 14 m within 18 m. Walkers far from the player are recycled onto
  blocks near the player, so the street is always populated. Rendering: articulated figures (≤ 40 detailed, ≤ 80 simple,
  ~100 KB of display list at most), glowing umbrella rims via the light batch.

## Handedness
The world is right-handed with Y up; looking along +Z, screen-right is −X. `craft_right()` returns forward × up, positive
yaw input turns right (yaw decreases), and the compass shows heading = −yaw. Host tests pin this down.

## Flight model (`src/game/flight.c`)
Fixed 60 Hz sub-steps (frame-rate independent; verified 30 fps and 60 fps paths agree within 0.5 m). Acceleration in the
craft frame (forward 30 m/s², ×2.1 boosting; strafe 21; vertical 18), linear drag (0.75/s horizontal, 2.2/s vertical, more
when idle, 3.6/4.5 when braking), soft speed limit (46 m/s, 82 boosting; raised by upgrades), yaw rate with smoothing, no
gravity drift while hovering, collision with bounce/slide and hull damage. Visual banking, pitch and nacelle tilt are derived
from inputs and velocity. Cameras: spring-smoothed chase camera with occlusion pull-in, cockpit with full instrument panel, and
a slow orbiting cinematic camera with letterbox.

## Missions (`src/game/missions.c`, `mission_data.c`)
Data-driven step sequences. Objective types: GOTO, SCAN (range, facing ≤ ~39°, line of sight, hold time), LOCATE (hidden source;
signal meter combines distance and facing), FOLLOW (25–150 m band; warnings, failure after 4 s too close or 8 s too far),
PURSUE (rubber-banded drone, EMP within 38 m, 2.5 s cooldown, time limit), COLLECT, RINGS (ordered, timed), LANDMARK,
DOCK (on pad, slow, 1.2 s), LISTEN (hover with no input for N seconds). Failure: hull destroyed, time limit, lost/alerted
contact → Retry/Abandon. Rewards are applied on completion and the game autosaves.

| # | Title | District | Core objective | Reward |
|---|---|---|---|---|
| 1 | First Watch | Civic | patrol route, scan a relay, land | — |
| 2 | Unlicensed Weather | Civic | locate a hidden carrier by signal strength | scanner range |
| 3 | Courier | Civic | follow without being seen; the Memory Archive | Stacks, ARCHIVE preset |
| 4 | Silence Allowance | Stacks | inspect three silent homes | hull |
| 5 | Pirate Choir | Stacks | scan transmitters; the Choir Tower | VOX wave, CHOIR preset |
| 6 | The Archived Woman | Stacks | pursue and EMP a retrieval drone | Works, boost, EMP |
| 7 | Hazard Corridor | Works | timed rings through steam vents | hull |
| 8 | Station Nine | Works | timing transmitters; Station Nine | HOLLOW wave |
| 9 | Low Frequency | Works | pursue a rogue drone | Undertow, scanner, GRIT wave |
| 10 | Black Reservoir | Undertow | locate a low drone in fog | GLASS wave, RESERVOIR preset |
| 11 | Recovery | Undertow | collect four data fragments | RAINWATER paint |
| 12 | The Silent Exchange | Undertow | descend, scan, and listen | NULL ZONE preset, ending |

Progression never locks essentials: Drift (all districts), the Signal Console and three presets are available from the start.

## Story delivery
Prologue (7 cards over a Civic flyover, first Patrol only, replayable), mission briefings and radio, debrief, an
Operator's Log entry after each mission (`story.c`), the Case File screen, and overheard chatter: hover low and slow
near pedestrians to catch a line from a pool that advances through five acts with the campaign, mixed with everyday satire.
Button names in story text are tokens (`[UP]`, `[SCAN]`…) substituted for the player's control scheme.

## Narrative
An unlicensed musical carrier on 61.7 MHz cannot be traced to a transmitter. The trail runs through a confiscated rain
recording, a citizen record without a citizen (Ines Vale), silent apartments still billed for silence, a decommissioned tower
that *receives* rather than sends, a drone that learned a dead commute, and finally the Silent Exchange, where the scanner
reports *SOURCE: NONE — CONDITIONS: RAIN, WIRE, RECORD, LISTENER*. The signal exists only as a meeting of conditions
(dependent origination); it is empty of a source without being nothing. Vale's note — "I was never only the record" — refuses
both the institution's equation of person and file and the opposite claim that she simply does not exist. Satire lives in the
signage: subscription silence, insured memories, licensed rainfall, registered melodies, predicted feelings.

## Saving (`src/game/progress.c`, `src/platform/psp/save_io.c`)
File `hush.sav` beside the EBOOT (`ms0:/PSP/GAME/HUSH/`). Header: magic `HUSH`, version, payload length, CRC-32; payload
written field by field in little-endian (never a raw struct). Contents: missions, rewards, upgrades, districts, landmarks,
unlocked waves/presets/paints, current synth state, four user presets, eight seed slots, options, Drift settings, flight time.
Writes go to `hush.tmp`, the old save becomes `hush.bak`, then rename. Loading validates everything, clamps every field, and
falls back to the backup and then to defaults (a damaged save never crashes the game).

## Memory budget (release ELF)
Code 0.85 MB; static data 9.7 MB, chiefly the city mesh pool (6.7 MB, 280k vertices), the music engine (0.7 MB) and
wavetables (0.65 MB), display list (1 MB). VRAM: double 32-bit framebuffers + 16-bit depth (1.33 MB) and textures (fonts,
facades, billboards, glow) copied in at boot.

## Scope decisions
- One district resident at a time instead of a seamless city: predictable memory and load cost; the skyline sells the scale.
- Lighting baked into vertices; no hardware lights.
- Engine at 22.05 kHz on the PSP to leave CPU for the game; the host renders at 44.1 kHz with the same events.
- Saves as plain files instead of the system savedata dialog: simpler and robust, but they do not appear in the XMB save list.
