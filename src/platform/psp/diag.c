/* HUSH — hardware diagnostics (see diag.h).
 * Lines collect in RAM and reach the Memory Stick only at quiet moments (menus, pause, every 30 s,
 * or when the buffer fills): on real hardware each Memory Stick write costs tens of milliseconds,
 * so writing during gameplay made the frame rate collapse. */
#include "diag.h"
#include <pspkernel.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <pspfpu.h>
#include <psprtc.h>

#define LOG_PATH "hush_log.txt"
#define LOG_MAX_BYTES (256 * 1024)   /* stop logging rather than fill the Memory Stick */
#define BUF_BYTES (16 * 1024)

int g_diag_fine;
static int g_frame, g_bytes, g_fpu_reports;
static char g_buf[BUF_BYTES];
static int g_n;

void diag_flush(void) {
    if (!g_n) return;
    if (g_bytes + g_n <= LOG_MAX_BYTES) {
        FILE *f = fopen(LOG_PATH, "a");
        if (f) { fwrite(g_buf, 1, (size_t)g_n, f); fclose(f); g_bytes += g_n; }
    }
    g_n = 0;
}

void diag_init(void) {
    FILE *f = fopen(LOG_PATH, "w");   /* a fresh log for each run */
    if (f) fclose(f);
    g_bytes = 0; g_n = 0;
}

void diag_log(const char *fmt, ...) {
    char line[256];
    int k = snprintf(line, sizeof(line), "[%5d] ", g_frame);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line + k, sizeof(line) - (size_t)k - 2, fmt, ap);
    va_end(ap);
    strcat(line, "\n");
    int n = (int)strlen(line);
    if (g_n + n > BUF_BYTES) return;   /* full: drop until the next flush */
    memcpy(g_buf + g_n, line, (size_t)n);
    g_n += n;
}

/* per-stage CPU time: the time between two marks is charged to the earlier stage */
#define MAX_STAGES 48
static const char *g_st_name[MAX_STAGES];
static uint32_t g_st_us[MAX_STAGES];
static int g_nst = 0, g_cur = -1, g_st_frames;
static u64 g_last_tick;

static int stage_index(const char *name) {
    for (int i = 0; i < g_nst; i++) if (g_st_name[i] == name) return i;
    if (g_nst >= MAX_STAGES) return -1;
    g_st_name[g_nst] = name;
    g_st_us[g_nst] = 0;
    return g_nst++;
}

void diag_stage_report(char *out, int cap) {
    /* top stages by average ms per frame since the last report */
    out[0] = 0;
    if (g_st_frames <= 0) return;
    int used[MAX_STAGES] = { 0 }, len = 0;
    for (int k = 0; k < 7; k++) {
        int best = -1;
        for (int i = 0; i < g_nst; i++) if (!used[i] && (best < 0 || g_st_us[i] > g_st_us[best])) best = i;
        if (best < 0 || g_st_us[best] == 0) break;
        used[best] = 1;
        len += snprintf(out + len, (size_t)(cap - len), "%s%s %.1f", k ? ", " : "", g_st_name[best], (float)g_st_us[best] / 1000.0f / (float)g_st_frames);
        if (len >= cap - 1) break;
    }
    for (int i = 0; i < g_nst; i++) g_st_us[i] = 0;
    g_st_frames = 0;
}

void diag_mark(const char *stage) {
    u64 now;
    sceRtcGetCurrentTick(&now);
    if (g_cur >= 0 && g_last_tick) g_st_us[g_cur] += (uint32_t)(now - g_last_tick);
    g_cur = stage_index(stage);
    g_last_tick = now;
    /* traps are off, but the FPU still raises sticky flags: note where invalid/divide/overflow first happen */
    uint32_t f = pspFpuGetFlags() & (PSP_FPU_EXCEPTION_OVERFLOW | PSP_FPU_EXCEPTION_DIVBYZERO | PSP_FPU_EXCEPTION_INVALIDOP);
    if (f) {
        if (g_fpu_reports < 24) {
            g_fpu_reports++;
            diag_log("FPU %s%s%s before stage '%s'", (f & PSP_FPU_EXCEPTION_INVALIDOP) ? "INVALID " : "", (f & PSP_FPU_EXCEPTION_DIVBYZERO) ? "DIV0 " : "", (f & PSP_FPU_EXCEPTION_OVERFLOW) ? "OVERFLOW " : "", stage);
        }
        pspFpuClearFlags(PSP_FPU_EXCEPTION_ALL);
    }
}

void diag_arm(int frames, const char *why) { (void)frames; (void)why; }

void diag_frame(void) { g_frame++; g_st_frames++; }
