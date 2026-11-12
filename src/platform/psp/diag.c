/* HUSH — hardware diagnostics (see diag.h). */
#include "diag.h"
#include <pspkernel.h>
#include <pspge.h>
#include <pspsysmem.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <pspfpu.h>

#define LOG_PATH "hush_log.txt"
#define LOG_MAX_BYTES (512 * 1024)   /* stop logging rather than fill the Memory Stick */

int g_diag_fine;
static int g_frames_left, g_frame, g_bytes;

static void put(const char *s) {
    int n = (int)strlen(s);
    if (g_bytes + n > LOG_MAX_BYTES) return;
    FILE *f = fopen(LOG_PATH, "a");
    if (!f) return;
    fputs(s, f);
    fclose(f);   /* closing flushes to the Memory Stick: the line survives a crash */
    g_bytes += n;
}

void diag_init(void) {
    FILE *f = fopen(LOG_PATH, "w");   /* a fresh log for each run */
    if (f) fclose(f);
    g_bytes = 0;
}

void diag_log(const char *fmt, ...) {
    char buf[256];
    int k = snprintf(buf, sizeof(buf), "[%5d] ", g_frame);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + k, sizeof(buf) - (size_t)k - 2, fmt, ap);
    va_end(ap);
    strcat(buf, "\n");
    put(buf);
}

static int g_fpu_reports;

void diag_mark(const char *stage) {
    /* with traps off, the FPU still raises sticky flags: report where invalid/divide/overflow first happen */
    uint32_t f = pspFpuGetFlags() & (PSP_FPU_EXCEPTION_OVERFLOW | PSP_FPU_EXCEPTION_DIVBYZERO | PSP_FPU_EXCEPTION_INVALIDOP);
    if (f) {
        if (g_fpu_reports < 24) {
            g_fpu_reports++;
            diag_log("FPU %s%s%s before stage '%s'", (f & PSP_FPU_EXCEPTION_INVALIDOP) ? "INVALID " : "", (f & PSP_FPU_EXCEPTION_DIVBYZERO) ? "DIV0 " : "", (f & PSP_FPU_EXCEPTION_OVERFLOW) ? "OVERFLOW " : "", stage);
        }
        pspFpuClearFlags(PSP_FPU_EXCEPTION_ALL);
    }
    if (!g_diag_fine) return;
    char buf[64];
    snprintf(buf, sizeof(buf), "[%5d]   . %s\n", g_frame, stage);
    put(buf);
}

void diag_arm(int frames, const char *why) {
    if (frames > g_frames_left) g_frames_left = frames;
    g_diag_fine = 1;
    diag_log("trace on (%s)", why);
}

void diag_frame(void) {
    g_frame++;
    if (g_frames_left > 0 && --g_frames_left == 0) { g_diag_fine = 0; diag_log("trace off"); }
}
