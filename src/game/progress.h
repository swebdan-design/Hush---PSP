/* HUSH — persistent progress, settings and synthesizer state.
 *
 * Save file format (all little-endian, see docs/DESIGN.md "Saving"):
 *   offset 0  u32 magic 'HUSH' (0x48535548)
 *          4  u16 version
 *          6  u16 reserved (0)
 *          8  u32 payload length
 *         12  u32 CRC-32 (IEEE 802.3) of the payload
 *         16  payload (field-by-field, never a raw struct dump)
 * Loading validates magic, version, length and CRC, then clamps every field. */
#ifndef HUSH_PROGRESS_H
#define HUSH_PROGRESS_H

#include <stdint.h>
#include "../audio/music.h"

#define SAVE_MAGIC 0x48535548u
#define SAVE_VERSION 1
#define USER_PRESETS 4
#define SAVED_SEEDS 8
#define SAVE_MAX_BYTES 4096

enum { HUD_FULL = 0, HUD_MINIMAL, HUD_OFF, HUD_COUNT };
enum { CTL_FACE = 0, CTL_CLASSIC, CTL_COUNT };

typedef struct {
    uint32_t missions_done;      /* bit per mission */
    uint32_t rewards;            /* accumulated RW_* flags */
    uint8_t scanner_level, boost_level, hull_level, has_emp;
    uint8_t districts;           /* bit per district unlocked for Patrol */
    uint8_t landmarks;           /* bit per discovered sonic landmark */
    uint16_t waves;              /* bit per W_* waveform available in the console */
    uint16_t presets;            /* bit per built-in preset available */
    uint8_t paint, paints;       /* current scheme, unlocked bitmask */
    MusicParams music;           /* the Signal Console's current state */
    MusicParams user[USER_PRESETS];
    uint8_t user_used[USER_PRESETS];
    uint32_t seeds[SAVED_SEEDS];
    uint8_t n_seeds;
    /* options */
    uint8_t hud_mode, cam_mode, show_fps, rain_density, music_vol, sfx_vol, invert_lift;
    uint8_t drift_district, drift_weather, drift_tod, drift_preset;
    uint32_t play_seconds;
    uint8_t control_scheme;      /* CTL_FACE (default) or CTL_CLASSIC; appended in v1 saves, optional on load */
    uint8_t story_flags;         /* bit0: prologue seen. Appended, optional on load */
} Progress;

void progress_defaults(Progress *p);
/* Applies mission rewards; returns the new reward bits. */
uint32_t progress_reward(Progress *p, uint32_t rewards);
void progress_discover(Progress *p, int landmark);
int progress_mission_unlocked(const Progress *p, int mission);
int progress_next_mission(const Progress *p);

/* Serialisation. Returns bytes written, or 0 on failure. */
int progress_serialize(const Progress *p, uint8_t *buf, int cap);
/* Returns 1 on success; on failure *p is left at defaults and the error is described. */
int progress_deserialize(Progress *p, const uint8_t *buf, int len, const char **err);
uint32_t hush_crc32(const uint8_t *data, int len);

#endif
