/* HUSH — progress and save serialisation. */
#include "progress.h"
#include "missions.h"
#include "../audio/dsp.h"
#include <string.h>

void progress_defaults(Progress *p) {
    memset(p, 0, sizeof(*p));
    p->districts = 1u << DISTRICT_CIVIC;
    p->waves = (1u << W_SINE) | (1u << W_TRI) | (1u << W_SAW) | (1u << W_SQUARE) | (1u << W_PULSE) | (1u << W_ORGAN);
    p->presets = (1u << PRESET_PATROL) | (1u << PRESET_PURSUIT) | (1u << PRESET_DRIFT);
    p->paints = 1;
    hush_preset_get(PRESET_PATROL, &p->music);
    for (int i = 0; i < USER_PRESETS; i++) hush_preset_get(PRESET_DRIFT, &p->user[i]);
    p->seeds[0] = 4817; p->seeds[1] = 2146; p->seeds[2] = 9002; p->n_seeds = 3;
    p->hud_mode = HUD_FULL; p->cam_mode = 0; p->rain_density = 2; p->music_vol = 8; p->sfx_vol = 7;
    p->drift_district = DISTRICT_STACKS; p->drift_weather = 4; p->drift_tod = 0; p->drift_preset = PRESET_DRIFT;
}

uint32_t progress_reward(Progress *p, uint32_t r) {
    uint32_t fresh = r & ~p->rewards;
    if (r & RW_SCANNER) p->scanner_level = (uint8_t)h_mini(p->scanner_level + 1, 3);
    if (r & RW_BOOST) p->boost_level = (uint8_t)h_mini(p->boost_level + 1, 3);
    if (r & RW_HULL) p->hull_level = (uint8_t)h_mini(p->hull_level + 1, 3);
    if (r & RW_EMP) p->has_emp = 1;
    if (r & RW_STACKS) p->districts |= 1u << DISTRICT_STACKS;
    if (r & RW_WORKS) p->districts |= 1u << DISTRICT_WORKS;
    if (r & RW_UNDERTOW) p->districts |= 1u << DISTRICT_UNDERTOW;
    if (r & RW_WAVE_VOX) p->waves |= 1u << W_VOX;
    if (r & RW_WAVE_HOLLOW) p->waves |= 1u << W_HOLLOW;
    if (r & RW_WAVE_GRIT) p->waves |= 1u << W_GRIT;
    if (r & RW_WAVE_GLASS) p->waves |= 1u << W_GLASS;
    if (r & RW_PRESET_ARCHIVE) p->presets |= 1u << PRESET_ARCHIVE;
    if (r & RW_PRESET_CHOIR) p->presets |= 1u << PRESET_CHOIR;
    if (r & RW_PRESET_RESERVOIR) p->presets |= 1u << PRESET_RESERVOIR;
    if (r & RW_PRESET_NULL) p->presets |= 1u << PRESET_NULLZONE;
    if (r & RW_PAINT) p->paints |= 2;
    p->rewards |= r;
    return fresh;
}

void progress_discover(Progress *p, int lm) {
    if (lm >= 0 && lm < LM_COUNT) p->landmarks |= (uint8_t)(1u << lm);
}

int progress_mission_unlocked(const Progress *p, int m) {
    if (m <= 0) return 1;
    if (m >= MISSION_COUNT) return 0;
    return (p->missions_done & (1u << (m - 1))) != 0;
}

int progress_next_mission(const Progress *p) {
    for (int i = 0; i < MISSION_COUNT; i++) if (!(p->missions_done & (1u << i))) return i;
    return MISSION_COUNT - 1;
}

/* ---------------- CRC-32 ---------------- */
uint32_t hush_crc32(const uint8_t *data, int len) {
    uint32_t c = 0xFFFFFFFFu;
    for (int i = 0; i < len; i++) {
        c ^= data[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

/* ---------------- byte writer / reader ---------------- */
typedef struct { uint8_t *b; int n, cap, err; } W;
typedef struct { const uint8_t *b; int n, len, err; } R;

static void w8(W *w, uint32_t v) { if (w->n + 1 > w->cap) { w->err = 1; return; } w->b[w->n++] = (uint8_t)v; }
static void w16(W *w, uint32_t v) { w8(w, v & 0xFF); w8(w, (v >> 8) & 0xFF); }
static void w32(W *w, uint32_t v) { w16(w, v & 0xFFFF); w16(w, v >> 16); }
static void wf(W *w, float f) { uint32_t u; memcpy(&u, &f, 4); w32(w, u); }
static uint32_t r8(R *r) { if (r->n + 1 > r->len) { r->err = 1; return 0; } return r->b[r->n++]; }
static uint32_t r16(R *r) { uint32_t a = r8(r); return a | (r8(r) << 8); }
static uint32_t r32(R *r) { uint32_t a = r16(r); return a | (r16(r) << 16); }
static float rf(R *r) { uint32_t u = r32(r); float f; memcpy(&f, &u, 4); return (f == f) ? f : 0.0f; }

static void w_music(W *w, const MusicParams *m) {
    wf(w, m->master);
    for (int i = 0; i < VOL_COUNT; i++) wf(w, m->vol[i]);
    wf(w, m->density); wf(w, m->darkness); wf(w, m->motion); wf(w, m->tempo);
    w8(w, (uint32_t)(m->key & 0xFF)); w8(w, (uint32_t)(m->mode + 1));
    wf(w, m->filter); wf(w, m->delay); wf(w, m->reverb);
    w32(w, m->seed);
    w8(w, (uint32_t)(m->pad_wave + 1)); wf(w, m->detune); w8(w, (uint32_t)(m->drone_root + 16));
    wf(w, m->cutoff); wf(w, m->lfo_rate); wf(w, m->complexity); wf(w, m->rhythm_prob);
    wf(w, m->seq_variation); wf(w, m->melody_prob); wf(w, m->delay_fb); wf(w, m->mod_depth);
}

static float c01(float v) { return h_clampf(v, 0.0f, 1.0f); }

static void r_music(R *r, MusicParams *m) {
    m->master = c01(rf(r));
    for (int i = 0; i < VOL_COUNT; i++) m->vol[i] = c01(rf(r));
    m->density = c01(rf(r)); m->darkness = c01(rf(r)); m->motion = c01(rf(r));
    m->tempo = h_clampf(rf(r), 50.0f, 140.0f);
    m->key = h_clampi((int)r8(r), 0, 11);
    m->mode = h_clampi((int)r8(r) - 1, MODE_AUTO, MODE_COUNT - 1);
    m->filter = c01(rf(r)); m->delay = c01(rf(r)); m->reverb = c01(rf(r));
    m->seed = r32(r);
    m->pad_wave = h_clampi((int)r8(r) - 1, -1, W_COUNT - 1);
    m->detune = c01(rf(r));
    m->drone_root = h_clampi((int)r8(r) - 16, -7, 7);
    m->cutoff = c01(rf(r)); m->lfo_rate = c01(rf(r)); m->complexity = c01(rf(r)); m->rhythm_prob = c01(rf(r));
    m->seq_variation = c01(rf(r)); m->melody_prob = c01(rf(r)); m->delay_fb = c01(rf(r)); m->mod_depth = c01(rf(r));
}

int progress_serialize(const Progress *p, uint8_t *buf, int cap) {
    if (cap < 16) return 0;
    W w = { buf + 16, 0, cap - 16, 0 };
    w32(&w, p->missions_done); w32(&w, p->rewards);
    w8(&w, p->scanner_level); w8(&w, p->boost_level); w8(&w, p->hull_level); w8(&w, p->has_emp);
    w8(&w, p->districts); w8(&w, p->landmarks); w16(&w, p->waves); w16(&w, p->presets);
    w8(&w, p->paint); w8(&w, p->paints);
    w_music(&w, &p->music);
    for (int i = 0; i < USER_PRESETS; i++) { w8(&w, p->user_used[i]); w_music(&w, &p->user[i]); }
    w8(&w, p->n_seeds);
    for (int i = 0; i < SAVED_SEEDS; i++) w32(&w, p->seeds[i]);
    w8(&w, p->hud_mode); w8(&w, p->cam_mode); w8(&w, p->show_fps); w8(&w, p->rain_density);
    w8(&w, p->music_vol); w8(&w, p->sfx_vol); w8(&w, p->invert_lift);
    w8(&w, p->drift_district); w8(&w, p->drift_weather); w8(&w, p->drift_tod); w8(&w, p->drift_preset);
    w32(&w, p->play_seconds);
    w8(&w, p->control_scheme);
    w8(&w, p->story_flags);
    if (w.err) return 0;
    W h = { buf, 0, 16, 0 };
    w32(&h, SAVE_MAGIC); w16(&h, SAVE_VERSION); w16(&h, 0); w32(&h, (uint32_t)w.n); w32(&h, hush_crc32(buf + 16, w.n));
    return 16 + w.n;
}

int progress_deserialize(Progress *p, const uint8_t *buf, int len, const char **err) {
    progress_defaults(p);
    const char *e = NULL;
    if (!buf || len < 16) e = "file too short";
    R h = { buf, 0, len, 0 };
    uint32_t magic = 0, ver = 0, plen = 0, crc = 0;
    if (!e) { magic = r32(&h); ver = r16(&h); r16(&h); plen = r32(&h); crc = r32(&h); }
    if (!e && magic != SAVE_MAGIC) e = "not a HUSH save";
    if (!e && ver != SAVE_VERSION) e = "unsupported save version";
    if (!e && (plen > (uint32_t)(len - 16) || plen > SAVE_MAX_BYTES)) e = "truncated save";
    if (!e && hush_crc32(buf + 16, (int)plen) != crc) e = "checksum mismatch (damaged save)";
    if (e) { if (err) *err = e; progress_defaults(p); return 0; }
    R r = { buf + 16, 0, (int)plen, 0 };
    Progress q;
    progress_defaults(&q);
    q.missions_done = r32(&r) & ((1u << MISSION_COUNT) - 1);
    q.rewards = r32(&r);
    q.scanner_level = (uint8_t)h_mini((int)r8(&r), 3); q.boost_level = (uint8_t)h_mini((int)r8(&r), 3);
    q.hull_level = (uint8_t)h_mini((int)r8(&r), 3); q.has_emp = r8(&r) ? 1 : 0;
    q.districts = (uint8_t)((r8(&r) & 0x0F) | 1u); q.landmarks = (uint8_t)(r8(&r) & 0x1F);
    q.waves = (uint16_t)(r16(&r) & ((1u << W_COUNT) - 1)); q.presets = (uint16_t)(r16(&r) & ((1u << PRESET_BUILTIN_COUNT) - 1));
    q.paint = (uint8_t)h_mini((int)r8(&r), 1); q.paints = (uint8_t)(r8(&r) | 1u);
    r_music(&r, &q.music);
    for (int i = 0; i < USER_PRESETS; i++) { q.user_used[i] = r8(&r) ? 1 : 0; r_music(&r, &q.user[i]); }
    q.n_seeds = (uint8_t)h_mini((int)r8(&r), SAVED_SEEDS);
    for (int i = 0; i < SAVED_SEEDS; i++) q.seeds[i] = r32(&r);
    q.hud_mode = (uint8_t)h_mini((int)r8(&r), HUD_COUNT - 1); q.cam_mode = (uint8_t)h_mini((int)r8(&r), 2);
    q.show_fps = r8(&r) ? 1 : 0; q.rain_density = (uint8_t)h_mini((int)r8(&r), 3);
    q.music_vol = (uint8_t)h_mini((int)r8(&r), 10); q.sfx_vol = (uint8_t)h_mini((int)r8(&r), 10); q.invert_lift = r8(&r) ? 1 : 0;
    q.drift_district = (uint8_t)h_mini((int)r8(&r), DISTRICT_COUNT - 1); q.drift_weather = (uint8_t)h_mini((int)r8(&r), 4);
    q.drift_tod = (uint8_t)h_mini((int)r8(&r), 2); q.drift_preset = (uint8_t)h_mini((int)r8(&r), PRESET_BUILTIN_COUNT - 1);
    q.play_seconds = r32(&r);
    /* fields appended after the first release: absent in older saves, so keep defaults */
    if (r.n < r.len) q.control_scheme = (uint8_t)h_mini((int)r8(&r), CTL_COUNT - 1);
    if (r.n < r.len) q.story_flags = (uint8_t)r8(&r);
    /* essential features always remain available */
    q.waves |= (1u << W_SINE) | (1u << W_TRI) | (1u << W_SAW) | (1u << W_SQUARE) | (1u << W_PULSE) | (1u << W_ORGAN);
    q.presets |= (1u << PRESET_PATROL) | (1u << PRESET_PURSUIT) | (1u << PRESET_DRIFT);
    if (r.err) { if (err) *err = "save payload truncated"; return 0; }
    *p = q;
    if (err) *err = NULL;
    return 1;
}
