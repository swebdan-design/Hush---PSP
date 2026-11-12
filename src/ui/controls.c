/* HUSH — control schemes: button names per action, [TOKEN] substitution in
 * story/HUD text, and the on-screen control hint panel. */
#include "app.h"
#include <string.h>
#include <stdio.h>

typedef struct { const char *token; const char *face; const char *classic; char glyph_face, glyph_classic; } CtlName;
static const CtlName names[] = {
    { "[UP]",    "TRIANGLE",    "D-PAD UP",   'T', 'u' },
    { "[DOWN]",  "CROSS",       "D-PAD DOWN", 'X', 'd' },
    { "[BOOST]", "SQUARE",      "CROSS",      'S', 'X' },
    { "[BRAKE]", "D-PAD DOWN",  "SQUARE",     'd', 'S' },
    { "[SCAN]",  "CIRCLE",      "TRIANGLE",   'O', 'T' },
    { "[EMP]",   "CIRCLE",      "CIRCLE",     'O', 'O' },
};

const char *ctl_name(int action) {
    int face = g_app.prog.control_scheme == CTL_FACE;
    return face ? names[action].face : names[action].classic;
}

char ctl_glyph(int action) {
    int face = g_app.prog.control_scheme == CTL_FACE;
    return face ? names[action].glyph_face : names[action].glyph_classic;
}

const char *ctl_subst(const char *in, char *buf, int n) {
    if (!in) return "";
    int o = 0;
    for (const char *p = in; *p && o < n - 1;) {
        int hit = 0;
        if (*p == '[') {
            for (int k = 0; k < (int)(sizeof(names) / sizeof(names[0])); k++) {
                size_t L = strlen(names[k].token);
                if (!strncmp(p, names[k].token, L)) {
                    const char *r = ctl_name(k);
                    while (*r && o < n - 1) buf[o++] = *r++;
                    p += L;
                    hit = 1;
                    break;
                }
            }
        }
        if (!hit) buf[o++] = *p++;
    }
    buf[o] = 0;
    return buf;
}

/* compact legend of the flight controls, shown at the start of a flight and when paused */
void ctl_hint_panel(float cx_mid, float y, float alpha) {
    static const char *const label[] = { "UP", "DOWN", "BOOST", "BRAKE", "SCAN/EMP" };
    static const int act[] = { CTL_ACT_UP, CTL_ACT_DOWN, CTL_ACT_BOOST, CTL_ACT_BRAKE, CTL_ACT_SCAN };
    float w = 0;
    for (int i = 0; i < 5; i++) w += 18 + gfx_text_width(FONT_SMALL, label[i]) + 8;
    w += gfx_text_width(FONT_SMALL, "L/R TURN") + 4;
    float x = cx_mid - w * 0.5f;
    gfx_rect(x - 8, y - 2, w + 16, 18, RGBA(4, 7, 10, (int)(200 * alpha)));
    float cx = x;
    for (int i = 0; i < 5; i++) {
        cx += ui_glyph(cx, y, ctl_glyph(act[i]), alpha) + 3;
        cx += gfx_text(FONT_SMALL, cx, y, label[i], col_alpha(RGBA(200, 210, 220, 255), alpha), ALIGN_LEFT) + 8;
    }
    gfx_text(FONT_SMALL, cx + 4, y, "L/R TURN", col_alpha(RGBA(200, 210, 220, 255), alpha), ALIGN_LEFT);
}
