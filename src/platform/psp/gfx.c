/* HUSH — PSP graphics layer. See gfx.h. */
#include "gfx.h"
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <psprtc.h>
#include <psputils.h>
#include <pspiofilemgr.h>
#include <string.h>
#include <stdio.h>

#include "../../assets/generated/font_small.h"
#include "../../assets/generated/font_large.h"
#include "../../assets/generated/logo.h"
#include "../../assets/generated/credit.h"

#include "diag.h"

#define BUF_W 512
#define LIST_BYTES (1024 * 1024)

static unsigned int __attribute__((aligned(64))) g_list[LIST_BYTES / 4];
static uint32_t g_vram_next;          /* offset from VRAM base */
static void *g_draw_buf;
static unsigned int g_last_vc, g_need = 2;
int gfx_frame_count;
float gfx_last_cpu_ms, gfx_last_gpu_wait_ms;
static uint64_t g_frame_start;

static Mat4 g_view, g_proj, g_viewproj;
static V3 g_cam_eye, g_cam_fwd, g_cam_right, g_cam_up;
static float g_cam_fov, g_cam_znear = 1.0f;

static Tex g_font_tex[2], g_logo_tex, g_credit_tex;
static const Tex *g_bound;

uint64_t gfx_ticks_us(void) {
    u64 t;
    sceRtcGetCurrentTick(&t);
    return (uint64_t)t;
}

void *gfx_vram_alloc(int bytes) {
    uint32_t size = sceGeEdramGetSize();
    uint32_t at = (g_vram_next + 15u) & ~15u;
    if (at + (uint32_t)bytes > size) return NULL;
    g_vram_next = at + (uint32_t)bytes;
    return (void *)((uint32_t)sceGeEdramGetAddr() + at);
}

int gfx_vram_used(void) { return (int)g_vram_next; }

Tex gfx_tex_to_vram(Tex t) {
    int bpp = t.psm == GU_PSM_8888 ? 4 : 2;
    int bytes = t.w * t.h * bpp;
    void *dst = gfx_vram_alloc(bytes);
    if (!dst) return t;
    memcpy(dst, t.data, (size_t)bytes);
    sceKernelDcacheWritebackAll();
    t.data = dst;
    return t;
}

void gfx_init(void) {
    g_vram_next = 0;
    /* framebuffers are addressed relative to VRAM start */
    /* 16-bit (RGB565) colour buffers, dithered: half the framebuffer bandwidth of 8888 for every blended pixel,
     * and 544 KB of VRAM freed so every texture is sampled from VRAM instead of main RAM */
    uint32_t fb_bytes = BUF_W * SCR_H * 2;
    void *fb0 = (void *)0;
    void *fb1 = (void *)fb_bytes;
    void *zb = (void *)(fb_bytes * 2);
    g_vram_next = fb_bytes * 2 + BUF_W * SCR_H * 2;
    g_draw_buf = fb0;

    sceGuInit();
    sceGuStart(GU_DIRECT, g_list);
    sceGuDrawBuffer(GU_PSM_5650, fb0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, fb1, BUF_W);
    sceGuDepthBuffer(zb, BUF_W);
    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDepthRange(65535, 0);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuEnable(GU_DEPTH_TEST);
    sceGuFrontFace(GU_CW);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_CLIP_PLANES);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    {   /* ordered dither hides 565 banding in fog and glows (never pass NULL here) */
        static ScePspIMatrix4 dith = { { -4, 0, -3, 1 }, { 2, -2, 3, -1 }, { -3, 1, -4, 0 }, { 3, -1, 2, -2 } };
        sceGuSetDither(&dith);
        sceGuEnable(GU_DITHER);
    }
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
    g_last_vc = sceDisplayGetVcount();
    gfx_fonts_init();
}

void gfx_term(void) { sceGuTerm(); }

static int g_pending;   /* a submitted frame the GE may still be drawing */

void gfx_present(void) {
    if (!g_pending) return;
    g_pending = 0;
    diag_mark("gpu wait");
    uint64_t t0 = gfx_ticks_us();
    sceGuSync(0, 0);
    uint64_t t1 = gfx_ticks_us();
    gfx_last_gpu_wait_ms = (float)(t1 - t0) / 1000.0f;
    unsigned int vc = sceDisplayGetVcount();
    while (vc - g_last_vc < g_need) { sceDisplayWaitVblankStart(); vc = sceDisplayGetVcount(); }
    g_last_vc = vc;
    g_draw_buf = sceGuSwapBuffers();
    gfx_frame_count++;
}

void gfx_begin(uint32_t clear_color) {
    gfx_present();   /* never start a list while the previous one may still run */
    g_frame_start = gfx_ticks_us();
    sceGuStart(GU_DIRECT, g_list);
    sceGuClearColor(clear_color);
    sceGuClearDepth(0);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT | GU_FAST_CLEAR_BIT);
    g_bound = NULL;
}

int gfx_list_peak, gfx_list_last;

/* Closes the frame's list. The GE has been executing it while it was built (GU_DIRECT) and finishes the tail
 * on its own; the caller can do CPU work (the next game update) before gfx_present waits and flips. */
void gfx_submit(int target_hz) {
    gfx_list_last = sceGuCheckList();
    if (gfx_list_last > gfx_list_peak) gfx_list_peak = gfx_list_last;
    sceGuFinish();
    gfx_last_cpu_ms = (float)(gfx_ticks_us() - g_frame_start) / 1000.0f;
    g_need = target_hz >= 60 ? 1u : 2u;
    g_pending = 1;
}

void gfx_end(int target_hz) {
    gfx_submit(target_hz);
    gfx_present();
}

void gfx_bind(const Tex *t, int linear, int repeat) {
    sceGuEnable(GU_TEXTURE_2D);
    if (g_bound != t) {
        sceGuTexMode(t->psm, 0, 0, 0);
        sceGuTexImage(0, t->w, t->h, t->w, t->data);
        sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
        g_bound = t;
    }
    sceGuTexFilter(linear ? GU_LINEAR : GU_NEAREST, linear ? GU_LINEAR : GU_NEAREST);
    sceGuTexWrap(repeat ? GU_REPEAT : GU_CLAMP, repeat ? GU_REPEAT : GU_CLAMP);
}

void gfx_no_tex(void) { sceGuDisable(GU_TEXTURE_2D); }

/* ---------------- matrices ---------------- */

static Mat4 mat_identity(void) { Mat4 m; memset(&m, 0, sizeof(m)); m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1.0f; return m; }

Mat4 mat_mul(const Mat4 *a, const Mat4 *b) {
    Mat4 r;
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a->m[k * 4 + rr] * b->m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}

Mat4 mat_trs(V3 t, float yaw, float pitch, float roll, float s) {
    /* R = Ry(yaw) * Rx(pitch) * Rz(roll) */
    float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch), cr = cosf(roll), sr = sinf(roll);
    Mat4 m = mat_identity();
    /* columns */
    m.m[0] = (cy * cr + sy * sp * sr) * s; m.m[1] = (cp * sr) * s; m.m[2] = (-sy * cr + cy * sp * sr) * s;
    m.m[4] = (-cy * sr + sy * sp * cr) * s; m.m[5] = (cp * cr) * s; m.m[6] = (sy * sr + cy * sp * cr) * s;
    m.m[8] = (sy * cp) * s; m.m[9] = (-sp) * s; m.m[10] = (cy * cp) * s;
    m.m[12] = t.x; m.m[13] = t.y; m.m[14] = t.z;
    return m;
}

void gfx_set_camera(V3 eye, V3 target, V3 up, float fov_deg, float znear, float zfar) {
    V3 f = v3_norm(v3_sub(target, eye));
    V3 s = v3_norm(v3_cross(f, up));
    V3 u = v3_cross(s, f);
    g_cam_eye = eye; g_cam_fwd = f; g_cam_right = s; g_cam_up = u; g_cam_fov = fov_deg; g_cam_znear = znear;
    Mat4 v = mat_identity();
    v.m[0] = s.x; v.m[4] = s.y; v.m[8] = s.z;
    v.m[1] = u.x; v.m[5] = u.y; v.m[9] = u.z;
    v.m[2] = -f.x; v.m[6] = -f.y; v.m[10] = -f.z;
    v.m[12] = -v3_dot(s, eye); v.m[13] = -v3_dot(u, eye); v.m[14] = v3_dot(f, eye);
    float angle = fov_deg * 0.5f * (H_PI / 180.0f);
    float cot = cosf(angle) / sinf(angle);
    float aspect = (float)SCR_W / (float)SCR_H;
    float dz = znear - zfar;
    Mat4 p;
    memset(&p, 0, sizeof(p));
    p.m[0] = cot / aspect;
    p.m[5] = cot;
    p.m[10] = (zfar + znear) / dz;
    p.m[11] = -1.0f;
    p.m[14] = 2.0f * zfar * znear / dz;
    g_view = v; g_proj = p;
    g_viewproj = mat_mul(&p, &v);
    sceGuSetMatrix(GU_PROJECTION, (const ScePspFMatrix4 *)&p);
    sceGuSetMatrix(GU_VIEW, (const ScePspFMatrix4 *)&v);
    gfx_model_identity();
}

void gfx_model_identity(void) { Mat4 m = mat_identity(); sceGuSetMatrix(GU_MODEL, (const ScePspFMatrix4 *)&m); }
void gfx_model(const Mat4 *m) { sceGuSetMatrix(GU_MODEL, (const ScePspFMatrix4 *)m); }

int gfx_project(V3 p, float *sx, float *sy, float *depth) {
    const float *m = g_viewproj.m;
    float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (depth) *depth = w;
    if (w < 0.1f) return 0;
    *sx = (x / w * 0.5f + 0.5f) * SCR_W;
    *sy = (1.0f - (y / w * 0.5f + 0.5f)) * SCR_H;
    return 1;
}
V3 gfx_cam_pos(void) { return g_cam_eye; }
V3 gfx_cam_fwd(void) { return g_cam_fwd; }
V3 gfx_cam_right(void) { return g_cam_right; }
V3 gfx_cam_up(void) { return g_cam_up; }
float gfx_cam_fov(void) { return g_cam_fov; }
float gfx_cam_znear(void) { return g_cam_znear; }

/* ---------------- state ---------------- */

void gfx_mode_2d(void) {
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_CULL_FACE);
    gfx_blend_alpha();
}
void gfx_mode_3d(void) {
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_FALSE);
    gfx_blend_alpha();
}
void gfx_blend_alpha(void) { sceGuEnable(GU_BLEND); sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0); }
void gfx_blend_add(void) { sceGuEnable(GU_BLEND); sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFFFF); }
void gfx_fog(int on, float n, float f, uint32_t c) {
    if (on) { sceGuFog(n, f, c & 0x00FFFFFF); sceGuEnable(GU_FOG); }
    else sceGuDisable(GU_FOG);
}

/* ---------------- 2D ---------------- */

/* Small per-call vertex blocks are written through the uncached alias of display-list memory (gfx_mem_uc): they
 * reach RAM directly, so the GE never sees stale data and no cache writeback (a syscall) is needed per call. */

void gfx_rect(float x, float y, float w, float h, uint32_t c) {
    gfx_no_tex();
    VtxCP *v = (VtxCP *)gfx_mem_uc(2 * sizeof(VtxCP));
    v[0].color = c; v[0].x = x; v[0].y = y; v[0].z = 0;
    v[1].color = c; v[1].x = x + w; v[1].y = y + h; v[1].z = 0;
    sceGuDrawArray(GU_SPRITES, VT_CP | GU_TRANSFORM_2D, 2, 0, v);
}

void gfx_rect_grad(float x, float y, float w, float h, uint32_t top, uint32_t bottom) {
    gfx_no_tex();
    VtxCP *v = (VtxCP *)gfx_mem_uc(4 * sizeof(VtxCP));
    v[0].color = top; v[0].x = x; v[0].y = y; v[0].z = 0;
    v[1].color = top; v[1].x = x + w; v[1].y = y; v[1].z = 0;
    v[2].color = bottom; v[2].x = x; v[2].y = y + h; v[2].z = 0;
    v[3].color = bottom; v[3].x = x + w; v[3].y = y + h; v[3].z = 0;
    sceGuDrawArray(GU_TRIANGLE_STRIP, VT_CP | GU_TRANSFORM_2D, 4, 0, v);
}

void gfx_line(float x0, float y0, float x1, float y1, uint32_t c) {
    gfx_no_tex();
    VtxCP *v = (VtxCP *)gfx_mem_uc(2 * sizeof(VtxCP));
    v[0].color = c; v[0].x = x0; v[0].y = y0; v[0].z = 0;
    v[1].color = c; v[1].x = x1; v[1].y = y1; v[1].z = 0;
    sceGuDrawArray(GU_LINES, VT_CP | GU_TRANSFORM_2D, 2, 0, v);
}

void gfx_lines(const float *xy, int n, uint32_t c, int closed) {
    if (n < 2) return;
    gfx_no_tex();
    int cnt = n + (closed ? 1 : 0);
    VtxCP *v = (VtxCP *)gfx_mem_uc(cnt * (int)sizeof(VtxCP));
    for (int i = 0; i < cnt; i++) { int k = i % n; v[i].color = c; v[i].x = xy[2 * k]; v[i].y = xy[2 * k + 1]; v[i].z = 0; }
    sceGuDrawArray(GU_LINE_STRIP, VT_CP | GU_TRANSFORM_2D, cnt, 0, v);
}

void gfx_rect_outline(float x, float y, float w, float h, uint32_t c) {
    float p[8] = { x, y, x + w, y, x + w, y + h, x, y + h };
    gfx_lines(p, 4, c, 1);
}

void gfx_tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c) {
    gfx_no_tex();
    VtxCP *v = (VtxCP *)gfx_mem_uc(3 * sizeof(VtxCP));
    v[0].color = c; v[0].x = x0; v[0].y = y0; v[0].z = 0;
    v[1].color = c; v[1].x = x1; v[1].y = y1; v[1].z = 0;
    v[2].color = c; v[2].x = x2; v[2].y = y2; v[2].z = 0;
    sceGuDrawArray(GU_TRIANGLES, VT_CP | GU_TRANSFORM_2D, 3, 0, v);
}

void gfx_image(const Tex *t, float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t c) {
    gfx_bind(t, 1, 0);
    VtxTCP *v = (VtxTCP *)gfx_mem_uc(2 * sizeof(VtxTCP));
    v[0].u = u0; v[0].v = v0; v[0].color = c; v[0].x = x; v[0].y = y; v[0].z = 0;
    v[1].u = u1; v[1].v = v1; v[1].color = c; v[1].x = x + w; v[1].y = y + h; v[1].z = 0;
    sceGuDrawArray(GU_SPRITES, VT_TCP | GU_TRANSFORM_2D, 2, 0, v);
}

/* ---------------- text ---------------- */

typedef struct { const uint16_t (*g)[4]; int line, pad; } FontInfo;
static FontInfo g_fonts[2];

void gfx_fonts_init(void) {
    Tex fs = { font_small_tex, FONT_SMALL_W, FONT_SMALL_H, GU_PSM_4444 };
    Tex fl = { font_large_tex, FONT_LARGE_W, FONT_LARGE_H, GU_PSM_4444 };
    Tex lg = { logo_tex, LOGO_W, LOGO_H, GU_PSM_4444 };
    Tex cr = { credit_tex, CREDIT_W, CREDIT_H, GU_PSM_4444 };
    g_font_tex[0] = gfx_tex_to_vram(fs);
    g_font_tex[1] = gfx_tex_to_vram(fl);
    g_logo_tex = gfx_tex_to_vram(lg);
    g_credit_tex = gfx_tex_to_vram(cr);
    g_fonts[0].g = font_small_glyphs; g_fonts[0].line = FONT_SMALL_LINE; g_fonts[0].pad = FONT_SMALL_PAD;
    g_fonts[1].g = font_large_glyphs; g_fonts[1].line = FONT_LARGE_LINE; g_fonts[1].pad = FONT_LARGE_PAD;
}

int gfx_text_height(int font) { return g_fonts[font & 1].line; }

float gfx_text_width(int font, const char *s) {
    const FontInfo *f = &g_fonts[font & 1];
    float w = 0;
    for (; *s; s++) {
        int c = (unsigned char)*s;
        if (c < 32 || c > 126) c = '?';
        w += f->g[c - 32][3];
    }
    return w;
}

float gfx_text(int font, float x, float y, const char *s, uint32_t col, int align) {
    const FontInfo *f = &g_fonts[font & 1];
    int n = (int)strlen(s);
    if (n == 0) return 0;
    float w = gfx_text_width(font, s);
    if (align == ALIGN_CENTER) x -= w * 0.5f;
    else if (align == ALIGN_RIGHT) x -= w;
    x = (float)(int)(x + 0.5f);
    y = (float)(int)(y + 0.5f);
    gfx_bind(&g_font_tex[font & 1], 0, 0);
    VtxTCP *v = (VtxTCP *)gfx_mem_uc(n * 2 * (int)sizeof(VtxTCP));
    int k = 0;
    float cx = x;
    for (int i = 0; i < n; i++) {
        int c = (unsigned char)s[i];
        if (c < 32 || c > 126) c = '?';
        const uint16_t *g = f->g[c - 32];
        if (c != ' ') {
            v[k].u = g[0]; v[k].v = g[1]; v[k].color = col; v[k].x = cx - (float)f->pad; v[k].y = y; v[k].z = 0; k++;
            v[k].u = g[0] + g[2]; v[k].v = g[1] + f->line; v[k].color = col; v[k].x = cx - (float)f->pad + g[2]; v[k].y = y + f->line; v[k].z = 0; k++;
        }
        cx += g[3];
    }
    if (k) sceGuDrawArray(GU_SPRITES, VT_TCP | GU_TRANSFORM_2D, k, 0, v);
    return w;
}

int gfx_text_wrap(int font, float x, float y, float w, const char *s, uint32_t c, int max_lines, int max_chars) {
    char line[128];
    int lines = 0, shown = 0;
    const char *p = s;
    int lh = gfx_text_height(font);
    while (*p && lines < max_lines) {
        int n = 0, last_space = -1;
        float lw = 0;
        while (p[n] && p[n] != '\n' && n < 127) {
            char t[2] = { p[n], 0 };
            float cw = gfx_text_width(font, t);
            if (lw + cw > w) break;
            if (p[n] == ' ') last_space = n;
            lw += cw;
            n++;
        }
        int take = n;
        if (p[n] && p[n] != '\n' && last_space > 0) take = last_space;
        int vis = take;
        if (max_chars >= 0 && shown + vis > max_chars) vis = max_chars - shown;
        if (vis < 0) vis = 0;
        memcpy(line, p, (size_t)vis);
        line[vis] = 0;
        gfx_text(font, x, y + (float)(lines * lh), line, c, ALIGN_LEFT);
        shown += take;
        lines++;
        p += take;
        while (*p == ' ' || *p == '\n') p++;
        if (max_chars >= 0 && shown >= max_chars) break;
    }
    return lines;
}

void gfx_logo(float cx, float y, float alpha) {
    uint32_t c = col_alpha(RGBA(232, 236, 244, 255), alpha);
    gfx_image(&g_logo_tex, cx - LOGO_W * 0.5f, y, LOGO_W, LOGO_H, 0, 0, LOGO_W, LOGO_H, c);
    gfx_image(&g_credit_tex, cx - CREDIT_W * 0.5f, y + LOGO_H + 4, CREDIT_W, CREDIT_H, 0, 0, CREDIT_W, CREDIT_H, col_alpha(RGBA(190, 200, 215, 255), alpha));
}

/* ---------------- screenshot ---------------- */

int gfx_screenshot(const char *path) {
    void *top; int bufw, fmt;
    sceDisplayGetFrameBuf(&top, &bufw, &fmt, 0);
    if (!top || (fmt != PSP_DISPLAY_PIXEL_FORMAT_8888 && fmt != PSP_DISPLAY_PIXEL_FORMAT_565)) return 0;
    const uint32_t *src = (const uint32_t *)((uint32_t)top | 0x40000000u);
    const uint16_t *src16 = (const uint16_t *)src;
    SceUID fd = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd < 0) return 0;
    uint8_t hdr[54];
    memset(hdr, 0, sizeof(hdr));
    uint32_t row = SCR_W * 3, size = row * SCR_H;
    hdr[0] = 'B'; hdr[1] = 'M';
    uint32_t fsz = 54 + size;
    memcpy(hdr + 2, &fsz, 4);
    uint32_t off = 54; memcpy(hdr + 10, &off, 4);
    uint32_t ih = 40; memcpy(hdr + 14, &ih, 4);
    int32_t w = SCR_W, h = SCR_H; memcpy(hdr + 18, &w, 4); memcpy(hdr + 22, &h, 4);
    uint16_t planes = 1, bpp = 24; memcpy(hdr + 26, &planes, 2); memcpy(hdr + 28, &bpp, 2);
    memcpy(hdr + 34, &size, 4);
    sceIoWrite(fd, hdr, 54);
    static uint8_t line[SCR_W * 3];
    for (int y = SCR_H - 1; y >= 0; y--) {
        for (int x = 0; x < SCR_W; x++) {
            if (fmt == PSP_DISPLAY_PIXEL_FORMAT_565) {
                uint32_t p = src16[y * bufw + x];
                uint32_t r = p & 31, g = (p >> 5) & 63, b = (p >> 11) & 31;
                line[x * 3 + 0] = (uint8_t)((b << 3) | (b >> 2));
                line[x * 3 + 1] = (uint8_t)((g << 2) | (g >> 4));
                line[x * 3 + 2] = (uint8_t)((r << 3) | (r >> 2));
                continue;
            }
            uint32_t p = src[y * bufw + x];
            line[x * 3 + 0] = (uint8_t)(p >> 16);
            line[x * 3 + 1] = (uint8_t)(p >> 8);
            line[x * 3 + 2] = (uint8_t)p;
        }
        sceIoWrite(fd, line, row);
    }
    sceIoClose(fd);
    return 1;
}
