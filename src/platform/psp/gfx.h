/* HUSH — PSP graphics layer on top of libGU: frame management, VRAM layout,
 * camera/projection matrices, textures, and 2D drawing primitives (rects,
 * lines, text) used by every screen. */
#ifndef HUSH_GFX_H
#define HUSH_GFX_H

#include <stdint.h>
#include <pspgu.h>
#include "../../core/hmath.h"

#define SCR_W 480
#define SCR_H 272

#define RGBA(r, g, b, a) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(g) << 8 | (uint32_t)(r))
static inline uint32_t col_alpha(uint32_t c, float a) {
    uint32_t al = (uint32_t)(h_clampf(a, 0.0f, 1.0f) * (float)(c >> 24));
    return (c & 0x00FFFFFFu) | (al << 24);
}
static inline uint32_t col_scale(uint32_t c, float k) {
    k = h_clampf(k, 0.0f, 4.0f);
    uint32_t r = (uint32_t)h_minf((float)(c & 0xFF) * k, 255.0f);
    uint32_t g = (uint32_t)h_minf((float)((c >> 8) & 0xFF) * k, 255.0f);
    uint32_t b = (uint32_t)h_minf((float)((c >> 16) & 0xFF) * k, 255.0f);
    return (c & 0xFF000000u) | (b << 16) | (g << 8) | r;
}
static inline uint32_t col_lerp(uint32_t a, uint32_t b, float t) {
    t = h_clampf(t, 0.0f, 1.0f);
    uint32_t o = 0;
    for (int s = 0; s < 32; s += 8) {
        float x = (float)((a >> s) & 0xFF), y = (float)((b >> s) & 0xFF);
        o |= ((uint32_t)(x + (y - x) * t) & 0xFF) << s;
    }
    return o;
}

typedef struct { float u, v; uint32_t color; float x, y, z; } VtxTCP;   /* GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF */
typedef struct { uint32_t color; float x, y, z; } VtxCP;                /* GU_COLOR_8888|GU_VERTEX_32BITF */
#define VT_TCP (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF)
#define VT_CP (GU_COLOR_8888 | GU_VERTEX_32BITF)

typedef struct { const void *data; int w, h, psm; } Tex;

typedef struct { float m[16]; } Mat4;  /* column-major, same layout as ScePspFMatrix4 */

void gfx_init(void);
void gfx_term(void);
void gfx_begin(uint32_t clear_color);
/* Finishes the list, waits for the GE, flips at a stable 30 Hz (or 60 when allowed). */
void gfx_end(int target_hz);
void *gfx_vram_alloc(int bytes);
int gfx_vram_used(void);
/* Copies a texture into VRAM if there is room (faster sampling); returns the Tex to use. */
Tex gfx_tex_to_vram(Tex t);
void gfx_bind(const Tex *t, int linear, int repeat);
void gfx_no_tex(void);

/* 3D */
void gfx_set_camera(V3 eye, V3 target, V3 up, float fov_deg, float znear, float zfar);
void gfx_model_identity(void);
void gfx_model(const Mat4 *m);
Mat4 mat_trs(V3 t, float yaw, float pitch, float roll, float s);
Mat4 mat_mul(const Mat4 *a, const Mat4 *b);
/* Projects a world point to screen; returns 0 if behind the camera. */
int gfx_project(V3 p, float *sx, float *sy, float *depth);
V3 gfx_cam_pos(void);
V3 gfx_cam_fwd(void);
V3 gfx_cam_right(void);
V3 gfx_cam_up(void);
float gfx_cam_fov(void);
float gfx_cam_znear(void);

/* state helpers */
void gfx_mode_2d(void);       /* no depth, alpha blend */
void gfx_mode_3d(void);
void gfx_blend_alpha(void);
void gfx_blend_add(void);
void gfx_fog(int on, float near_d, float far_d, uint32_t color);

/* 2D primitives in screen pixels */
void gfx_rect(float x, float y, float w, float h, uint32_t c);
void gfx_rect_grad(float x, float y, float w, float h, uint32_t top, uint32_t bottom);
void gfx_rect_outline(float x, float y, float w, float h, uint32_t c);
void gfx_line(float x0, float y0, float x1, float y1, uint32_t c);
void gfx_lines(const float *xy, int n_points, uint32_t c, int closed);
void gfx_tri(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t c);
void gfx_image(const Tex *t, float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t c);

/* text */
enum { FONT_SMALL = 0, FONT_LARGE = 1 };
enum { ALIGN_LEFT = 0, ALIGN_CENTER = 1, ALIGN_RIGHT = 2 };
void gfx_fonts_init(void);
float gfx_text(int font, float x, float y, const char *s, uint32_t c, int align);
float gfx_text_width(int font, const char *s);
int gfx_text_height(int font);
/* word-wrapped text; returns number of lines drawn (max_lines) */
int gfx_text_wrap(int font, float x, float y, float w, const char *s, uint32_t c, int max_lines, int max_chars);
void gfx_logo(float cx, float y, float alpha);

/* timing / diagnostics */
uint64_t gfx_ticks_us(void);
extern int gfx_frame_count;
extern int gfx_list_peak, gfx_list_last;   /* display-list bytes used (of 1 MB) */
extern float gfx_last_cpu_ms, gfx_last_gpu_wait_ms;

/* debug screenshot of the current display buffer to a 24-bit BMP */
int gfx_screenshot(const char *path);

#endif
