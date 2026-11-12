/* hush-render — offline renderer for the HUSH generative music engine.
 *
 * Links the exact same DSP and composition code that runs on the PSP
 * (src/audio/ sources). Renders stereo 16-bit WAV files and prints diagnostics.
 *
 *   hush-render --seed 4817 --preset drift --minutes 10 --output hush_drift.wav
 *
 * Run with --help for the full option list. */
#include "../../src/audio/music.h"
#include "../../src/core/hmath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <math.h>

static void usage(void) {
    printf(
        "hush-render — HUSH generative music engine, offline renderer\n\n"
        "usage: hush-render [options]\n"
        "  --seed N            composition seed (default: preset seed)\n"
        "  --preset NAME       patrol | pursuit | drift | archive | reservoir | choir | null_zone (default patrol)\n"
        "  --minutes M         render length in minutes (default 2)\n"
        "  --seconds S         render length in seconds (overrides --minutes)\n"
        "  --output FILE.wav   write a 16-bit stereo WAV (omit to analyse only)\n"
        "  --rate HZ           engine sample rate, 44100 (default) or 22050 (PSP engine rate)\n"
        "  --district NAME     civic | works | stacks | undertow (default civic)\n"
        "  --altitude X        0..1 (default 0.5)\n"
        "  --speed X           0..1 (default 0.25)\n"
        "  --rain X            0..1 (default 0)\n"
        "  --fog X             0..1 (default 0.3)\n"
        "  --pursuit X         0..1 (default 0)\n"
        "  --traffic X         0..1 (default 0.3)\n"
        "  --canyon X          0..1 (default 0.3)\n"
        "  --landmark NAME     choir | station9 | archive | reservoir | exchange\n"
        "  --prox X            landmark proximity 0..1 (default 0.8 when --landmark is set)\n"
        "  --flight            simulate a slow deterministic flight (altitude/speed/canyon wander)\n"
        "  --console           Signal Console mode: no world inputs\n"
        "  --tempo BPM --key 0-11 --mode NAME|auto --density X --darkness X --motion X\n"
        "  --complexity X --melody X --rhythm X   (override preset parameters)\n"
        "  --vol LAYER=X       layer volume override (drone|harmony|bass|perc|frag|texture), repeatable\n"
        "  --trace             print every scheduled event (step layer note velocity)\n"
        "  --events            print the first 200 musical events\n"
        "  --quiet             only print the summary line\n"
        "  --help\n");
}

static int parse_district(const char *s) {
    if (!strcasecmp(s, "civic")) return DISTRICT_CIVIC;
    if (!strcasecmp(s, "works")) return DISTRICT_WORKS;
    if (!strcasecmp(s, "stacks")) return DISTRICT_STACKS;
    if (!strcasecmp(s, "undertow")) return DISTRICT_UNDERTOW;
    return -1;
}
static int parse_landmark(const char *s) {
    if (!strcasecmp(s, "choir")) return LM_CHOIR;
    if (!strcasecmp(s, "station9")) return LM_STATION9;
    if (!strcasecmp(s, "archive")) return LM_ARCHIVE;
    if (!strcasecmp(s, "reservoir")) return LM_RESERVOIR;
    if (!strcasecmp(s, "exchange")) return LM_EXCHANGE;
    return -1;
}
static int parse_mode(const char *s) {
    if (!strcasecmp(s, "auto")) return MODE_AUTO;
    for (int i = 0; i < MODE_COUNT; i++) {
        char buf[32]; int k = 0;
        for (const char *p = hush_mode_names[i]; *p && k < 31; p++) buf[k++] = *p == ' ' ? '_' : *p;
        buf[k] = 0;
        if (!strcasecmp(s, buf) || !strcasecmp(s, hush_mode_names[i])) return i;
    }
    return -2;
}

static void wav_header(FILE *f, int rate, uint32_t frames) {
    uint32_t data = frames * 4, riff = 36 + data, br = (uint32_t)rate * 4, fmt = 16, sr = (uint32_t)rate;
    uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f);
    fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
}

static void trace_ev(uint32_t step, int layer, int note, int vel) { printf("EV %u %d %d %d\n", step, layer, note, vel); }

int main(int argc, char **argv) {
    int preset = PRESET_PATROL, rate = 44100, quiet = 0, flight = 0, console = 0, show_events = 0;
    long seed = -1;
    double seconds = 120.0;
    const char *out_path = NULL;
    MusicEnv env;
    memset(&env, 0, sizeof(env));
    env.active = 1; env.district = DISTRICT_CIVIC; env.altitude = 0.5f; env.speed = 0.25f; env.fog = 0.3f;
    env.traffic = 0.3f; env.canyon = 0.3f; env.landmark = -1;
    float prox = -1;
    /* parameter overrides (NAN = keep preset) */
    float o_tempo = NAN, o_density = NAN, o_dark = NAN, o_motion = NAN, o_complex = NAN, o_melody = NAN, o_rhythm = NAN;
    int o_key = -1, o_mode = -3;
    float o_vol[VOL_COUNT];
    for (int k = 0; k < VOL_COUNT; k++) o_vol[k] = NAN;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
#define NEED() do { if (!v) { fprintf(stderr, "missing value for %s\n", a); return 2; } i++; } while (0)
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
        else if (!strcmp(a, "--seed")) { NEED(); seed = strtol(v, NULL, 10); }
        else if (!strcmp(a, "--preset")) { NEED(); preset = hush_preset_find(v); if (preset < 0) { fprintf(stderr, "unknown preset '%s'\n", v); return 2; } }
        else if (!strcmp(a, "--minutes")) { NEED(); seconds = atof(v) * 60.0; }
        else if (!strcmp(a, "--seconds")) { NEED(); seconds = atof(v); }
        else if (!strcmp(a, "--output") || !strcmp(a, "-o")) { NEED(); out_path = v; }
        else if (!strcmp(a, "--rate")) { NEED(); rate = atoi(v); if (rate != 44100 && rate != 22050) { fprintf(stderr, "rate must be 44100 or 22050\n"); return 2; } }
        else if (!strcmp(a, "--district")) { NEED(); env.district = parse_district(v); if (env.district < 0) { fprintf(stderr, "unknown district '%s'\n", v); return 2; } }
        else if (!strcmp(a, "--altitude")) { NEED(); env.altitude = (float)atof(v); }
        else if (!strcmp(a, "--speed")) { NEED(); env.speed = (float)atof(v); }
        else if (!strcmp(a, "--rain")) { NEED(); env.rain = (float)atof(v); }
        else if (!strcmp(a, "--fog")) { NEED(); env.fog = (float)atof(v); }
        else if (!strcmp(a, "--pursuit")) { NEED(); env.pursuit = (float)atof(v); }
        else if (!strcmp(a, "--traffic")) { NEED(); env.traffic = (float)atof(v); }
        else if (!strcmp(a, "--canyon")) { NEED(); env.canyon = (float)atof(v); }
        else if (!strcmp(a, "--landmark")) { NEED(); env.landmark = parse_landmark(v); if (env.landmark < 0) { fprintf(stderr, "unknown landmark '%s'\n", v); return 2; } }
        else if (!strcmp(a, "--prox")) { NEED(); prox = (float)atof(v); }
        else if (!strcmp(a, "--flight")) flight = 1;
        else if (!strcmp(a, "--console")) console = 1;
        else if (!strcmp(a, "--tempo")) { NEED(); o_tempo = (float)atof(v); }
        else if (!strcmp(a, "--key")) { NEED(); o_key = atoi(v) % 12; }
        else if (!strcmp(a, "--mode")) { NEED(); o_mode = parse_mode(v); if (o_mode == -2) { fprintf(stderr, "unknown mode '%s'\n", v); return 2; } }
        else if (!strcmp(a, "--density")) { NEED(); o_density = (float)atof(v); }
        else if (!strcmp(a, "--darkness")) { NEED(); o_dark = (float)atof(v); }
        else if (!strcmp(a, "--motion")) { NEED(); o_motion = (float)atof(v); }
        else if (!strcmp(a, "--complexity")) { NEED(); o_complex = (float)atof(v); }
        else if (!strcmp(a, "--melody")) { NEED(); o_melody = (float)atof(v); }
        else if (!strcmp(a, "--rhythm")) { NEED(); o_rhythm = (float)atof(v); }
        else if (!strcmp(a, "--vol")) {
            NEED();
            static const char *vn[VOL_COUNT] = {"drone", "harmony", "bass", "perc", "frag", "texture"};
            const char *eq = strchr(v, '=');
            int found = 0;
            for (int k = 0; eq && k < VOL_COUNT; k++)
                if ((size_t)(eq - v) == strlen(vn[k]) && !strncasecmp(v, vn[k], (size_t)(eq - v))) { o_vol[k] = (float)atof(eq + 1); found = 1; }
            if (!found) { fprintf(stderr, "bad --vol '%s'\n", v); return 2; }
        }
        else if (!strcmp(a, "--events")) show_events = 1;
        else if (!strcmp(a, "--trace")) hush_event_trace = trace_ev;
        else if (!strcmp(a, "--quiet")) quiet = 1;
        else { fprintf(stderr, "unknown option '%s' (try --help)\n", a); return 2; }
    }
    if (env.landmark >= 0) env.landmark_prox = prox >= 0 ? prox : 0.8f;
    if (console) env.active = 0;

    MusicParams P;
    hush_preset_get(preset, &P);
    if (seed >= 0) P.seed = (uint32_t)seed;
    if (!isnan(o_tempo)) P.tempo = o_tempo;
    if (o_key >= 0) P.key = o_key;
    if (o_mode != -3) P.mode = o_mode;
    if (!isnan(o_density)) P.density = o_density;
    if (!isnan(o_dark)) P.darkness = o_dark;
    if (!isnan(o_motion)) P.motion = o_motion;
    if (!isnan(o_complex)) P.complexity = o_complex;
    if (!isnan(o_melody)) P.melody_prob = o_melody;
    if (!isnan(o_rhythm)) P.rhythm_prob = o_rhythm;
    for (int k = 0; k < VOL_COUNT; k++) if (!isnan(o_vol[k])) P.vol[k] = o_vol[k];

    HushMusic *m = hush_music_instance();
    hush_music_init(m, (float)rate, &P);
    hush_music_set_env(m, &env);

    uint32_t total = (uint32_t)(seconds * rate);
    FILE *f = NULL;
    if (out_path) {
        f = fopen(out_path, "wb");
        if (!f) { perror(out_path); return 1; }
        wav_header(f, rate, total);
    }
    if (!quiet) {
        printf("HUSH render: preset=%s seed=%u rate=%d length=%.1fs district=%s%s%s\n",
               hush_preset_names[preset], P.seed, rate, seconds, hush_district_names[env.district],
               console ? " (console mode)" : "", flight ? " (simulated flight)" : "");
        printf("  inputs: altitude=%.2f speed=%.2f rain=%.2f fog=%.2f pursuit=%.2f traffic=%.2f canyon=%.2f landmark=%s prox=%.2f\n",
               env.altitude, env.speed, env.rain, env.fog, env.pursuit, env.traffic, env.canyon,
               env.landmark >= 0 ? hush_landmark_names[env.landmark] : "none", env.landmark_prox);
        printf("  params: tempo=%.0f key=%s mode=%s density=%.2f darkness=%.2f motion=%.2f complexity=%.2f\n",
               P.tempo, hush_note_names[P.key], P.mode < 0 ? "AUTO" : hush_mode_names[P.mode], P.density, P.darkness, P.motion, P.complexity);
    }

    enum { CH = 1024 };
    static float fbuf[CH * 2];
    static int16_t sbuf[CH * 2];
    double sumsq = 0, peak = 0, min_peak_s = 0;
    double minute_sumsq = 0, minute_peak = 0;
    uint32_t clip = 0, silent_blocks = 0, blocks = 0, nan_count = 0;
    uint32_t minute_frames = 0, minute = 0;
    int last_section = -1;
    char last_chord[16] = "";
    uint32_t last_events = 0;
    int ev_shown = 0;
    clock_t t0 = clock();
    (void)min_peak_s;

    for (uint32_t done = 0; done < total;) {
        uint32_t n = total - done < CH ? total - done : CH;
        if (flight && env.active) {
            double t = (double)done / rate;
            env.altitude = (float)(0.5 + 0.38 * sin(t / 71.0) + 0.08 * sin(t / 13.0));
            env.speed = (float)h_clampf((float)(0.35 + 0.4 * sin(t / 37.0 + 1.3) + 0.15 * sin(t / 9.0)), 0.0f, 1.0f);
            env.canyon = (float)h_clampf(0.6f - env.altitude * 0.7f, 0.0f, 1.0f);
            env.traffic = (float)(0.3 + 0.2 * sin(t / 23.0));
            hush_music_set_env(m, &env);
        }
        hush_music_render(m, fbuf, (int)n);
        double bpeak = 0;
        for (uint32_t i = 0; i < 2 * n; i++) {
            float x = fbuf[i];
            if (x != x) { nan_count++; x = 0; }
            double ax = fabs(x);
            if (ax > bpeak) bpeak = ax;
            if (ax >= 0.999) clip++;
            sumsq += (double)x * x;
            minute_sumsq += (double)x * x;
            float s = x * 32767.0f;
            if (s > 32767.0f) s = 32767.0f;
            if (s < -32768.0f) s = -32768.0f;
            sbuf[i] = (int16_t)(s < 0 ? s - 0.5f : s + 0.5f);
        }
        if (bpeak > peak) peak = bpeak;
        if (bpeak > minute_peak) minute_peak = bpeak;
        if (bpeak < 0.001) silent_blocks++;
        blocks++;
        if (f) fwrite(sbuf, sizeof(int16_t), 2 * n, f);
        done += n;
        minute_frames += n;

        const MusicViz *z = hush_music_viz(m);
        if (!quiet && z->section != last_section) {
            printf("  [%6.1fs] bar %4d  section -> %-13s (%d bars)  key %s %s\n", (double)done / rate, z->bar,
                   hush_section_names[z->section], z->section_bars_left, hush_note_names[z->key_center], hush_mode_names[z->mode]);
            last_section = z->section;
        }
        if (!quiet && show_events && strcmp(last_chord, z->chord_name) != 0) {
            printf("  [%6.1fs] chord %s\n", (double)done / rate, z->chord_name);
        }
        memcpy(last_chord, z->chord_name, sizeof(last_chord));
        if (show_events && !quiet && ev_shown < 200 && z->event_count != last_events) {
            uint32_t newn = z->event_count - last_events;
            if (newn > VIZ_EVENTS) newn = VIZ_EVENTS;
            for (uint32_t k = newn; k > 0 && ev_shown < 200; k--) {
                const VizEvent *e = &z->events[(z->event_head - (int)k + VIZ_EVENTS * 4) % VIZ_EVENTS];
                printf("      step %6u  %-10s note %3d vel %3d\n", e->step, hush_layer_names[e->layer], e->note, e->vel);
                ev_shown++;
            }
            last_events = z->event_count;
        }
        if (minute_frames >= (uint32_t)rate * 60 || done == total) {
            double rms = sqrt(minute_sumsq / (2.0 * minute_frames));
            if (!quiet)
                printf("  minute %3u: peak %6.2f dBFS  rms %6.2f dBFS  tempo %.1f  erosion %.2f\n", minute + 1,
                       20.0 * log10(minute_peak + 1e-9), 20.0 * log10(rms + 1e-9), z->tempo, z->erosion);
            minute++; minute_frames = 0; minute_sumsq = 0; minute_peak = 0;
        }
    }
    double secs = (double)(clock() - t0) / CLOCKS_PER_SEC;
    if (f) fclose(f);

    MusicStats st;
    hush_music_stats(m, &st);
    double rms = sqrt(sumsq / (2.0 * (double)total));
    if (!quiet) {
        printf("  events: total %u  atmos %u harmony %u pulse %u perc %u frag %u decay %u\n", hush_music_event_count(m),
               st.layer_events[0], st.layer_events[1], st.layer_events[2], st.layer_events[3], st.layer_events[4], st.layer_events[5]);
        printf("  structure: bars %u  section changes %u  chord changes %u  key moves %u  motifs new/recalled/varied %u/%u/%u\n",
               st.bars, st.section_changes, st.chord_changes, st.key_changes, st.motif_new, st.motif_recall, st.motif_variation);
        printf("  sections:");
        for (int s = 0; s < SEC_COUNT; s++) printf(" %s=%u", hush_section_names[s], st.section_hist[s]);
        printf("\n  voices: max simultaneous %u  steals %u\n", st.max_voices, st.voice_steals);
        printf("  level: peak %.2f dBFS  rms %.2f dBFS  clipped samples %u  near-silent blocks %.1f%%  NaN %u\n",
               20.0 * log10(peak + 1e-9), 20.0 * log10(rms + 1e-9), clip, 100.0 * silent_blocks / (blocks ? blocks : 1), nan_count);
        printf("  speed: %.1fx realtime at %d Hz (%.2fs CPU)\n", seconds / (secs > 0 ? secs : 1e-9), rate, secs);
    }
    printf("SUMMARY seed=%u preset=%s seconds=%.0f events=%u hash=%08x peak_db=%.2f rms_db=%.2f clip=%u nan=%u%s%s\n",
           P.seed, hush_preset_names[preset], seconds, hush_music_event_count(m), hush_music_event_hash(m),
           20.0 * log10(peak + 1e-9), 20.0 * log10(rms + 1e-9), clip, nan_count, out_path ? " out=" : "", out_path ? out_path : "");
    return nan_count ? 3 : 0;
}
