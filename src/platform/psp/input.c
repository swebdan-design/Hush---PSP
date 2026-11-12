/* HUSH — input. */
#include "input.h"
#include "../../core/hmath.h"
#include <string.h>

void input_init(Input *in) {
    memset(in, 0, sizeof(*in));
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
}

void input_update(Input *in, float dt) {
    SceCtrlData pad;
    sceCtrlReadBufferPositive(&pad, 1);
    uint32_t b = pad.Buttons & 0xFFFF;
    float x = ((float)pad.Lx - 128.0f) / 127.0f, y = -((float)pad.Ly - 128.0f) / 127.0f;
    if (in->scripted) { b |= in->s_held; if (in->s_lx != 0 || in->s_ly != 0) { x = in->s_lx; y = in->s_ly; } }
    /* radial dead zone with rescale */
    float mag = sqrtf(x * x + y * y);
    const float dz = 0.22f;
    if (mag < dz) { x = 0; y = 0; }
    else { float k = h_minf((mag - dz) / (1.0f - dz), 1.0f) / mag; x *= k; y *= k; }
    in->lx = h_clampf(x, -1.0f, 1.0f);
    in->ly = h_clampf(y, -1.0f, 1.0f);
    in->pressed = b & ~in->held;
    in->released = in->held & ~b;
    in->held = b;
    in->repeat = 0;
    for (int i = 0; i < 16; i++) {
        uint32_t m = 1u << i;
        if (b & m) {
            float before = in->hold_time[i];
            in->hold_time[i] += dt;
            float t = in->hold_time[i];
            if (t > 0.38f) {
                float a = before - 0.38f, c = t - 0.38f;
                if (a < 0 || (int)(a / 0.07f) != (int)(c / 0.07f)) in->repeat |= m;
            }
        } else in->hold_time[i] = 0;
    }
}

int input_rep(const Input *in, uint32_t b) { return ((in->pressed | in->repeat) & b) != 0; }
