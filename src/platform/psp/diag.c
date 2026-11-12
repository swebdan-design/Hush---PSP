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

void diag_mark(const char *stage) {
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

void diag_frame(void) { g_frame++; }
