/* HUSH — effects: dub delay, FDN ambience, chorus, radio degradation, master stage.
 * All buffers are static (no allocation on the audio path). */
#ifndef HUSH_FX_H
#define HUSH_FX_H

#include <stdint.h>
#include "dsp.h"

#define DLY_SIZE 65536
#define DLY_MASK (DLY_SIZE - 1)

typedef struct {
    float bufL[DLY_SIZE], bufR[DLY_SIZE];
    uint32_t w;
    float time_cur, time_target;   /* samples */
    float fb, lp_coef, hp_coef;
    float lpL, lpR, hpL, hpR;
    float wow_ph, wow_depth;
} DubDelay;
void delay_init(DubDelay *d);
/* in: send buffers. adds wet output to outL/outR. */
void delay_process(DubDelay *d, const float *inL, const float *inR, float *outL, float *outR, float *revSend, float rev_amt, int n);

#define REV_LINES 4
#define REV_MAX 8192
#define AP_MAX 1024
typedef struct {
    float line[REV_LINES][REV_MAX];
    int len[REV_LINES];
    uint32_t w;
    float damp[REV_LINES];
    float g[REV_LINES];
    float damp_coef;
    float ap[2][AP_MAX];
    int ap_len[2], ap_w[2];
    float size, decay;
} Reverb;
void reverb_init(Reverb *r);
void reverb_set(Reverb *r, float size, float decay_s, float damp);
void reverb_process(Reverb *r, const float *in, float *outL, float *outR, float wet, int n);

#define CH_SIZE 2048
typedef struct {
    float bufL[CH_SIZE], bufR[CH_SIZE];
    uint32_t w;
    float ph;
} Chorus;
void chorus_init(Chorus *c);
void chorus_process(Chorus *c, float *L, float *R, float rate, float depth, float mix, int n);

typedef struct {
    float holdL, holdR, cnt;
    Svf hpL, hpR, lpL, lpR;
    uint32_t noise;
    float crackle;
} Radio;
void radio_init(Radio *r);
/* amount 0..1: band-limit, quantise, decimate and crackle. */
void radio_process(Radio *r, float *L, float *R, float amount, int n);

typedef struct {
    float dcxL, dcyL, dcxR, dcyR;
    uint32_t noise;
    float hiss_lp;
} Master;
void master_init(Master *m);
void master_process(Master *m, float *L, float *R, float gain, float hiss, int n);

#endif
