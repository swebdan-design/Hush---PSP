/* HUSH — PSP audio thread. */
#include "audio_out.h"
#include "../../core/hmath.h"
#include <pspkernel.h>
#include <pspaudio.h>
#include <psprtc.h>
#include <string.h>

#define OUT_FRAMES 1024
#define ENG_FRAMES (OUT_FRAMES / 2)

static int16_t __attribute__((aligned(64))) g_out[2][OUT_FRAMES * 2];
static float g_eng[ENG_FRAMES * 2];
static volatile int g_running;
static int g_channel = -1;
static SceUID g_thread = -1;
static HushMusic *g_music;
static Sfx *g_sfx;
static volatile float g_music_gain = 1.0f, g_sfx_gain = 1.0f, g_load;
static volatile uint32_t g_blocks, g_late;
static float g_prevL, g_prevR;

static uint64_t now_us(void) { u64 t; sceRtcGetCurrentTick(&t); return (uint64_t)t; }

static int audio_thread(SceSize args, void *argp) {
    (void)args; (void)argp;
    int cur = 0;
    const float block_us = (float)OUT_FRAMES * 1e6f / (float)HUSH_OUT_RATE;
    while (g_running) {
        uint64_t t0 = now_us();
        hush_music_render(g_music, g_eng, ENG_FRAMES);
        float mg = g_music_gain;
        if (mg != 1.0f) for (int i = 0; i < ENG_FRAMES * 2; i++) g_eng[i] *= mg;
        if (g_sfx) {
            g_sfx->master = 0.7f * g_sfx_gain;
            sfx_render(g_sfx, g_eng, ENG_FRAMES);
        }
        /* 2x linear-interpolation upsampling to the 44.1 kHz hardware rate */
        int16_t *o = g_out[cur];
        float pl = g_prevL, pr = g_prevR;
        for (int i = 0; i < ENG_FRAMES; i++) {
            float l = g_eng[2 * i], r = g_eng[2 * i + 1];
            float ml = (pl + l) * 0.5f, mr = (pr + r) * 0.5f;
            ml = h_clampf(ml, -1.0f, 1.0f) * 32767.0f; mr = h_clampf(mr, -1.0f, 1.0f) * 32767.0f;
            float l2 = h_clampf(l, -1.0f, 1.0f) * 32767.0f, r2 = h_clampf(r, -1.0f, 1.0f) * 32767.0f;
            o[4 * i + 0] = (int16_t)ml; o[4 * i + 1] = (int16_t)mr;
            o[4 * i + 2] = (int16_t)l2; o[4 * i + 3] = (int16_t)r2;
            pl = l; pr = r;
        }
        g_prevL = pl; g_prevR = pr;
        uint64_t t1 = now_us();
        float load = (float)(t1 - t0) / block_us;
        g_load = g_load * 0.95f + load * 0.05f;
        if (load > 0.9f) g_late++;
        g_blocks++;
        sceAudioOutputPannedBlocking(g_channel, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX, o);
        cur ^= 1;
    }
    return 0;
}

int audio_out_start(HushMusic *m, Sfx *s) {
    g_music = m;
    g_sfx = s;
    g_channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, OUT_FRAMES, PSP_AUDIO_FORMAT_STEREO);
    if (g_channel < 0) return 0;
    g_running = 1;
    g_thread = sceKernelCreateThread("hush_audio", audio_thread, 0x10, 0x10000, PSP_THREAD_ATTR_USER, NULL);
    if (g_thread < 0) { g_running = 0; sceAudioChRelease(g_channel); return 0; }
    sceKernelStartThread(g_thread, 0, NULL);
    return 1;
}

void audio_out_stop(void) {
    if (!g_running) return;
    g_running = 0;
    sceKernelWaitThreadEnd(g_thread, NULL);
    sceKernelDeleteThread(g_thread);
    sceAudioChRelease(g_channel);
}

void audio_out_set_gain(float music, float sfx) { g_music_gain = music; g_sfx_gain = sfx; }
float audio_out_load(void) { return g_load; }
uint32_t audio_out_underruns(void) { return g_late; }
uint32_t audio_out_blocks(void) { return g_blocks; }
