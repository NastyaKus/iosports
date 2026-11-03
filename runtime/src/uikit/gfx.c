/*
 * iosrt software rasterizer (see gfx.h).
 *
 * Fill: scanline polygon rasterizer with 4 vertical sub-scanlines per pixel
 * row and exact horizontal span coverage, accumulated in a difference buffer,
 * nonzero or even-odd. Strokes are converted to positively oriented outline
 * contours (segment quads + joins + caps) and filled with nonzero, so overlaps
 * union instead of cancelling. Images/gradients are "paints" sampled per
 * covered pixel, which gives antialiased edges and clipping for free.
 */
#include "gfx.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <pthread.h>
#include "../third_party/stb/stb_image.h"
#include "../third_party/stb/stb_image_write.h"
#include "../third_party/stb/stb_truetype.h"

/* ------------------------------------------------------------ matrices */
gfx_mat gfx_mat_mul(gfx_mat m, gfx_mat n) {
    gfx_mat r;
    r.a = m.a * n.a + m.b * n.c;   r.b = m.a * n.b + m.b * n.d;
    r.c = m.c * n.a + m.d * n.c;   r.d = m.c * n.b + m.d * n.d;
    r.tx = m.tx * n.a + m.ty * n.c + n.tx; r.ty = m.tx * n.b + m.ty * n.d + n.ty;
    return r;
}
gfx_mat gfx_mat_invert(gfx_mat m) {
    double det = m.a * m.d - m.b * m.c;
    if (fabs(det) < 1e-12) return m;
    gfx_mat r;
    r.a = m.d / det; r.b = -m.b / det; r.c = -m.c / det; r.d = m.a / det;
    r.tx = -(m.tx * r.a + m.ty * r.c); r.ty = -(m.tx * r.b + m.ty * r.d);
    return r;
}

/* -------------------------------------------------------------- images */
struct gfx_image *gfx_image_new(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    struct gfx_image *im = calloc(1, sizeof *im);
    im->w = w; im->h = h; im->refs = 1;
    im->px = calloc((size_t)w * h, 4);
    return im;
}
struct gfx_image *gfx_image_retain(struct gfx_image *im) { if (im) __atomic_add_fetch(&im->refs, 1, __ATOMIC_RELAXED); return im; }
void gfx_image_release(struct gfx_image *im) {
    if (im && __atomic_sub_fetch(&im->refs, 1, __ATOMIC_ACQ_REL) == 0) { free(im->px); free(im); }
}
static pthread_once_t stbi_once = PTHREAD_ONCE_INIT;
static void stbi_setup(void) { stbi_convert_iphone_png_to_rgb(1); stbi_set_unpremultiply_on_load(1); }
struct gfx_image *gfx_image_decode(const void *data, size_t len) {
    pthread_once(&stbi_once, stbi_setup);
    int w, h, n;
    unsigned char *p = stbi_load_from_memory(data, (int)len, &w, &h, &n, 4);
    if (!p) return NULL;
    struct gfx_image *im = gfx_image_new(w, h);
    for (size_t i = 0; i < (size_t)w * h; i++) {
        unsigned r = p[i * 4], g = p[i * 4 + 1], b = p[i * 4 + 2], a = p[i * 4 + 3];
        if (a < 255) { r = (r * a + 127) / 255; g = (g * a + 127) / 255; b = (b * a + 127) / 255; }
        im->px[i] = r | g << 8 | b << 16 | (uint32_t)a << 24;
    }
    stbi_image_free(p);
    return im;
}
struct gfx_image *gfx_image_load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return NULL; }
    void *buf = malloc((size_t)n);
    size_t got = fread(buf, 1, (size_t)n, f); fclose(f);
    struct gfx_image *im = got == (size_t)n ? gfx_image_decode(buf, got) : NULL;
    free(buf);
    return im;
}
static unsigned char *unpremul_copy(const struct gfx_image *im) {
    unsigned char *o = malloc((size_t)im->w * im->h * 4);
    for (size_t i = 0; i < (size_t)im->w * im->h; i++) {
        uint32_t v = im->px[i]; unsigned a = v >> 24, r = v & 255, g = (v >> 8) & 255, b = (v >> 16) & 255;
        if (a && a < 255) { r = r * 255 / a; g = g * 255 / a; b = b * 255 / a; }
        o[i * 4] = (unsigned char)(r > 255 ? 255 : r); o[i * 4 + 1] = (unsigned char)(g > 255 ? 255 : g);
        o[i * 4 + 2] = (unsigned char)(b > 255 ? 255 : b); o[i * 4 + 3] = (unsigned char)a;
    }
    return o;
}
struct membuf { unsigned char *p; size_t n, cap; };
static void mem_write(void *ctx, void *data, int size) {
    struct membuf *m = ctx;
    if (m->n + (size_t)size > m->cap) { m->cap = (m->n + (size_t)size) * 2; m->p = realloc(m->p, m->cap); }
    memcpy(m->p + m->n, data, (size_t)size); m->n += (size_t)size;
}
void *gfx_image_encode_png(const struct gfx_image *im, size_t *len) {
    unsigned char *u = unpremul_copy(im); struct membuf m = { 0 };
    stbi_write_png_to_func(mem_write, &m, im->w, im->h, 4, u, im->w * 4);
    free(u); *len = m.n; return m.p;
}
void *gfx_image_encode_jpeg(const struct gfx_image *im, int quality, size_t *len) {
    unsigned char *u = unpremul_copy(im); struct membuf m = { 0 };
    stbi_write_jpg_to_func(mem_write, &m, im->w, im->h, 4, u, quality);
    free(u); *len = m.n; return m.p;
}
int gfx_image_write_png(const struct gfx_image *im, const char *path) {
    size_t n; void *p = gfx_image_encode_png(im, &n);
    FILE *f = fopen(path, "wb"); int ok = 0;
    if (f) { ok = fwrite(p, 1, n, f) == n; fclose(f); }
    free(p); return ok;
}

/* ---------------------------------------------------------------- polys */
void gfx_poly_init(struct gfx_poly *p) { memset(p, 0, sizeof *p); }
void gfx_poly_free(struct gfx_poly *p) { free(p->xy); free(p->starts); free(p->closed); memset(p, 0, sizeof *p); }
static void poly_pt(struct gfx_poly *p, float x, float y) {
    if (p->n == p->cap) { p->cap = p->cap ? p->cap * 2 : 64; p->xy = realloc(p->xy, (size_t)p->cap * 2 * sizeof(float)); }
    p->xy[p->n * 2] = x; p->xy[p->n * 2 + 1] = y; p->n++;
}
void gfx_poly_move(struct gfx_poly *p, float x, float y) {
    /* drop a previous contour that has only its start point */
    if (p->ncont && p->starts[p->ncont - 1] == p->n - 1) { p->n--; p->ncont--; }
    if (p->ncont == p->ccap) {
        p->ccap = p->ccap ? p->ccap * 2 : 8;
        p->starts = realloc(p->starts, (size_t)p->ccap * sizeof(int));
        p->closed = realloc(p->closed, (size_t)p->ccap);
    }
    p->starts[p->ncont] = p->n; p->closed[p->ncont] = 0; p->ncont++;
    poly_pt(p, x, y);
}
void gfx_poly_line(struct gfx_poly *p, float x, float y) {
    if (!p->ncont) { gfx_poly_move(p, x, y); return; }
    if (p->closed[p->ncont - 1]) {   /* drawing after closepath starts a new subpath at the old start */
        int s0 = p->starts[p->ncont - 1];
        gfx_poly_move(p, p->xy[s0 * 2], p->xy[s0 * 2 + 1]);
    }
    if (p->n > p->starts[p->ncont - 1]) {
        float lx = p->xy[p->n * 2 - 2], ly = p->xy[p->n * 2 - 1];
        if (lx == x && ly == y) return;
    }
    poly_pt(p, x, y);
}
static void last_pt(struct gfx_poly *p, float *x, float *y) {
    if (!p->n) { *x = *y = 0; return; }
    if (p->ncont && p->closed[p->ncont - 1]) { int s0 = p->starts[p->ncont - 1]; *x = p->xy[s0 * 2]; *y = p->xy[s0 * 2 + 1]; return; }
    *x = p->xy[p->n * 2 - 2]; *y = p->xy[p->n * 2 - 1];
}
#define FLAT_TOL 0.2f
void gfx_poly_cubic(struct gfx_poly *p, float x1, float y1, float x2, float y2, float x3, float y3) {
    float x0, y0; last_pt(p, &x0, &y0);
    float ddx = fmaxf(fabsf(x0 - 2 * x1 + x2), fabsf(x1 - 2 * x2 + x3));
    float ddy = fmaxf(fabsf(y0 - 2 * y1 + y2), fabsf(y1 - 2 * y2 + y3));
    float dd = sqrtf(ddx * ddx + ddy * ddy);
    int n = (int)ceilf(sqrtf(0.75f * dd / FLAT_TOL));
    if (n < 1) n = 1;
    if (n > 200) n = 200;
    for (int i = 1; i <= n; i++) {
        float t = (float)i / n, u = 1 - t;
        float a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
        gfx_poly_line(p, a * x0 + b * x1 + c * x2 + d * x3, a * y0 + b * y1 + c * y2 + d * y3);
    }
}
void gfx_poly_quad(struct gfx_poly *p, float x1, float y1, float x2, float y2) {
    float x0, y0; last_pt(p, &x0, &y0);
    gfx_poly_cubic(p, x0 + 2.f / 3 * (x1 - x0), y0 + 2.f / 3 * (y1 - y0), x2 + 2.f / 3 * (x1 - x2), y2 + 2.f / 3 * (y1 - y2), x2, y2);
}
void gfx_poly_close(struct gfx_poly *p) { if (p->ncont) p->closed[p->ncont - 1] = 1; }
void gfx_poly_bounds(const struct gfx_poly *p, float *x0, float *y0, float *x1, float *y1) {
    float a = INFINITY, b = INFINITY, c = -INFINITY, d = -INFINITY;
    for (int i = 0; i < p->n; i++) {
        float x = p->xy[i * 2], y = p->xy[i * 2 + 1];
        if (x < a) a = x;
        if (y < b) b = y;
        if (x > c) c = x;
        if (y > d) d = y;
    }
    *x0 = a; *y0 = b; *x1 = c; *y1 = d;
}
static int cont_end(const struct gfx_poly *p, int k) { return k + 1 < p->ncont ? p->starts[k + 1] : p->n; }

/* ------------------------------------------------------------- strokes */
static void add_contour_positive(struct gfx_poly *o, const float *pts, int n) {
    if (n < 3) return;
    double area = 0;
    for (int i = 0; i < n; i++) { int j = (i + 1) % n; area += (double)pts[i * 2] * pts[j * 2 + 1] - (double)pts[j * 2] * pts[i * 2 + 1]; }
    if (fabs(area) < 1e-9) return;
    gfx_poly_move(o, area > 0 ? pts[0] : pts[(n - 1) * 2], area > 0 ? pts[1] : pts[(n - 1) * 2 + 1]);
    for (int i = 1; i < n; i++) { int k = area > 0 ? i : n - 1 - i; poly_pt(o, pts[k * 2], pts[k * 2 + 1]); }
    o->closed[o->ncont - 1] = 1;
}
static void add_circle(struct gfx_poly *o, float cx, float cy, float r) {
    int n = (int)(r * 1.5f); if (n < 8) n = 8; if (n > 64) n = 64;
    float pts[128];
    for (int i = 0; i < n; i++) { float a = (float)(2 * M_PI * i / n); pts[i * 2] = cx + r * cosf(a); pts[i * 2 + 1] = cy + r * sinf(a); }
    add_contour_positive(o, pts, n);
}
static void stroke_polyline(const float *xy, int n, int closed, const struct gfx_stroke_style *st, struct gfx_poly *o) {
    float hw = st->width / 2; if (hw <= 0) hw = 0.5f;
    if (n == 1) {   /* zero-length subpath: round/square caps draw a dot */
        if (st->cap == GFX_CAP_ROUND) add_circle(o, xy[0], xy[1], hw);
        else if (st->cap == GFX_CAP_SQUARE) { float q[8] = { xy[0] - hw, xy[1] - hw, xy[0] + hw, xy[1] - hw, xy[0] + hw, xy[1] + hw, xy[0] - hw, xy[1] + hw }; add_contour_positive(o, q, 4); }
        return;
    }
    int nseg = closed ? n : n - 1;
    for (int i = 0; i < nseg; i++) {
        int j = (i + 1) % n;
        float x0 = xy[i * 2], y0 = xy[i * 2 + 1], x1 = xy[j * 2], y1 = xy[j * 2 + 1];
        float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
        if (len < 1e-6f) continue;
        float nx = -dy / len * hw, ny = dx / len * hw, ex = 0, ey = 0, sx = 0, sy = 0;
        if (!closed && st->cap == GFX_CAP_SQUARE) {
            if (i == 0) { sx = dx / len * hw; sy = dy / len * hw; }
            if (i == nseg - 1) { ex = dx / len * hw; ey = dy / len * hw; }
        }
        float q[8] = { x0 - sx + nx, y0 - sy + ny, x1 + ex + nx, y1 + ey + ny, x1 + ex - nx, y1 + ey - ny, x0 - sx - nx, y0 - sy - ny };
        add_contour_positive(o, q, 4);
    }
    /* joins */
    for (int i = 0; i < n; i++) {
        if (!closed && (i == 0 || i == n - 1)) continue;
        int pi = (i - 1 + n) % n, ni = (i + 1) % n;
        float px = xy[pi * 2], py = xy[pi * 2 + 1], cx = xy[i * 2], cy = xy[i * 2 + 1], qx = xy[ni * 2], qy = xy[ni * 2 + 1];
        float d0x = cx - px, d0y = cy - py, d1x = qx - cx, d1y = qy - cy;
        float l0 = sqrtf(d0x * d0x + d0y * d0y), l1 = sqrtf(d1x * d1x + d1y * d1y);
        if (l0 < 1e-6f || l1 < 1e-6f) continue;
        d0x /= l0; d0y /= l0; d1x /= l1; d1y /= l1;
        float cross = d0x * d1y - d0y * d1x;
        if (fabsf(cross) < 1e-4f && d0x * d1x + d0y * d1y > 0) continue;   /* straight */
        if (st->join == GFX_JOIN_ROUND) { add_circle(o, cx, cy, hw); continue; }
        /* outer side normals */
        float s = cross > 0 ? -1.f : 1.f;
        float n0x = -d0y * hw * s, n0y = d0x * hw * s, n1x = -d1y * hw * s, n1y = d1x * hw * s;
        float tri[8] = { cx, cy, cx + n0x, cy + n0y, cx + n1x, cy + n1y };
        int k = 3;
        if (st->join == GFX_JOIN_MITER) {
            float mx = n0x + n1x, my = n0y + n1y, ml = sqrtf(mx * mx + my * my);
            if (ml > 1e-6f) {
                float cosh = ml / (2 * hw);                 /* cos(theta/2) */
                float limit = st->miter > 0 ? st->miter : 10;
                if (cosh > 1e-6f && 1 / cosh <= limit) {
                    float mlen = hw / cosh;
                    tri[2] = cx + n0x; tri[3] = cy + n0y;
                    tri[4] = cx + mx / ml * mlen; tri[5] = cy + my / ml * mlen;
                    tri[6] = cx + n1x; tri[7] = cy + n1y; k = 4;
                }
            }
        }
        add_contour_positive(o, tri, k);
    }
    if (!closed && st->cap == GFX_CAP_ROUND) { add_circle(o, xy[0], xy[1], hw); add_circle(o, xy[(n - 1) * 2], xy[(n - 1) * 2 + 1], hw); }
}
static void dash_contour(const float *xy, int n, int closed, const struct gfx_stroke_style *st, struct gfx_poly *o) {
    float total = 0; for (int i = 0; i < st->ndash; i++) total += st->dash[i];
    if (total <= 0) { stroke_polyline(xy, n, closed, st, o); return; }
    int di = 0; float left = st->dash[0], phase = fmodf(st->dash_phase, total); int on = 1;
    while (phase > 0) { if (phase >= left) { phase -= left; di = (di + 1) % st->ndash; left = st->dash[di]; on = !on; } else { left -= phase; phase = 0; } }
    float *buf = malloc(sizeof(float) * 2 * (size_t)(n + 2) * 4 + 64); int bn = 0, cap = (n + 2) * 4 + 32;
    if (on) { buf[0] = xy[0]; buf[1] = xy[1]; bn = 1; }
    int nseg = closed ? n : n - 1;
    for (int i = 0; i < nseg; i++) {
        int j = (i + 1) % n;
        float x0 = xy[i * 2], y0 = xy[i * 2 + 1], dx = xy[j * 2] - x0, dy = xy[j * 2 + 1] - y0, len = sqrtf(dx * dx + dy * dy), pos = 0;
        while (len - pos > left) {
            pos += left;
            float px = x0 + dx * pos / len, py = y0 + dy * pos / len;
            if (on) { if (bn >= cap) break; buf[bn * 2] = px; buf[bn * 2 + 1] = py; bn++; stroke_polyline(buf, bn, 0, st, o); bn = 0; }
            else { buf[0] = px; buf[1] = py; bn = 1; }
            on = !on; di = (di + 1) % st->ndash; left = st->dash[di];
        }
        left -= len - pos;
        if (on && bn < cap) { buf[bn * 2] = xy[j * 2]; buf[bn * 2 + 1] = xy[j * 2 + 1]; bn++; }
    }
    if (on && bn > 1) stroke_polyline(buf, bn, 0, st, o);
    free(buf);
}
void gfx_stroke_to_poly(const struct gfx_poly *p, const struct gfx_stroke_style *st, struct gfx_poly *o) {
    for (int k = 0; k < p->ncont; k++) {
        int s = p->starts[k], e = cont_end(p, k), n = e - s;
        int closed = p->closed[k];
        if (closed && n > 2 && p->xy[s * 2] == p->xy[(e - 1) * 2] && p->xy[s * 2 + 1] == p->xy[(e - 1) * 2 + 1]) n--;
        if (n < 1) continue;
        if (st->ndash > 0 && st->dash) dash_contour(p->xy + s * 2, n, closed && n > 2, st, o);
        else stroke_polyline(p->xy + s * 2, n, closed && n > 2, st, o);
    }
}

/* ----------------------------------------------------------- surfaces */
void gfx_surface_init(struct gfx_surface *s, uint32_t *px, int w, int h, int stride) {
    memset(s, 0, sizeof *s); s->px = px; s->w = w; s->h = h; s->stride = stride;
    gfx_surface_reset_clip(s);
}
void gfx_surface_reset_clip(struct gfx_surface *s) { s->cx0 = 0; s->cy0 = 0; s->cx1 = s->w; s->cy1 = s->h; free(s->mask); s->mask = NULL; }

/* ---------------------------------------------------------- shading */
static inline uint32_t pack(float r, float g, float b, float a) {
    #define C8(v) ((uint32_t)((v) <= 0 ? 0 : (v) >= 1 ? 255 : (v) * 255.f + 0.5f))
    return C8(r) | C8(g) << 8 | C8(b) << 16 | C8(a) << 24;
    #undef C8
}
static void grad_color(const struct gfx_paint *pt, float t, float out[4]) {
    if (pt->nstops <= 0) { out[0] = out[1] = out[2] = out[3] = 0; return; }
    if (t <= pt->stop_pos[0]) { memcpy(out, pt->stop_rgba[0], 16); return; }
    for (int i = 1; i < pt->nstops; i++) {
        if (t <= pt->stop_pos[i]) {
            float a = pt->stop_pos[i - 1], b = pt->stop_pos[i], f = b > a ? (t - a) / (b - a) : 1;
            for (int k = 0; k < 4; k++) out[k] = pt->stop_rgba[i - 1][k] + (pt->stop_rgba[i][k] - pt->stop_rgba[i - 1][k]) * f;
            return;
        }
    }
    memcpy(out, pt->stop_rgba[pt->nstops - 1], 16);
}
static inline uint32_t sample_image(const struct gfx_image *im, double u, double v, int smooth) {
    if (!smooth) {
        int x = (int)floor(u), y = (int)floor(v);
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= im->w) x = im->w - 1;
        if (y >= im->h) y = im->h - 1;
        return im->px[(size_t)y * im->w + x];
    }
    u -= 0.5; v -= 0.5;
    int x0 = (int)floor(u), y0 = (int)floor(v);
    float fx = (float)(u - x0), fy = (float)(v - y0);
    int x1 = x0 + 1, y1 = y0 + 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= im->w) x1 = im->w - 1;
    if (y1 >= im->h) y1 = im->h - 1;
    if (x0 >= im->w) x0 = im->w - 1;
    if (y0 >= im->h) y0 = im->h - 1;
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    uint32_t a = im->px[(size_t)y0 * im->w + x0], b = im->px[(size_t)y0 * im->w + x1];
    uint32_t c = im->px[(size_t)y1 * im->w + x0], d = im->px[(size_t)y1 * im->w + x1];
    uint32_t r = 0;
    for (int k = 0; k < 32; k += 8) {
        float va = (float)((a >> k) & 255), vb = (float)((b >> k) & 255), vc = (float)((c >> k) & 255), vd = (float)((d >> k) & 255);
        float top = va + (vb - va) * fx, bot = vc + (vd - vc) * fx, v2 = top + (bot - top) * fy;
        r |= (uint32_t)(v2 + 0.5f) << k;
    }
    return r;
}
/* source pixel (premultiplied) for device pixel (x,y) */
static inline uint32_t shade(const struct gfx_paint *pt, int x, int y, uint32_t solid) {
    switch (pt->kind) {
    case GFX_PAINT_SOLID: return solid;
    case GFX_PAINT_LINEAR: case GFX_PAINT_RADIAL: {
        float px = x + 0.5f, py = y + 0.5f, t;
        if (pt->kind == GFX_PAINT_LINEAR) {
            float dx = pt->gx1 - pt->gx0, dy = pt->gy1 - pt->gy0, l2 = dx * dx + dy * dy;
            t = l2 > 0 ? ((px - pt->gx0) * dx + (py - pt->gy0) * dy) / l2 : 0;
        } else {
            double cdx = pt->gx1 - pt->gx0, cdy = pt->gy1 - pt->gy0, dr = pt->gr1 - pt->gr0;
            double qx = px - pt->gx0, qy = py - pt->gy0;
            double a = cdx * cdx + cdy * cdy - dr * dr, b = qx * cdx + qy * cdy + pt->gr0 * dr, c = qx * qx + qy * qy - (double)pt->gr0 * pt->gr0;
            if (fabs(a) < 1e-9) { if (fabs(b) < 1e-9) return 0; t = (float)(c / (2 * b)); }
            else {
                double disc = b * b - a * c;
                if (disc < 0) return 0;
                double sq = sqrt(disc), t1 = (b + sq) / a, t2 = (b - sq) / a;
                double tt = t1 > t2 ? t1 : t2;
                if (pt->gr0 + tt * dr < 0) tt = t1 > t2 ? t2 : t1;
                if (pt->gr0 + tt * dr < 0) return 0;
                t = (float)tt;
            }
        }
        if ((t < 0 && !pt->extend_start) || (t > 1 && !pt->extend_end)) return 0;
        float c4[4]; grad_color(pt, t < 0 ? 0 : t > 1 ? 1 : t, c4);
        float a = c4[3] * pt->alpha;
        return pack(c4[0] * a, c4[1] * a, c4[2] * a, a);
    }
    case GFX_PAINT_IMAGE: {
        double u, v; gfx_mat_apply(&pt->inv, x + 0.5, y + 0.5, &u, &v);
        uint32_t p = sample_image(pt->image, u, v, pt->smooth);
        if (pt->tint) {
            float a = (float)(p >> 24) / 255.f * pt->tint[3] * pt->alpha;
            return pack(pt->tint[0] * a, pt->tint[1] * a, pt->tint[2] * a, a);
        }
        if (pt->alpha < 1) {
            uint32_t r = 0;
            for (int k = 0; k < 32; k += 8) r |= (uint32_t)(((p >> k) & 255) * pt->alpha + 0.5f) << k;
            return r;
        }
        return p;
    }
    }
    return 0;
}
static inline uint32_t swap_rb(uint32_t v) { return (v & 0xff00ff00u) | (v & 0xff) << 16 | ((v >> 16) & 0xff); }
static inline uint32_t blend_px(uint32_t d, uint32_t s, float cov, int mode) {
    float sr = (float)(s & 255), sg = (float)((s >> 8) & 255), sb = (float)((s >> 16) & 255), sa = (float)(s >> 24);
    float dr = (float)(d & 255), dg = (float)((d >> 8) & 255), db = (float)((d >> 16) & 255), da = (float)(d >> 24);
    float r, g, b, a;
    switch (mode) {
    case GFX_BLEND_CLEAR: r = dr * (1 - cov); g = dg * (1 - cov); b = db * (1 - cov); a = da * (1 - cov); break;
    case GFX_BLEND_COPY: r = dr + (sr - dr) * cov; g = dg + (sg - dg) * cov; b = db + (sb - db) * cov; a = da + (sa - da) * cov; break;
    case GFX_BLEND_DEST_OUT: { float k = 1 - sa / 255.f * cov; r = dr * k; g = dg * k; b = db * k; a = da * k; break; }
    case GFX_BLEND_DEST_IN: { float k = 1 - cov + cov * sa / 255.f; r = dr * k; g = dg * k; b = db * k; a = da * k; break; }
    case GFX_BLEND_MULTIPLY: case GFX_BLEND_SCREEN: {
        sr *= cov; sg *= cov; sb *= cov; sa *= cov;
        float isa = 1 - sa / 255.f, ida = 1 - da / 255.f;
        if (mode == GFX_BLEND_MULTIPLY) {
            r = sr * dr / 255.f + sr * ida + dr * isa; g = sg * dg / 255.f + sg * ida + dg * isa;
            b = sb * db / 255.f + sb * ida + db * isa; a = sa + da * isa;
        } else { r = sr + dr - sr * dr / 255.f; g = sg + dg - sg * dg / 255.f; b = sb + db - sb * db / 255.f; a = sa + da - sa * da / 255.f; }
        break;
    }
    default: {
        if (cov >= 0.999f && (s >> 24) == 255) return s;
        sr *= cov; sg *= cov; sb *= cov; sa *= cov;
        float k = 1 - sa / 255.f;
        r = sr + dr * k; g = sg + dg * k; b = sb + db * k; a = sa + da * k;
    }
    }
    #define B8(v) ((uint32_t)((v) <= 0 ? 0 : (v) >= 255 ? 255 : (v) + 0.5f))
    return B8(r) | B8(g) << 8 | B8(b) << 16 | B8(a) << 24;
    #undef B8
}

/* ---------------------------------------------------------- rasterizer */
#define SS 4
struct edge { float x0, y0, x1, y1, dxdy; int dir; };
struct xing { float x; int dir; };
static int edge_cmp(const void *a, const void *b) { float d = ((const struct edge *)a)->y0 - ((const struct edge *)b)->y0; return d < 0 ? -1 : d > 0; }
static int xing_cmp(const void *a, const void *b) { float d = ((const struct xing *)a)->x - ((const struct xing *)b)->x; return d < 0 ? -1 : d > 0; }

/* Calls emit(ctx, y, x0, x1, cov[]) for every row with coverage; cov indexed from x0. */
typedef void (*row_fn)(void *ctx, int y, int x0, int x1, const float *cov);
static void rasterize(const struct gfx_poly *p, int rule, int cx0, int cy0, int cx1, int cy1, row_fn emit, void *ctx) {
    if (p->n < 3 || cx0 >= cx1 || cy0 >= cy1) return;
    struct edge *E = malloc(sizeof *E * (size_t)p->n); int ne = 0;
    float bx0, by0, bx1, by1; gfx_poly_bounds(p, &bx0, &by0, &bx1, &by1);
    for (int k = 0; k < p->ncont; k++) {
        int s = p->starts[k], e = cont_end(p, k);
        for (int i = s; i < e; i++) {
            int j = i + 1 < e ? i + 1 : s;
            float x0 = p->xy[i * 2], y0 = p->xy[i * 2 + 1], x1 = p->xy[j * 2], y1 = p->xy[j * 2 + 1];
            if (y0 == y1 || !isfinite(x0) || !isfinite(y0) || !isfinite(x1) || !isfinite(y1)) continue;
            struct edge *ed = &E[ne++];
            if (y0 < y1) { ed->x0 = x0; ed->y0 = y0; ed->x1 = x1; ed->y1 = y1; ed->dir = 1; }
            else { ed->x0 = x1; ed->y0 = y1; ed->x1 = x0; ed->y1 = y0; ed->dir = -1; }
            ed->dxdy = (ed->x1 - ed->x0) / (ed->y1 - ed->y0);
        }
    }
    if (!ne) { free(E); return; }
    qsort(E, (size_t)ne, sizeof *E, edge_cmp);
    int ry0 = (int)floorf(by0), ry1 = (int)ceilf(by1), rx0 = (int)floorf(bx0), rx1 = (int)ceilf(bx1) + 1;
    if (ry0 < cy0) ry0 = cy0;
    if (ry1 > cy1) ry1 = cy1;
    if (rx0 < cx0) rx0 = cx0;
    if (rx1 > cx1) rx1 = cx1;
    if (rx0 >= rx1 || ry0 >= ry1) { free(E); return; }
    int W = rx1 - rx0;
    float *acc = calloc((size_t)W + 2, sizeof(float)), *run = calloc((size_t)W + 2, sizeof(float)), *cov = malloc(sizeof(float) * ((size_t)W + 2));
    int *active = malloc(sizeof(int) * (size_t)ne); int nact = 0, next = 0;
    struct xing *X = malloc(sizeof *X * (size_t)ne);
    const float wgt = 1.f / SS;
    for (int y = ry0; y < ry1; y++) {
        while (next < ne && E[next].y0 < y + 1) active[nact++] = next++;
        int k = 0; for (int i = 0; i < nact; i++) if (E[active[i]].y1 > y) active[k++] = active[i];
        nact = k;
        if (!nact) { if (next >= ne) break; continue; }
        int lo = W, hi = -1;
        for (int sub = 0; sub < SS; sub++) {
            float sy = y + (sub + 0.5f) / SS; int nx = 0;
            for (int i = 0; i < nact; i++) {
                const struct edge *ed = &E[active[i]];
                if (sy < ed->y0 || sy >= ed->y1) continue;
                X[nx].x = ed->x0 + (sy - ed->y0) * ed->dxdy; X[nx].dir = ed->dir; nx++;
            }
            if (nx < 2) continue;
            qsort(X, (size_t)nx, sizeof *X, xing_cmp);
            int wind = 0;
            for (int i = 0; i < nx - 1; i++) {
                wind += X[i].dir;
                int inside = rule == GFX_FILL_EVENODD ? (wind & 1) : wind != 0;
                if (!inside) continue;
                float xa = X[i].x - rx0, xb = X[i + 1].x - rx0;
                if (xa < 0) xa = 0;
                if (xb > W) xb = (float)W;
                if (xb <= xa) continue;
                int ia = (int)xa, ib = (int)xb;
                if (ia == ib) acc[ia] += (xb - xa) * wgt;
                else {
                    acc[ia] += (ia + 1 - xa) * wgt;
                    run[ia + 1] += wgt; run[ib] -= wgt;
                    if (ib < W) acc[ib] += (xb - ib) * wgt;
                }
                if (ia < lo) lo = ia;
                if (ib > hi) hi = ib;
            }
        }
        if (hi < lo) continue;
        if (hi >= W) hi = W - 1;
        float r = 0; int any = 0;
        for (int x = lo; x <= hi; x++) {
            r += run[x]; float c = acc[x] + r;
            cov[x - lo] = c > 1 ? 1 : c < 0 ? 0 : c;
            if (cov[x - lo] > 0.002f) any = 1;
        }
        memset(acc + lo, 0, sizeof(float) * (size_t)(hi - lo + 2));
        memset(run + lo, 0, sizeof(float) * (size_t)(hi - lo + 2));
        if (any) emit(ctx, y, rx0 + lo, rx0 + hi + 1, cov);
    }
    free(E); free(acc); free(run); free(cov); free(active); free(X);
}

struct paint_ctx { struct gfx_surface *s; const struct gfx_paint *pt; uint32_t solid; };
static void paint_row(void *c, int y, int x0, int x1, const float *cov) {
    struct paint_ctx *pc = c; struct gfx_surface *s = pc->s;
    uint32_t *row = s->px + (size_t)y * s->stride;
    const uint8_t *m = s->mask ? s->mask + (size_t)y * s->w : NULL;
    for (int x = x0; x < x1; x++) {
        float cv = cov[x - x0];
        if (m) cv *= m[x] * (1.f / 255);
        if (cv <= 0.002f) continue;
        uint32_t src = shade(pc->pt, x, y, pc->solid);
        if (!src && pc->pt->blend == GFX_BLEND_NORMAL) continue;
        if (s->bgra) src = swap_rb(src);
        row[x] = blend_px(row[x], src, cv, pc->pt->blend);
    }
}
static uint32_t solid_of(const struct gfx_paint *pt) {
    float a = pt->color[3] * pt->alpha;
    return pack(pt->color[0] * a, pt->color[1] * a, pt->color[2] * a, a);
}
void gfx_fill_poly(struct gfx_surface *s, const struct gfx_poly *p, int rule, const struct gfx_paint *pt) {
    struct paint_ctx pc = { s, pt, pt->kind == GFX_PAINT_SOLID ? solid_of(pt) : 0 };
    if (pt->kind == GFX_PAINT_SOLID && !pc.solid && pt->blend == GFX_BLEND_NORMAL) return;
    rasterize(p, rule, s->cx0, s->cy0, s->cx1, s->cy1, paint_row, &pc);
}
void gfx_fill_rect(struct gfx_surface *s, float x0, float y0, float x1, float y1, const struct gfx_paint *pt) {
    struct gfx_poly p; gfx_poly_init(&p);
    gfx_poly_move(&p, x0, y0); gfx_poly_line(&p, x1, y0); gfx_poly_line(&p, x1, y1); gfx_poly_line(&p, x0, y1);
    gfx_fill_poly(s, &p, GFX_FILL_NONZERO, pt);
    gfx_poly_free(&p);
}
void gfx_draw_image(struct gfx_surface *s, const struct gfx_image *im, gfx_mat m, float alpha, int blend, const float *tint) {
    if (!im || alpha <= 0) return;
    struct gfx_poly p; gfx_poly_init(&p); double x, y;
    gfx_mat_apply(&m, 0, 0, &x, &y); gfx_poly_move(&p, (float)x, (float)y);
    gfx_mat_apply(&m, im->w, 0, &x, &y); gfx_poly_line(&p, (float)x, (float)y);
    gfx_mat_apply(&m, im->w, im->h, &x, &y); gfx_poly_line(&p, (float)x, (float)y);
    gfx_mat_apply(&m, 0, im->h, &x, &y); gfx_poly_line(&p, (float)x, (float)y);
    struct gfx_paint pt; memset(&pt, 0, sizeof pt);
    pt.kind = GFX_PAINT_IMAGE; pt.blend = blend; pt.alpha = alpha; pt.image = im; pt.inv = gfx_mat_invert(m); pt.tint = tint;
    /* bilinear unless the mapping is a 1:1 pixel copy */
    pt.smooth = !(fabs(m.a - 1) < 1e-3 && fabs(m.d - 1) < 1e-3 && fabs(m.b) < 1e-3 && fabs(m.c) < 1e-3 && fabs(m.tx - floor(m.tx)) < 1e-3 && fabs(m.ty - floor(m.ty)) < 1e-3);
    gfx_fill_poly(s, &p, GFX_FILL_NONZERO, &pt);
    gfx_poly_free(&p);
}
struct mask_ctx { uint8_t *m; int w; };
static void mask_row(void *c, int y, int x0, int x1, const float *cov) {
    struct mask_ctx *mc = c;
    for (int x = x0; x < x1; x++) mc->m[(size_t)y * mc->w + x] = (uint8_t)(cov[x - x0] * 255.f + 0.5f);
}
static void clip_bbox_tighten(struct gfx_surface *s, float x0, float y0, float x1, float y1) {
    int a = (int)floorf(x0), b = (int)floorf(y0), c = (int)ceilf(x1), d = (int)ceilf(y1);
    if (a > s->cx0) s->cx0 = a;
    if (b > s->cy0) s->cy0 = b;
    if (c < s->cx1) s->cx1 = c;
    if (d < s->cy1) s->cy1 = d;
    if (s->cx1 < s->cx0) s->cx1 = s->cx0;
    if (s->cy1 < s->cy0) s->cy1 = s->cy0;
}
void gfx_clip_poly(struct gfx_surface *s, const struct gfx_poly *p, int rule) {
    float x0, y0, x1, y1; gfx_poly_bounds(p, &x0, &y0, &x1, &y1);
    if (p->n < 3) { s->cx1 = s->cx0; s->cy1 = s->cy0; return; }
    /* axis-aligned rectangle on pixel boundaries: no mask needed */
    if (p->ncont == 1 && p->n == 4 && !s->mask) {
        int rect = 1;
        for (int i = 0; i < 4; i++) {
            float x = p->xy[i * 2], y = p->xy[i * 2 + 1];
            if ((x != x0 && x != x1) || (y != y0 && y != y1) || fabsf(x - roundf(x)) > 0.01f || fabsf(y - roundf(y)) > 0.01f) rect = 0;
        }
        if (rect) { clip_bbox_tighten(s, x0, y0, x1, y1); return; }
    }
    uint8_t *m = calloc((size_t)s->w * s->h, 1);
    struct mask_ctx mc = { m, s->w };
    rasterize(p, rule, s->cx0, s->cy0, s->cx1, s->cy1, mask_row, &mc);
    if (s->mask) for (size_t i = 0; i < (size_t)s->w * s->h; i++) m[i] = (uint8_t)((m[i] * s->mask[i] + 127) / 255);
    free(s->mask); s->mask = m;
    clip_bbox_tighten(s, x0, y0, x1, y1);
}
void gfx_clip_image_mask(struct gfx_surface *s, const struct gfx_image *im, gfx_mat m, int lum) {
    uint8_t *mk = calloc((size_t)s->w * s->h, 1);
    gfx_mat inv = gfx_mat_invert(m);
    for (int y = s->cy0; y < s->cy1; y++)
        for (int x = s->cx0; x < s->cx1; x++) {
            double u, v; gfx_mat_apply(&inv, x + 0.5, y + 0.5, &u, &v);
            if (u < 0 || v < 0 || u >= im->w || v >= im->h) continue;
            uint32_t p = sample_image(im, u, v, 1);
            unsigned val = lum ? ((p & 255) * 3 + ((p >> 8) & 255) * 6 + ((p >> 16) & 255)) / 10 : p >> 24;
            mk[(size_t)y * s->w + x] = (uint8_t)val;
        }
    if (s->mask) for (size_t i = 0; i < (size_t)s->w * s->h; i++) mk[i] = (uint8_t)((mk[i] * s->mask[i] + 127) / 255);
    free(s->mask); s->mask = mk;
}

/* ---------------------------------------------------------------- text */
struct gfx_font { stbtt_fontinfo info; unsigned char *data; float ascent, descent, gap; };
static struct gfx_font *fonts[2]; static int font_tried[2];
static pthread_mutex_t font_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned char *read_all(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = n > 0 ? malloc((size_t)n) : NULL;
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f); return b;
}
struct gfx_font *gfx_font_from_memory(unsigned char *d, size_t len);
static struct gfx_font *font_open(const char *path) {
    unsigned char *d = read_all(path);
    if (!d) return NULL;
    struct gfx_font *f = gfx_font_from_memory(d, 0);
    if (!f) free(d);
    return f;
}
struct gfx_font *gfx_font_from_memory(unsigned char *d, size_t len) {
    (void)len;
    struct gfx_font *f = calloc(1, sizeof *f);
    if (!stbtt_InitFont(&f->info, d, stbtt_GetFontOffsetForIndex(d, 0))) { free(f); return NULL; }
    f->data = d;
    int a, de, g; stbtt_GetFontVMetrics(&f->info, &a, &de, &g);
    float sc = stbtt_ScaleForMappingEmToPixels(&f->info, 1);
    f->ascent = a * sc; f->descent = -de * sc; f->gap = g * sc;
    return f;
}
int gfx_font_name(struct gfx_font *f, int id, char *out, size_t n) {
    if (!f || !n) return 0;
    int len = 0; const char *p = stbtt_GetFontNameString(&f->info, &len, STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP, STBTT_MS_LANG_ENGLISH, id);
    size_t k = 0;
    if (p) { for (int i = 0; i + 1 < len && k + 1 < n; i += 2) out[k++] = p[i + 1]; }   /* UTF-16BE, ASCII subset */
    else {
        p = stbtt_GetFontNameString(&f->info, &len, STBTT_PLATFORM_ID_MAC, STBTT_MAC_EID_ROMAN, STBTT_MAC_LANG_ENGLISH, id);
        if (!p) return 0;
        for (int i = 0; i < len && k + 1 < n; i++) out[k++] = p[i];
    }
    out[k] = 0; return (int)k;
}
struct gfx_font *gfx_font_get(int bold) {
    bold = bold ? 1 : 0;
    pthread_mutex_lock(&font_lock);
    if (!font_tried[bold]) {
        font_tried[bold] = 1;
        const char *env = getenv(bold ? "IOSRT_FONT_BOLD" : "IOSRT_FONT");
        static const char *regular[] = { "/system/fonts/Roboto-Regular.ttf", "/system/fonts/RobotoStatic-Regular.ttf", "/system/fonts/NotoSans-Regular.ttf",
            "/system/fonts/DroidSans.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/truetype/crosextra/Carlito-Regular.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf", NULL };
        static const char *boldf[] = { "/system/fonts/Roboto-Bold.ttf", "/system/fonts/RobotoStatic-Bold.ttf", "/system/fonts/NotoSans-Bold.ttf",
            "/system/fonts/DroidSans-Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", "/usr/share/fonts/truetype/crosextra/Carlito-Bold.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf", NULL };
        if (env) fonts[bold] = font_open(env);
        for (const char **p = bold ? boldf : regular; !fonts[bold] && *p; p++) fonts[bold] = font_open(*p);
    }
    struct gfx_font *f = fonts[bold];
    pthread_mutex_unlock(&font_lock);
    if (!f && bold) return gfx_font_get(0);
    return f;
}
void gfx_font_vmetrics(struct gfx_font *f, float *a, float *d, float *g) {
    if (!f) { *a = 0.8f; *d = 0.2f; *g = 0; return; }
    *a = f->ascent; *d = f->descent; *g = f->gap;
}
static uint32_t utf8_next(const char **pp, const char *end) {
    const unsigned char *p = (const unsigned char *)*pp; uint32_t c = *p++;
    if (c >= 0xf0 && p + 2 < (const unsigned char *)end + 1) { c = (c & 7) << 18 | (p[0] & 63u) << 12 | (p[1] & 63u) << 6 | (p[2] & 63u); p += 3; }
    else if (c >= 0xe0 && p + 1 < (const unsigned char *)end + 1) { c = (c & 15) << 12 | (p[0] & 63u) << 6 | (p[1] & 63u); p += 2; }
    else if (c >= 0xc0 && p < (const unsigned char *)end + 1) { c = (c & 31) << 6 | (p[0] & 63u); p += 1; }
    *pp = (const char *)p; return c;
}
float gfx_text_width(struct gfx_font *f, const char *s, size_t n, float size) {
    const char *p = s, *e = s + n; float w = 0;
    if (!f) { size_t k = 0; while (p < e) { utf8_next(&p, e); k++; } return k * size * 0.55f; }
    float sc = stbtt_ScaleForMappingEmToPixels(&f->info, size);
    int prev = 0;
    while (p < e) {
        uint32_t c = utf8_next(&p, e);
        int g = stbtt_FindGlyphIndex(&f->info, (int)c), adv, lsb;
        stbtt_GetGlyphHMetrics(&f->info, g, &adv, &lsb);
        if (prev) w += stbtt_GetGlyphKernAdvance(&f->info, prev, g) * sc;
        w += adv * sc; prev = g;
    }
    return w;
}
/* glyph cache */
struct glyph { struct gfx_font *f; int g; int qsize; int sub; unsigned char *bm; int w, h, xo, yo; };
#define GCACHE 2048
static struct glyph gcache[GCACHE];
static pthread_mutex_t gcache_lock = PTHREAD_MUTEX_INITIALIZER;
static const struct glyph *glyph_get(struct gfx_font *f, int g, float px, int sub) {
    int q = (int)(px * 4 + 0.5f);
    unsigned h = ((unsigned)g * 2654435761u ^ (unsigned)q * 40503u ^ (unsigned)sub * 97u ^ (unsigned)(uintptr_t)f) % GCACHE;
    struct glyph *e = &gcache[h];
    if (e->bm && e->f == f && e->g == g && e->qsize == q && e->sub == sub) return e;
    free(e->bm); memset(e, 0, sizeof *e);
    float sc = stbtt_ScaleForMappingEmToPixels(&f->info, q / 4.f);
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBoxSubpixel(&f->info, g, sc, sc, sub / 4.f, 0, &x0, &y0, &x1, &y1);
    e->w = x1 - x0; e->h = y1 - y0; e->xo = x0; e->yo = y0;
    if (e->w > 0 && e->h > 0) {
        e->bm = malloc((size_t)e->w * e->h);
        stbtt_MakeGlyphBitmapSubpixel(&f->info, e->bm, e->w, e->h, e->w, sc, sc, sub / 4.f, 0, g);
    } else e->bm = malloc(1);
    e->f = f; e->g = g; e->qsize = q; e->sub = sub;
    return e;
}
void gfx_draw_text(struct gfx_surface *s, struct gfx_font *f, const char *str, size_t n, float x, float y, float px, const float *rgba, int fake_bold) {
    if (!f || px < 1) return;
    float a = rgba[3];
    uint32_t col = pack(rgba[0] * a, rgba[1] * a, rgba[2] * a, a);
    if (!col) return;
    if (s->bgra) col = swap_rb(col);
    float sc = stbtt_ScaleForMappingEmToPixels(&f->info, px);
    const char *p = str, *e = str + n; int prev = 0; float pen = x;
    pthread_mutex_lock(&gcache_lock);
    while (p < e) {
        uint32_t c = utf8_next(&p, e);
        if (c == '\n' || c == '\r') continue;
        int g = stbtt_FindGlyphIndex(&f->info, (int)c), adv, lsb;
        stbtt_GetGlyphHMetrics(&f->info, g, &adv, &lsb);
        if (prev) pen += stbtt_GetGlyphKernAdvance(&f->info, prev, g) * sc;
        prev = g;
        int ix = (int)floorf(pen), sub = (int)((pen - ix) * 4);
        const struct glyph *gl = glyph_get(f, g, px, sub);
        int gx = ix + gl->xo, gy = (int)floorf(y + 0.5f) + gl->yo;
        for (int pass = 0; pass <= (fake_bold ? 1 : 0); pass++)
            for (int j = 0; j < gl->h; j++) {
                int yy = gy + j;
                if (yy < s->cy0 || yy >= s->cy1) continue;
                uint32_t *row = s->px + (size_t)yy * s->stride;
                const uint8_t *m = s->mask ? s->mask + (size_t)yy * s->w : NULL;
                for (int i = 0; i < gl->w; i++) {
                    int xx = gx + i + pass;
                    if (xx < s->cx0 || xx >= s->cx1) continue;
                    float cv = gl->bm[j * gl->w + i] * (1.f / 255);
                    if (m) cv *= m[xx] * (1.f / 255);
                    if (cv > 0.002f) row[xx] = blend_px(row[xx], col, cv, GFX_BLEND_NORMAL);
                }
            }
        pen += adv * sc;
    }
    pthread_mutex_unlock(&gcache_lock);
}
