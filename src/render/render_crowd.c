/* HUSH — pedestrians: articulated low-poly figures (legs swing with the walk
 * cycle), coarser figures further away, umbrellas with glowing neon rims.
 * Geometry is transformed on the CPU into one batch per frame. */
#include "render_crowd.h"
#include "render_world.h"
#include <pspgu.h>

#define NEAR_LOD 42.0f
#define FAR_CUT 125.0f
#define MAX_NEAR 40      /* display-list budget: ~100 KB of pedestrian geometry per frame at most */
#define MAX_FAR 80

static const float face_shade[5] = { 0.85f, 0.7f, 1.0f, 0.75f, 0.6f };

/* box of half-size (hx,hy,hz) centred at local c, optionally swung by 'swing' about a hip at local y = pivot;
 * transformed by yaw and translated to base. 5 faces (no bottom) = 30 vertices. */
static int emit_box(VtxCP *v, V3 base, float yaw, V3 c, V3 h, float swing, float pivot, uint32_t col) {
    static const int F[5][4] = { {1, 2, 6, 5}, {4, 7, 3, 0}, {3, 7, 6, 2}, {5, 6, 7, 4}, {0, 3, 2, 1} };
    V3 p[8];
    float cs = cosf(yaw), sn = sinf(yaw), cw = cosf(swing), sw = sinf(swing);
    for (int i = 0; i < 8; i++) {
        float lx = (i == 1 || i == 2 || i == 5 || i == 6) ? c.x + h.x : c.x - h.x;
        float ly = (i == 2 || i == 3 || i == 6 || i == 7) ? c.y + h.y : c.y - h.y;
        float lz = (i >= 4) ? c.z + h.z : c.z - h.z;
        /* swing about the x axis through the pivot */
        float dy = ly - pivot, dz = lz;
        float ry = pivot + dy * cw - dz * sw, rz = dy * sw + dz * cw;
        p[i] = v3(base.x + lx * cs + rz * sn, base.y + ry, base.z - lx * sn + rz * cs);
    }
    int k = 0;
    for (int f = 0; f < 5; f++) {
        uint32_t fc = col_scale(col, face_shade[f]);
        int idx[6] = { 0, 1, 2, 0, 2, 3 };
        for (int j = 0; j < 6; j++) { V3 q = p[F[f][idx[j]]]; v[k].color = fc; v[k].x = q.x; v[k].y = q.y; v[k].z = q.z; k++; }
    }
    return k;
}

static int emit_umbrella(VtxCP *v, V3 top, float r, uint32_t col) {
    int k = 0;
    V3 apex = v3(top.x, top.y + 0.18f, top.z);
    for (int i = 0; i < 8; i++) {
        float a0 = (float)i * H_TAU / 8.0f, a1 = (float)(i + 1) * H_TAU / 8.0f;
        V3 p0 = v3(top.x + cosf(a0) * r, top.y, top.z + sinf(a0) * r), p1 = v3(top.x + cosf(a1) * r, top.y, top.z + sinf(a1) * r);
        uint32_t c = col_scale(col, (i & 1) ? 0.35f : 0.45f);
        v[k].color = c; v[k].x = apex.x; v[k].y = apex.y; v[k].z = apex.z; k++;
        v[k].color = c; v[k].x = p0.x; v[k].y = p0.y; v[k].z = p0.z; k++;
        v[k].color = c; v[k].x = p1.x; v[k].y = p1.y; v[k].z = p1.z; k++;
    }
    return k;
}

void rw_draw_crowd(const Crowd *cr, const Atmos *a, float time) {
    (void)time;
    if (!cr || !cr->n) return;
    V3 eye = gfx_cam_pos(), fwd = gfx_cam_fwd();
    int near_n = 0, far_n = 0;
    for (int i = 0; i < cr->n; i++) {
        const Ped *p = &cr->p[i];
        V3 d = v3_sub(p->pos, eye);
        float dist = v3_len(d);
        if (!p->active || dist > FAR_CUT || v3_dot(d, fwd) < -3.0f) continue;
        if (dist < NEAR_LOD) near_n++; else far_n++;
    }
    static const int qn[4] = { 12, 20, 30, MAX_NEAR }, qf[4] = { 20, 40, 60, MAX_FAR };
    if (near_n > qn[rw_quality()]) near_n = qn[rw_quality()];
    if (far_n > qf[rw_quality()]) far_n = qf[rw_quality()];
    int near_left = near_n, far_left = far_n;
    if (!near_n && !far_n) return;
    int cap = near_n * (4 * 30 + 24) + far_n * (2 * 30 + 24);
    VtxCP *v = (VtxCP *)sceGuGetMemory(cap * (int)sizeof(VtxCP));
    int k = 0;
    static const uint32_t skin[4] = { RGBA(120, 92, 76, 255), RGBA(80, 60, 50, 255), RGBA(150, 120, 100, 255), RGBA(40, 40, 46, 255) };
    for (int i = 0; i < cr->n; i++) {
        const Ped *p = &cr->p[i];
        V3 d = v3_sub(p->pos, eye);
        float dist = v3_len(d);
        if (!p->active || dist > FAR_CUT || v3_dot(d, fwd) < -3.0f) continue;
        uint32_t coat = p->coat, head = skin[p->style & 3];
        int is_near = dist < NEAR_LOD;
        if (is_near ? near_left <= 0 : far_left <= 0) continue;
        if (is_near) near_left--; else far_left--;
        if (is_near) {
            float sw = sinf(p->phase) * 0.5f * (p->wait > 0 ? 0.0f : 1.0f);
            k += emit_box(&v[k], p->pos, p->yaw, v3(-0.1f, 0.42f, 0), v3(0.07f, 0.42f, 0.08f), sw, 0.85f, RGBA(28, 28, 32, 255));
            k += emit_box(&v[k], p->pos, p->yaw, v3(0.1f, 0.42f, 0), v3(0.07f, 0.42f, 0.08f), -sw, 0.85f, RGBA(28, 28, 32, 255));
            k += emit_box(&v[k], p->pos, p->yaw, v3(0, 1.16f, 0), v3(0.21f, 0.32f, 0.12f), 0, 0, coat);
            k += emit_box(&v[k], p->pos, p->yaw, v3(0, 1.6f, 0), v3(0.1f, 0.11f, 0.1f), 0, 0, head);
        } else {
            k += emit_box(&v[k], p->pos, p->yaw, v3(0, 0.75f, 0), v3(0.2f, 0.75f, 0.12f), 0, 0, coat);
            k += emit_box(&v[k], p->pos, p->yaw, v3(0, 1.6f, 0), v3(0.1f, 0.11f, 0.1f), 0, 0, head);
        }
        if (p->umbrella) {
            V3 top = v3(p->pos.x, p->pos.y + 2.0f, p->pos.z);
            k += emit_umbrella(&v[k], top, 0.55f, p->umb);
            /* the canopy rim glows: one light sprite per umbrella */
            rw_light(v3(top.x, top.y + 0.05f, top.z), 0.75f, p->umb, 0.55f);
        }
    }
    gfx_no_tex();
    sceGuEnable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_FALSE);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_CULL_FACE);
    gfx_fog(1, a->fog_near, a->fog_far, a->fog_color);
    gfx_model_identity();
    if (k) sceGuDrawArray(GU_TRIANGLES, VT_CP | GU_TRANSFORM_3D, k, 0, v);
    sceGuEnable(GU_BLEND);
}
