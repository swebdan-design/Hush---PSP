/* HUSH — hardware diagnostics: a crash-surviving breadcrumb log (hush_log.txt beside the EBOOT).
 * Every line is appended and the file closed again, so the last line survives a crash. Fine-grained
 * stage markers are written only for the first frames after a screen change or district load. */
#ifndef HUSH_DIAG_H
#define HUSH_DIAG_H

void diag_init(void);
void diag_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* stage marker: written only while fine tracing is armed */
void diag_mark(const char *stage);
void diag_arm(int frames, const char *why);   /* trace stages for the next N frames */
void diag_frame(void);                        /* call once per frame */
void diag_flush(void);                        /* write buffered lines to the Memory Stick (slow: quiet moments only) */
extern int g_diag_fine;

#endif
