/* HUSH — district sound profiles, instrument patches, presets and landmark motifs. */
#include "music_internal.h"
#include <string.h>
#include <ctype.h>

const char *const hush_section_names[SEC_COUNT] = { "EMERGENCE", "ACCUMULATION", "SUSTAIN", "FRAGMENTATION", "DISSOLUTION", "SILENCE", "RENEWAL" };
const char *const hush_district_names[DISTRICT_COUNT] = { "CIVIC CENTRE", "THE WORKS", "THE STACKS", "THE UNDERTOW" };
const char *const hush_landmark_names[LM_COUNT] = { "THE CHOIR TOWER", "STATION NINE", "THE MEMORY ARCHIVE", "THE BLACK RESERVOIR", "THE SILENT EXCHANGE" };
const char *const hush_layer_names[LAYER_COUNT] = { "ATMOSPHERE", "HARMONY", "PULSE", "PERCUSSION", "FRAGMENTS", "DECAY" };
const char *const hush_preset_names[PRESET_BUILTIN_COUNT] = { "PATROL", "PURSUIT", "DRIFT", "ARCHIVE", "RESERVOIR", "CHOIR", "NULL ZONE" };

/*                         name          tempo  perc styles (geo ind brk sparse)  bass (steady drive dub deep)   frag (bell glass radio vox) mode            drone waves              pad         arp       */
const DistrictProfile hush_profiles[DISTRICT_COUNT] = {
    { "CIVIC CENTRE", 1.00f, {3.0f, 0.3f, 0.6f, 0.4f}, {3.0f, 0.4f, 0.8f, 0.3f}, {1.0f, 2.0f, 0.8f, 0.3f}, MODE_AEOLIAN,
      W_SAW, W_PULSE, W_SAW, W_PULSE, 70.0f, 80.0f, 82.0f, 1.25f, 5.0f, 0.45f, 3.0f,
      {1.0f, 1.0f, 0.85f, 0.75f, 0.7f, 0.5f}, 0.25f, 0.15f, 0.04f, 0.10f, 0.25f },
    { "THE WORKS", 0.92f, {0.4f, 3.0f, 0.8f, 0.6f}, {0.3f, 3.0f, 0.4f, 0.6f}, {0.8f, 0.3f, 1.6f, 0.0f}, MODE_PHRYGIAN,
      W_GRIT, W_SQUARE, W_HOLLOW, W_SQUARE, 62.0f, 70.0f, 76.0f, 0.9f, 3.2f, 0.7f, 3.0f,
      {1.0f, 0.6f, 1.0f, 1.0f, 0.5f, 0.9f}, 0.08f, 0.9f, 0.0f, 0.25f, 0.8f },
    { "THE STACKS", 0.86f, {0.5f, 0.3f, 3.0f, 0.8f}, {0.4f, 0.2f, 3.0f, 0.5f}, {0.8f, 1.2f, 1.6f, 1.0f}, MODE_DORIAN,
      W_TRI, W_HOLLOW, W_ORGAN, W_TRI, 66.0f, 78.0f, 80.0f, 1.1f, 4.5f, 0.55f, 4.0f,
      {0.9f, 1.15f, 0.8f, 0.65f, 1.0f, 0.6f}, 0.35f, 0.15f, 0.10f, 0.15f, 0.35f },
    { "THE UNDERTOW", 0.72f, {0.2f, 0.6f, 0.2f, 3.0f}, {0.1f, 0.2f, 0.4f, 3.0f}, {1.2f, 1.4f, 0.8f, 0.6f}, MODE_SUSPENDED,
      W_HOLLOW, W_SINE, W_GLASS, W_GLASS, 56.0f, 66.0f, 74.0f, 1.9f, 9.0f, 0.65f, 6.0f,
      {1.1f, 0.55f, 0.35f, 0.25f, 0.55f, 0.85f}, 0.0f, 0.55f, 0.0f, 0.4f, 0.6f },
};

void profile_blend(const float *w, ProfileBlend *o) {
    memset(o, 0, sizeof(*o));
    float tot = 0;
    for (int d = 0; d < DISTRICT_COUNT; d++) tot += w[d];
    if (tot <= 0) tot = 1;
    for (int d = 0; d < DISTRICT_COUNT; d++) {
        float k = w[d] / tot;
        const DistrictProfile *p = &hush_profiles[d];
        o->tempo_mul += p->tempo_mul * k; o->drone_cut += p->drone_cut * k; o->pad_cut += p->pad_cut * k;
        o->arp_cut += p->arp_cut * k; o->rev_size += p->rev_size * k; o->rev_decay += p->rev_decay * k;
        o->rev_damp += p->rev_damp * k; o->delay_steps += p->delay_steps * k;
        for (int l = 0; l < LAYER_COUNT; l++) o->bias[l] += p->bias[l] * k;
        o->stab_prob += p->stab_prob * k; o->metal += p->metal * k; o->swing += p->swing * k;
        o->erosion += p->erosion * k; o->hum += p->hum * k;
    }
}

static void patch_defaults(Patch *p) {
    memset(p, 0, sizeof(*p));
    p->wave1 = W_SAW; p->wave2 = W_SAW; p->pw = 0.5f; p->fm_ratio = 2.0f;
    p->ftype = F_LP; p->cutoff = 90.0f; p->res = 0.2f;
    p->a = 0.01f; p->d = 0.3f; p->s = 0.7f; p->r = 0.4f;
    p->fa = 0.01f; p->fd = 0.4f; p->fs = 0.0f; p->fr = 0.4f;
    p->lfo_rate = 0.2f;
    p->bus = BUS_MAIN;
}

static float dark_cut(const Composer *c) { return -10.0f * c->s_dark + (c->P.cutoff - 0.5f) * 16.0f; }

void patch_drone(Patch *p, const Composer *c, int variant) {
    const DistrictProfile *dp = &hush_profiles[c->district_major];
    patch_defaults(p);
    p->bus = BUS_PAD;
    float mod = c->P.mod_depth, lfo = c->P.lfo_rate;
    p->a = 6.0f; p->d = 2.0f; p->s = 1.0f; p->r = 7.0f;
    p->lfo_rate = 0.025f + 0.11f * lfo;
    p->send_reverb = 0.45f; p->send_delay = 0.06f;
    if (variant == 0) {
        p->wave1 = dp->drone_wave; p->wave2 = dp->drone_wave2; p->mix2 = 0.35f;
        p->detune2 = 0.06f + 0.22f * c->P.detune;
        p->cutoff = c->pb.drone_cut + dark_cut(c); p->res = 0.28f; p->key_track = 0.3f;
        p->lfo_cut = 5.0f + 9.0f * mod; p->lfo_pitch = 2.0f + 4.0f * mod; p->lfo_morph = 0.3f * mod;
        p->drive = c->district_major == DISTRICT_WORKS ? 0.35f : 0.0f;
    } else if (variant == 1) {
        p->wave1 = W_TRI; p->wave2 = dp->drone_wave; p->mix2 = 0.2f; p->detune2 = -0.08f - 0.15f * c->P.detune;
        p->cutoff = c->pb.drone_cut + 6.0f + dark_cut(c); p->res = 0.15f; p->key_track = 0.4f;
        p->lfo_rate *= 1.37f; p->lfo_cut = 4.0f + 6.0f * mod; p->lfo_pitch = 3.0f;
    } else {
        /* high shimmer partial */
        p->wave1 = W_GLASS; p->wave2 = W_SINE; p->mix2 = 0.5f; p->detune2 = 12.03f;
        p->cutoff = 104.0f + dark_cut(c); p->res = 0.1f;
        p->a = 9.0f; p->r = 9.0f; p->lfo_rate *= 0.6f; p->lfo_morph = 0.5f; p->lfo_pitch = 5.0f;
        p->send_reverb = 0.85f; p->send_delay = 0.25f;
    }
}

void patch_sub(Patch *p, const Composer *c) {
    (void)c;
    patch_defaults(p);
    p->bus = BUS_PAD; p->wave1 = W_SINE; p->wave2 = W_TRI; p->mix2 = 0.15f; p->ftype = F_LP; p->cutoff = 60.0f;
    p->a = 7.0f; p->d = 1.0f; p->s = 1.0f; p->r = 8.0f; p->lfo_rate = 0.05f; p->lfo_pitch = 4.0f;
    p->send_reverb = 0.25f;
}

void patch_pad(Patch *p, const Composer *c, int vox) {
    const DistrictProfile *dp = &hush_profiles[c->district_major];
    patch_defaults(p);
    p->bus = BUS_PAD;
    int w = c->P.pad_wave >= 0 && c->P.pad_wave < W_COUNT && c->P.pad_wave != W_NOISE && c->P.pad_wave != W_FM ? c->P.pad_wave : dp->pad_wave;
    if (vox) w = W_VOX;
    p->wave1 = (uint8_t)w; p->wave2 = vox ? W_GLASS : W_SAW; p->mix2 = vox ? 0.25f : 0.3f;
    p->detune2 = 0.08f + 0.25f * c->P.detune;
    p->cutoff = c->pb.pad_cut + dark_cut(c); p->res = 0.18f; p->key_track = 0.5f;
    p->a = 2.5f + 2.5f * (1.0f - c->s_motion); p->d = 2.0f; p->s = 0.85f; p->r = 4.5f;
    p->fa = 3.0f; p->fd = 4.0f; p->fs = 0.5f; p->fr = 4.0f; p->fenv_amt = 10.0f;
    p->lfo_rate = 0.06f + 0.25f * c->P.lfo_rate; p->lfo_cut = 3.0f + 6.0f * c->P.mod_depth;
    p->lfo_morph = 0.35f * c->P.mod_depth; p->lfo_pitch = 3.0f + 4.0f * c->P.mod_depth;
    if (vox) { p->lfo_pitch = 9.0f; p->lfo_rate = 4.8f; p->a = 3.5f; p->r = 6.0f; }
    p->send_reverb = 0.6f; p->send_delay = 0.12f;
}

void patch_stab(Patch *p, const Composer *c) {
    patch_defaults(p);
    p->bus = BUS_PAD;
    p->wave1 = W_SAW; p->wave2 = W_PULSE; p->mix2 = 0.45f; p->detune2 = 0.1f + 0.2f * c->P.detune; p->pw = 0.35f;
    p->cutoff = 64.0f + dark_cut(c); p->res = 0.42f; p->fenv_amt = 30.0f - 10.0f * c->s_dark; p->key_track = 0.3f;
    p->fa = 0.001f; p->fd = 0.13f; p->fs = 0.0f; p->fr = 0.2f;
    p->a = 0.003f; p->d = 0.28f; p->s = 0.0f; p->r = 0.2f;
    p->send_delay = 0.7f; p->send_reverb = 0.3f;
}

void patch_bass(Patch *p, const Composer *c, int style) {
    patch_defaults(p);
    p->bus = BUS_MAIN;
    float dk = dark_cut(c) * 0.5f;
    switch (style) {
    case BS_DRIVING:
        p->wave1 = W_GRIT; p->wave2 = W_SQUARE; p->mix2 = 0.4f; p->detune2 = -12.0f;
        p->cutoff = 48.0f + dk; p->res = 0.5f; p->fenv_amt = 34.0f; p->fd = 0.14f; p->drive = 0.55f;
        p->a = 0.003f; p->d = 0.2f; p->s = 0.45f; p->r = 0.08f; p->glide = 0.04f; break;
    case BS_DUB:
        p->wave1 = W_SINE; p->wave2 = W_TRI; p->mix2 = 0.35f; p->detune2 = 0.0f;
        p->cutoff = 58.0f + dk; p->res = 0.1f; p->fenv_amt = 10.0f; p->fd = 0.3f; p->drive = 0.18f;
        p->a = 0.006f; p->d = 0.5f; p->s = 0.7f; p->r = 0.22f; p->glide = 0.09f; break;
    case BS_DEEP:
        p->wave1 = W_SINE; p->wave2 = W_HOLLOW; p->mix2 = 0.12f;
        p->cutoff = 52.0f + dk; p->res = 0.1f; p->fenv_amt = 6.0f;
        p->a = 0.35f; p->d = 1.5f; p->s = 0.8f; p->r = 1.6f; p->glide = 0.3f; p->send_reverb = 0.2f; break;
    case BS_STEADY: default:
        p->wave1 = W_PULSE; p->wave2 = W_SINE; p->mix2 = 0.45f; p->pw = 0.3f; p->lfo_pwm = 0.15f; p->lfo_rate = 0.13f;
        p->cutoff = 50.0f + dk; p->res = 0.35f; p->fenv_amt = 24.0f; p->fd = 0.22f;
        p->a = 0.004f; p->d = 0.32f; p->s = 0.5f; p->r = 0.12f; p->glide = 0.06f; break;
    }
    p->send_delay = 0.04f;
}

void patch_arp(Patch *p, const Composer *c) {
    const DistrictProfile *dp = &hush_profiles[c->district_major];
    patch_defaults(p);
    p->bus = BUS_MAIN;
    p->wave1 = dp->arp_wave; p->wave2 = W_SINE; p->mix2 = 0.25f; p->detune2 = 12.0f; p->pw = 0.25f;
    if (c->district_major == DISTRICT_WORKS) { p->wave1 = W_FM; p->fm_ratio = 2.41f; p->fm_index = 0.9f; }
    p->cutoff = c->pb.arp_cut + dark_cut(c); p->res = 0.3f; p->fenv_amt = 26.0f; p->key_track = 0.4f;
    p->fa = 0.001f; p->fd = 0.18f; p->fs = 0.0f; p->fr = 0.2f;
    p->a = 0.002f; p->d = 0.38f; p->s = 0.0f; p->r = 0.3f;
    p->send_delay = 0.55f; p->send_reverb = 0.35f;
}

void patch_frag(Patch *p, const Composer *c, int inst) {
    patch_defaults(p);
    float dk = dark_cut(c);
    switch (inst) {
    case FI_BELL:
        p->wave1 = W_FM; p->fm_ratio = 3.51f; p->fm_index = 0.75f; p->ftype = F_LP; p->cutoff = 118.0f + dk;
        p->a = 0.002f; p->d = 1.6f; p->s = 0.0f; p->r = 1.4f; p->fa = 0.001f; p->fd = 0.6f; p->fs = 0.0f;
        p->send_reverb = 0.6f; p->send_delay = 0.4f; p->bus = BUS_MAIN; break;
    case FI_GLASS:
        p->wave1 = W_GLASS; p->wave2 = W_SINE; p->mix2 = 0.3f; p->detune2 = 0.05f; p->cutoff = 104.0f + dk; p->res = 0.2f;
        p->a = 0.01f; p->d = 1.8f; p->s = 0.15f; p->r = 1.8f; p->lfo_rate = 0.4f; p->lfo_morph = 0.3f;
        p->send_reverb = 0.65f; p->send_delay = 0.35f; p->bus = BUS_MAIN; break;
    case FI_RADIO:
        p->wave1 = W_PULSE; p->wave2 = W_SAW; p->mix2 = 0.3f; p->pw = 0.2f; p->cutoff = 92.0f + dk; p->res = 0.45f;
        p->a = 0.02f; p->d = 0.5f; p->s = 0.55f; p->r = 0.35f; p->lfo_rate = 5.5f; p->lfo_pitch = 14.0f;
        p->send_reverb = 0.3f; p->send_delay = 0.45f; p->bus = BUS_RADIO; break;
    case FI_VOX: default:
        p->wave1 = W_VOX; p->wave2 = W_GLASS; p->mix2 = 0.2f; p->detune2 = 0.04f; p->cutoff = 98.0f + dk; p->res = 0.15f;
        p->a = 0.45f; p->d = 1.0f; p->s = 0.7f; p->r = 1.8f; p->lfo_rate = 4.6f; p->lfo_pitch = 8.0f;
        p->send_reverb = 0.75f; p->send_delay = 0.2f; p->bus = BUS_PAD; break;
    }
}

void patch_texture(Patch *p, const Composer *c, int which) {
    patch_defaults(p);
    p->bus = BUS_PAD;
    p->a = 4.0f; p->d = 1.0f; p->s = 1.0f; p->r = 4.0f;
    if (which == 0) {
        /* rain / air: band-passed noise */
        p->wave1 = W_NOISE; p->ftype = F_BP; p->cutoff = 101.0f; p->res = 0.25f;
        p->lfo_rate = 0.07f; p->lfo_cut = 7.0f; p->send_reverb = 0.35f;
    } else {
        switch (c->district_major) {
        case DISTRICT_WORKS:   /* transformer hum and machinery */
            p->wave1 = W_GRIT; p->wave2 = W_NOISE; p->mix2 = 0.15f; p->cutoff = 58.0f; p->res = 0.5f; p->drive = 0.4f;
            p->lfo_rate = 0.19f; p->lfo_cut = 6.0f; p->send_reverb = 0.3f; break;
        case DISTRICT_UNDERTOW: /* slow water */
            p->wave1 = W_NOISE; p->ftype = F_LP; p->cutoff = 64.0f; p->res = 0.3f; p->lfo_rate = 0.045f; p->lfo_cut = 9.0f;
            p->send_reverb = 0.6f; break;
        case DISTRICT_STACKS:  /* distant radio static */
            p->wave1 = W_NOISE; p->ftype = F_BP; p->cutoff = 92.0f; p->res = 0.55f; p->lfo_rate = 0.11f; p->lfo_cut = 10.0f;
            p->bus = BUS_RADIO; p->send_delay = 0.2f; break;
        default:               /* civic: high air-conditioning whine and low traffic rumble */
            p->wave1 = W_SINE; p->wave2 = W_NOISE; p->mix2 = 0.5f; p->ftype = F_LP; p->cutoff = 70.0f; p->lfo_rate = 0.09f;
            p->lfo_cut = 5.0f; p->send_reverb = 0.4f; break;
        }
    }
}

/* Landmark motifs: fixed, recognisable phrases (scale degrees relative to the tonic). */
static const Motif lm_motifs[LM_COUNT] = {
    /* CHOIR TOWER: slow descending suspension */
    { {9, 8, 7, 8, 6, 0, 0, 0}, {8, 8, 12, 4, 16, 0, 0, 0}, {80, 70, 75, 60, 70, 0, 0, 0}, 5, FI_VOX, 0 },
    /* STATION NINE: mechanical repeated cell */
    { {4, 4, 7, 4, 3, 4, 0, 0}, {2, 2, 4, 2, 2, 4, 0, 0}, {90, 60, 80, 60, 70, 85, 0, 0}, 6, FI_BELL, 0 },
    /* MEMORY ARCHIVE: the remembered tune that erodes with every repetition */
    { {7, 9, 11, 9, 7, 6, 4, 0}, {3, 3, 4, 2, 3, 3, 8, 0}, {85, 75, 90, 70, 80, 70, 75, 0}, 7, FI_RADIO, 0 },
    /* BLACK RESERVOIR: low, wide intervals */
    { {0, -3, 4, -1, 0, 0, 0, 0}, {12, 12, 16, 8, 24, 0, 0, 0}, {90, 80, 70, 60, 70, 0, 0, 0}, 5, FI_GLASS, 0 },
    /* SILENT EXCHANGE: two notes, a question left unanswered */
    { {11, 8, 0, 0, 0, 0, 0, 0}, {12, 32, 0, 0, 0, 0, 0, 0}, {60, 50, 0, 0, 0, 0, 0, 0}, 2, FI_GLASS, 0 },
};
const Motif *landmark_motif(int lm) { return (lm >= 0 && lm < LM_COUNT) ? &lm_motifs[lm] : 0; }

/* ---------------- Presets ---------------- */
static void preset_base(MusicParams *p) {
    memset(p, 0, sizeof(*p));
    p->master = 0.8f;
    for (int i = 0; i < VOL_COUNT; i++) p->vol[i] = 0.8f;
    p->density = 0.5f; p->darkness = 0.5f; p->motion = 0.5f; p->tempo = 88.0f;
    p->key = 9; p->mode = MODE_AUTO; p->filter = 0.5f; p->delay = 0.5f; p->reverb = 0.55f; p->seed = 4817;
    p->pad_wave = -1; p->detune = 0.35f; p->drone_root = 0; p->cutoff = 0.5f; p->lfo_rate = 0.35f;
    p->complexity = 0.45f; p->rhythm_prob = 0.6f; p->seq_variation = 0.4f; p->melody_prob = 0.5f;
    p->delay_fb = 0.45f; p->mod_depth = 0.5f;
}

void hush_preset_get(int id, MusicParams *p) {
    preset_base(p);
    switch (id) {
    case PRESET_PURSUIT:
        p->tempo = 112.0f; p->density = 0.78f; p->darkness = 0.72f; p->motion = 0.75f; p->mode = MODE_PHRYGIAN; p->key = 4;
        p->vol[VOL_PERC] = 0.9f; p->vol[VOL_BASS] = 0.9f; p->vol[VOL_FRAG] = 0.55f; p->complexity = 0.55f;
        p->rhythm_prob = 0.85f; p->seq_variation = 0.55f; p->delay = 0.42f; p->delay_fb = 0.4f; p->seed = 9002;
        break;
    case PRESET_DRIFT:
        p->tempo = 72.0f; p->density = 0.25f; p->darkness = 0.45f; p->motion = 0.3f; p->reverb = 0.8f; p->delay = 0.6f;
        p->vol[VOL_PERC] = 0.35f; p->vol[VOL_BASS] = 0.55f; p->vol[VOL_DRONE] = 0.9f; p->vol[VOL_HARMONY] = 0.85f;
        p->rhythm_prob = 0.3f; p->melody_prob = 0.4f; p->delay_fb = 0.6f; p->complexity = 0.5f; p->seed = 2146;
        break;
    case PRESET_ARCHIVE:
        p->tempo = 80.0f; p->density = 0.4f; p->darkness = 0.6f; p->mode = MODE_DORIAN; p->key = 2;
        p->vol[VOL_FRAG] = 1.0f; p->melody_prob = 0.85f; p->vol[VOL_TEXTURE] = 0.9f; p->pad_wave = W_ORGAN; p->seed = 1977;
        break;
    case PRESET_RESERVOIR:
        p->tempo = 60.0f; p->density = 0.2f; p->darkness = 0.85f; p->mode = MODE_LOCRIAN; p->key = 1;
        p->vol[VOL_DRONE] = 1.0f; p->vol[VOL_PERC] = 0.3f; p->reverb = 0.95f; p->filter = 0.3f; p->seed = 333;
        break;
    case PRESET_CHOIR:
        p->tempo = 66.0f; p->density = 0.3f; p->darkness = 0.4f; p->mode = MODE_AEOLIAN; p->key = 7;
        p->pad_wave = W_VOX; p->vol[VOL_HARMONY] = 1.0f; p->vol[VOL_PERC] = 0.25f; p->reverb = 0.9f; p->complexity = 0.7f; p->seed = 7070;
        break;
    case PRESET_NULLZONE:
        p->tempo = 96.0f; p->density = 0.55f; p->darkness = 0.9f; p->mode = MODE_HARMMINOR; p->key = 11; p->motion = 0.9f;
        p->pad_wave = W_GRIT; p->detune = 0.9f; p->mod_depth = 0.95f; p->delay_fb = 0.8f; p->seed = 0;
        break;
    case PRESET_PATROL: default:
        break;
    }
}

int hush_preset_find(const char *name) {
    for (int i = 0; i < PRESET_BUILTIN_COUNT; i++) {
        const char *a = hush_preset_names[i], *b = name;
        int ok = 1;
        while (*a || *b) {
            char ca = *a, cb = *b;
            if (ca == ' ') ca = '_';
            if (cb == ' ' || cb == '-') cb = '_';
            if (toupper((unsigned char)ca) != toupper((unsigned char)cb)) { ok = 0; break; }
            if (*a) a++;
            if (*b) b++;
        }
        if (ok) return i;
    }
    return -1;
}
