/* HUSH — PSP controls: digital buttons with edge detection and menu auto-repeat,
 * analogue nub with radial dead zone. */
#ifndef HUSH_INPUT_H
#define HUSH_INPUT_H

#include <stdint.h>
#include <pspctrl.h>

typedef struct {
    uint32_t held, pressed, released;
    float lx, ly;              /* -1..1, ly positive = nub pushed up */
    float hold_time[32];
    uint32_t repeat;           /* buttons that auto-repeated this frame */
    /* scripted override for the automated validation build */
    int scripted;
    uint32_t s_held; float s_lx, s_ly;
} Input;

void input_init(Input *in);
void input_update(Input *in, float dt);
int input_rep(const Input *in, uint32_t b);   /* pressed or auto-repeat */

#endif
