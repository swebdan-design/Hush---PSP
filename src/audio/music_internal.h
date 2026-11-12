/* HUSH — internal state of the music engine (composer + renderer). Not a public API. */
#ifndef HUSH_MUSIC_INTERNAL_H
#define HUSH_MUSIC_INTERNAL_H

#include "music.h"
#include "synth.h"
#include "fx.h"
#include "theory.h"
#include "../core/rng.h"

/* Tone voice pools per layer (indices into voices[]) */
#define V_ATMOS0 0
#define V_ATMOS_N 4
#define V_HARM0 4
#define V_HARM_N 10
#define V_BASS 14
#define V_ARP0 15
#define V_ARP_N 3
#define V_FRAG0 18
#define V_FRAG_N 4
#define V_TEX0 22
#define V_TEX_N 3
#define MAX_VOICES 25
#define MAX_DRUMS 8
#define MAX_PENDING 48

enum { PS_GEOMETRIC = 0, PS_INDUSTRIAL, PS_BROKEN, PS_SPARSE, PS_COUNT };
enum { BS_STEADY = 0, BS_DRIVING, BS_DUB, BS_DEEP, BS_COUNT };
enum { FI_BELL = 0, FI_GLASS, FI_RADIO, FI_VOX, FI_COUNT };

typedef struct {
    const char *name;
    float tempo_mul;
    float perc_style[PS_COUNT];
    float bass_style[BS_COUNT];
    float frag_inst[FI_COUNT];
    int mode;                    /* tonal character used when params.mode == MODE_AUTO */
    uint8_t drone_wave, drone_wave2, pad_wave, arp_wave;
    float drone_cut, pad_cut, arp_cut;
    float rev_size, rev_decay, rev_damp;
    float delay_steps;           /* delay time in 16th steps */
    float bias[LAYER_COUNT];     /* per-layer energy bias */
    float stab_prob, metal, swing, erosion, hum;
} DistrictProfile;

/* Continuous profile values blended by district weights */
typedef struct {
    float tempo_mul, drone_cut, pad_cut, arp_cut, rev_size, rev_decay, rev_damp, delay_steps;
    float bias[LAYER_COUNT];
    float stab_prob, metal, swing, erosion, hum;
} ProfileBlend;

typedef struct {
    int8_t deg[8];      /* scale degrees (relative to tonic, may exceed one octave) */
    uint8_t dur[8];     /* steps */
    uint8_t vel[8];     /* 0..127 */
    uint8_t n;
    uint8_t inst;
    uint8_t uses;
} Motif;

typedef struct {
    float vel[16];      /* probability/velocity per step, 0 = never */
} PercLane;

typedef struct {
    uint8_t on[16];
    int8_t deg[16];     /* relative to chord root, scale steps */
    int8_t oct[16];
    uint8_t len[16];
    uint8_t slide[16];
    uint8_t accent[16];
} BassPattern;

enum { PEV_NOTE = 0, PEV_DRUM, PEV_RELEASE, PEV_LEGATO };
typedef struct {
    int32_t delay;      /* samples until it fires */
    uint8_t type, layer, voice, kind;
    int8_t note;
    float vel, pan, aux1, aux2;
    Patch patch;        /* for PEV_NOTE */
    int32_t gate;       /* samples until auto-release, 0 = held */
} Pending;

typedef struct {
    /* random streams, one per musical concern */
    Rng r_struct, r_harm, r_pulse, r_perc, r_frag, r_decay, r_atmos, r_misc;
    uint32_t seed, pending_seed;
    int reseed_pending;

    /* clock */
    double sample_clock;          /* host diagnostics only */
    float samples_per_step;
    float step_countdown;         /* samples until next step */
    uint32_t step;                /* absolute step counter */
    int step_in_bar, bar;
    float bpm;

    /* smoothed per-step controls (deterministic, rate independent) */
    MusicParams P;                /* target params (latest snapshot) */
    MusicEnv E;                   /* latest env snapshot */
    float s_alt, s_speed, s_speed_slow, s_rain, s_pursuit, s_traffic, s_canyon, s_signal, s_scan, s_fog;
    float s_district[DISTRICT_COUNT];
    float s_landmark[LM_COUNT];
    float s_density, s_dark, s_motion, s_complex, s_rhythm, s_melody, s_seqvar;
    ProfileBlend pb;
    int district_major;

    /* structure */
    int section, section_bars_left, section_len;
    float sect_mult[LAYER_COUNT];       /* target */
    float energy[LAYER_COUNT];          /* combined, per step */

    /* harmony */
    int key_offset, key_offset_pending, mode_cur, mode_pending, mode_change_pending;
    Chord chord;
    int chord_bars_left;
    int voicing[VOICING_N];
    int pad_voice[VOICING_N];
    int drone_notes[2];
    int drone_bars;
    int drone_retrigger;
    int hum_district, hum_voice;

    /* pulse */
    BassPattern bass, bass_mem[4];
    int bass_mem_n;
    int bass_style;
    uint8_t arp_k, arp_rot, arp_order;
    int arp_idx;
    int bass_last_note;

    /* percussion */
    int perc_style;
    PercLane lanes[DR_COUNT];
    int displace;                 /* broken-beat displacement in steps */
    int halftime;

    /* fragments */
    Motif motifs[8];
    int n_motifs, motif_next;
    int phrase_active, phrase_note, phrase_rest_bars;
    int phrase_step_left;
    Motif phrase;
    int archive_reps;

    /* decay */
    float erosion, erosion_target;
    float drift[LAYER_COUNT], drift_target[LAYER_COUNT];

    /* event punctuation from gameplay */
    int punct_type, punct_arg;

    /* stats + fingerprint */
    MusicStats stats;
    uint32_t event_hash, event_count;
} Composer;

struct HushMusic {
    float sr;
    /* mailbox (seqlock) written by the game thread */
    volatile uint32_t mb_seq;
    MusicParams mb_params;
    MusicEnv mb_env;
    volatile uint32_t mb_reseed;      /* 0 = none */
    volatile uint32_t mb_reseed_seq;
    uint32_t mb_reseed_seen;
    volatile int mb_event_type, mb_event_arg;
    volatile uint32_t mb_event_seq;
    uint32_t mb_event_seen;

    Composer c;

    Voice voices[MAX_VOICES];
    int32_t gate[MAX_VOICES];
    DrumVoice drums[MAX_DRUMS];
    int drum_rr;
    Pending pend[MAX_PENDING];
    int n_pend;

    /* smoothed audio-rate parameters (block rate) */
    float g_layer[LAYER_COUNT];
    float g_master, g_delay, g_reverb, g_cut, g_radio, g_hiss, g_chorus;

    /* buses */
    float padL[HUSH_CTRL], padR[HUSH_CTRL], mainL[HUSH_CTRL], mainR[HUSH_CTRL];
    float radL[HUSH_CTRL], radR[HUSH_CTRL];
    float sdL[HUSH_CTRL], sdR[HUSH_CTRL], srv[HUSH_CTRL];

    DubDelay delay;
    Reverb reverb;
    Chorus chorus;
    Radio radio;
    Master master;
    float rev_size_applied, rev_decay_applied, rev_damp_applied;
    int rev_update_ctr;

    volatile int economy;             /* set by the platform when the audio CPU budget is tight */
    MusicViz viz;
    float rms_accL, rms_accR;
    int rms_n;
    int viz_scope_div;
};

/* profiles.c */
extern const DistrictProfile hush_profiles[DISTRICT_COUNT];
void profile_blend(const float *w, ProfileBlend *out);
void patch_drone(Patch *p, const Composer *c, int variant);
void patch_pad(Patch *p, const Composer *c, int vox);
void patch_stab(Patch *p, const Composer *c);
void patch_bass(Patch *p, const Composer *c, int style);
void patch_arp(Patch *p, const Composer *c);
void patch_frag(Patch *p, const Composer *c, int inst);
void patch_texture(Patch *p, const Composer *c, int which);
void patch_sub(Patch *p, const Composer *c);
const Motif *landmark_motif(int lm);

/* composer.c */
void composer_init(HushMusic *m, uint32_t seed);
void composer_step(HushMusic *m);
float composer_samples_per_step(HushMusic *m);

/* music_engine.c (used by composer) */
void eng_note(HushMusic *m, int layer, int voice, const Patch *p, int note, float vel, float pan, float gate_steps, float delay_frac);
void eng_legato(HushMusic *m, int voice, int note, float gate_steps);
void eng_release(HushMusic *m, int voice, float delay_frac);
void eng_drum(HushMusic *m, int kind, float vel, float pan, float decay, float tone, float delay_frac);
int eng_alloc(HushMusic *m, int first, int count);
void eng_log(HushMusic *m, int layer, int note, float vel);

#endif
