/* HUSH — public interface of the generative music engine.
 *
 * Shared verbatim by the PSP game and the host renderer (tools/audio-renderer).
 *
 * Threading model: one producer (game/UI thread) writes MusicParams and
 * MusicEnv through hush_music_set_params / hush_music_set_env, which publish
 * into a sequence-locked mailbox. The audio thread picks the latest complete
 * copy at the start of every control block. No locks, no allocation, no I/O
 * on the audio path. MusicViz is written by the audio thread and read
 * tear-tolerantly by the UI (it is display-only). */
#ifndef HUSH_MUSIC_H
#define HUSH_MUSIC_H

#include <stdint.h>

enum { VOL_DRONE = 0, VOL_HARMONY, VOL_BASS, VOL_PERC, VOL_FRAG, VOL_TEXTURE, VOL_COUNT };
enum { LAYER_ATMOS = 0, LAYER_HARMONY, LAYER_PULSE, LAYER_PERC, LAYER_FRAG, LAYER_DECAY, LAYER_COUNT };
enum { DISTRICT_CIVIC = 0, DISTRICT_WORKS, DISTRICT_STACKS, DISTRICT_UNDERTOW, DISTRICT_COUNT };
enum { LM_CHOIR = 0, LM_STATION9, LM_ARCHIVE, LM_RESERVOIR, LM_EXCHANGE, LM_COUNT };
enum { MODE_AEOLIAN = 0, MODE_DORIAN, MODE_PHRYGIAN, MODE_LYDIAN, MODE_HARMMINOR, MODE_MINPENT, MODE_SUSPENDED, MODE_LOCRIAN, MODE_COUNT };
#define MODE_AUTO (-1)   /* follow the current district's tonal character */

enum { SEC_EMERGE = 0, SEC_ACCUMULATE, SEC_SUSTAIN, SEC_FRAGMENT, SEC_DISSOLVE, SEC_SILENCE, SEC_RENEW, SEC_COUNT };

typedef struct {
    /* core */
    float master;            /* 0..1 */
    float vol[VOL_COUNT];    /* 0..1 per layer */
    float density;           /* 0..1 event density */
    float darkness;          /* 0..1 timbre/mode/register darkness */
    float motion;            /* 0..1 modulation and evolution rate */
    float tempo;             /* BPM, 50..140 */
    int key;                 /* 0..11, 0 = C */
    int mode;                /* MODE_* or MODE_AUTO */
    float filter;            /* 0..1 global brightness */
    float delay;             /* 0..1 delay send */
    float reverb;            /* 0..1 ambience send */
    uint32_t seed;           /* composition seed */
    /* advanced */
    int pad_wave;            /* W_* wave for harmony pads (-1 = district default) */
    float detune;            /* 0..1 oscillator detune */
    int drone_root;          /* semitone offset of the drone root, -7..7 */
    float cutoff;            /* 0..1 base filter cutoff bias */
    float lfo_rate;          /* 0..1 */
    float complexity;        /* 0..1 harmonic complexity */
    float rhythm_prob;       /* 0..1 */
    float seq_variation;     /* 0..1 */
    float melody_prob;       /* 0..1 */
    float delay_fb;          /* 0..1 */
    float mod_depth;         /* 0..1 */
} MusicParams;

typedef struct {
    int active;              /* 0 = no world (Signal Console mode): neutral inputs */
    int district;            /* DISTRICT_* */
    float altitude;          /* 0 (street) .. 1 (above the skyline) */
    float speed;             /* 0 (hover) .. 1 (full boost) */
    float rain;              /* 0..1 */
    float fog;               /* 0..1 */
    float pursuit;           /* 0..1 urgency */
    float traffic;           /* 0..1 nearby traffic density */
    float canyon;            /* 0..1 how enclosed by buildings */
    float scan;              /* 0..1 scanner engaged */
    float signal;            /* 0..1 strength of the mission transmission */
    int landmark;            /* LM_* or -1 */
    float landmark_prox;     /* 0..1 */
} MusicEnv;

#define VIZ_SCOPE 256
#define VIZ_EVENTS 24
typedef struct { uint8_t layer; int8_t note; uint8_t vel; uint8_t pad; uint32_t step; } VizEvent;
typedef struct {
    float scopeL[VIZ_SCOPE], scopeR[VIZ_SCOPE];
    float peakL, peakR, rmsL, rmsR;
    float layer_level[LAYER_COUNT];
    float layer_gain[LAYER_COUNT];
    uint32_t voice_mask;     /* bit per tone voice */
    uint32_t drum_mask;
    int voices_active;
    int section, section_bars_left;
    int bar, step;
    int key_center, mode;
    char chord_name[16];
    float tempo, erosion;
    VizEvent events[VIZ_EVENTS];
    int event_head;
    uint32_t event_count, event_hash;
    uint32_t seed;
} MusicViz;

typedef struct HushMusic HushMusic;

/* The engine is a single large static object (~1.4 MB with delay lines). */
HushMusic *hush_music_instance(void);
void hush_music_init(HushMusic *m, float sample_rate, const MusicParams *p);
void hush_music_set_params(HushMusic *m, const MusicParams *p);
void hush_music_set_env(HushMusic *m, const MusicEnv *e);
/* Re-seed: the new composition takes over at the next bar via crossfade. */
void hush_music_reseed(HushMusic *m, uint32_t seed);
/* Musical punctuation from gameplay (discoveries, mission complete). */
enum { MEV_DISCOVERY = 1, MEV_MISSION_COMPLETE, MEV_SIGNAL_LOCK, MEV_ALERT, MEV_TRANSMISSION };
void hush_music_event(HushMusic *m, int type, int arg);
/* Render interleaved stereo float frames. */
void hush_music_render(HushMusic *m, float *out, int frames);
/* Render interleaved stereo int16 frames (with TPDF-free rounding; output is soft-limited). */
void hush_music_render_s16(HushMusic *m, int16_t *out, int frames);
const MusicViz *hush_music_viz(HushMusic *m);
uint32_t hush_music_event_hash(HushMusic *m);
uint32_t hush_music_event_count(HushMusic *m);
/* lighter DSP (no chorus, no noise textures) for when the platform's audio CPU budget is tight */
void hush_music_set_economy(HushMusic *m, int on);
/* motifs of the hidden transmissions the player has collected (bit per id); they recur in the fragments layer */
void hush_music_set_collected(HushMusic *m, uint32_t mask);

/* Presets */
enum { PRESET_PATROL = 0, PRESET_PURSUIT, PRESET_DRIFT, PRESET_ARCHIVE, PRESET_RESERVOIR, PRESET_CHOIR, PRESET_NULLZONE, PRESET_BUILTIN_COUNT };
extern const char *const hush_preset_names[PRESET_BUILTIN_COUNT];
void hush_preset_get(int id, MusicParams *out);
int hush_preset_find(const char *name);   /* case-insensitive, -1 if none */

extern const char *const hush_mode_names[MODE_COUNT];
extern const char *const hush_section_names[SEC_COUNT];
extern const char *const hush_district_names[DISTRICT_COUNT];
extern const char *const hush_landmark_names[LM_COUNT];
extern const char *const hush_layer_names[LAYER_COUNT];
extern const char *const hush_note_names[12];

/* Diagnostics for tools/tests */
typedef struct {
    uint32_t layer_events[LAYER_COUNT];
    uint32_t section_changes, chord_changes, key_changes, motif_new, motif_recall, motif_variation;
    uint32_t max_voices, voice_steals;
    uint32_t bars;
    uint32_t section_hist[SEC_COUNT];
} MusicStats;
void hush_music_stats(HushMusic *m, MusicStats *out);
/* optional diagnostic hook called for every scheduled musical event */
extern void (*hush_event_trace)(uint32_t step, int layer, int note, int vel);

#endif
