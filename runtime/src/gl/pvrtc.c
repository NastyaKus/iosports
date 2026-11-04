/* PVRTC v1 decoder (2 and 4 bits per pixel), written from the format's
 * public description:
 *   - the image is a grid of blocks (4x4 pixels at 4 bpp, 8x4 at 2 bpp),
 *     stored in Morton (twiddled) order, 64 bits each: 32 bits of
 *     modulation data, then 32 bits of colour data;
 *   - colour data: bit 0 is the modulation mode, bits 1-15 colour A
 *     (RGB554 when bit 15 is set, else ARGB3443), bits 16-31 colour B
 *     (RGB555 when bit 31 is set, else ARGB3444);
 *   - A and B form two low-resolution images that are bilinearly upscaled,
 *     with blocks centred on their area and the texture wrapping around;
 *   - each pixel blends the upscaled A and B by its modulation value.
 * Output is RGBA8 in the stored row order. */
#include "gl_internal.h"
#include <string.h>

static int imax(int a, int b) { return a > b ? a : b; }
static void blocks(int w, int h, int bpp2, int *bw, int *bh) { *bw = imax((bpp2 ? w / 8 : w / 4), 2); *bh = imax(h / 4, 2); }
size_t rt_pvrtc_size(int w, int h, int bpp2) { int bw, bh; blocks(w, h, bpp2, &bw, &bh); return (size_t)bw * (size_t)bh * 8; }

static unsigned twiddle(unsigned xs, unsigned ys, unsigned x, unsigned y) {
    unsigned mn = xs < ys ? xs : ys, mx = ys < xs ? x : y, tw = 0, src = 1, dst = 1, shift = 0;
    while (src < mn) {
        if (y & src) tw |= dst;
        if (x & src) tw |= dst << 1;
        src <<= 1; dst <<= 2; shift++;
    }
    return tw | (mx >> shift) << (2 * shift);
}

struct col { int r, g, b, a; };
static int e5(unsigned v) { return (int)(v << 3 | v >> 2); }
static int e4(unsigned v) { return (int)(v << 4 | v); }
static int e3(unsigned v) { return (int)(v << 5 | v << 2 | v >> 1); }
static void colours(uint32_t cw, struct col *A, struct col *B) {
    unsigned a = cw & 0xffff, b = cw >> 16;
    if (a & 0x8000) { A->r = e5(a >> 10 & 31); A->g = e5(a >> 5 & 31); A->b = e4(a >> 1 & 15); A->a = 255; }
    else { A->a = (int)(a >> 12 & 7) * 2 * 17; A->r = e4(a >> 8 & 15); A->g = e4(a >> 4 & 15); A->b = e3(a >> 1 & 7); }
    if (b & 0x8000) { B->r = e5(b >> 10 & 31); B->g = e5(b >> 5 & 31); B->b = e5(b & 31); B->a = 255; }
    else { B->a = (int)(b >> 12 & 7) * 2 * 17; B->r = e4(b >> 8 & 15); B->g = e4(b >> 4 & 15); B->b = e4(b & 15); }
}

/* modulation weight (0..8) of pixel (px, py) inside its block, and punch-through */
static int weight(uint32_t mod, uint32_t cw, int px, int py, int bpp2, int *punch, const uint32_t *nmod, const uint32_t *ncw) {
    int mode = (int)(cw & 1);
    *punch = 0;
    if (!bpp2) {
        int v = (int)(mod >> ((py * 4 + px) * 2) & 3);
        if (!mode) { static const int w[4] = { 0, 3, 5, 8 }; return w[v]; }
        if (v == 2) *punch = 1;
        static const int w[4] = { 0, 4, 4, 8 }; return w[v];
    }
    if (!mode) return (mod >> (py * 8 + px) & 1) ? 8 : 0;
    /* 2 bpp interpolated mode: 2-bit values on a checkerboard, the others from their neighbours */
    (void)nmod; (void)ncw;
    static const int w2[4] = { 0, 3, 5, 8 };
    if (((px ^ py) & 1) == 0) {
        int i = (py * 8 + px) / 2;
        return w2[mod >> (i * 2) & 3];
    }
    int sum = 0, n = 0;
    static const int dx[4] = { -1, 1, 0, 0 }, dy[4] = { 0, 0, -1, 1 };
    for (int k = 0; k < 4; k++) {
        int qx = px + dx[k], qy = py + dy[k];
        if (qx < 0 || qx > 7 || qy < 0 || qy > 3) continue;
        int i = (qy * 8 + qx) / 2; sum += w2[mod >> (i * 2) & 3]; n++;
    }
    return n ? (sum + n / 2) / n : 4;
}

void rt_pvrtc_decode(const uint8_t *src, int w, int h, int bpp2, uint8_t *out) {
    int bw, bh; blocks(w, h, bpp2, &bw, &bh);
    int bxs = bpp2 ? 8 : 4, bys = 4;
    #define BLK(x, y) (src + (size_t)twiddle((unsigned)bw, (unsigned)bh, (unsigned)(((x) % bw + bw) % bw), (unsigned)(((y) % bh + bh) % bh)) * 8)
    #define U32(p) ((uint32_t)(p)[0] | (uint32_t)(p)[1] << 8 | (uint32_t)(p)[2] << 16 | (uint32_t)(p)[3] << 24)
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            /* the four blocks around the pixel, measured from block centres */
            int fx = x - bxs / 2, fy = y - bys / 2;
            int b0x = fx >= 0 ? fx / bxs : -((-fx + bxs - 1) / bxs), b0y = fy >= 0 ? fy / bys : -((-fy + bys - 1) / bys);
            int wx = fx - b0x * bxs, wy = fy - b0y * bys;    /* 0..bxs-1, 0..bys-1 */
            struct col A[4], B[4];
            for (int k = 0; k < 4; k++) { const uint8_t *b = BLK(b0x + (k & 1), b0y + (k >> 1)); colours(U32(b + 4), &A[k], &B[k]); }
            int w00 = (bxs - wx) * (bys - wy), w10 = wx * (bys - wy), w01 = (bxs - wx) * wy, w11 = wx * wy, tot = bxs * bys;
            struct col a, b;
            #define LERP4(dst, S, f) dst.f = (S[0].f * w00 + S[1].f * w10 + S[2].f * w01 + S[3].f * w11 + tot / 2) / tot
            LERP4(a, A, r); LERP4(a, A, g); LERP4(a, A, b); LERP4(a, A, a);
            LERP4(b, B, r); LERP4(b, B, g); LERP4(b, B, b); LERP4(b, B, a);
            const uint8_t *own = BLK(x / bxs, y / bys);
            int punch; int m = weight(U32(own), U32(own + 4), x % bxs, y % bys, bpp2, &punch, NULL, NULL);
            uint8_t *o = out + ((size_t)y * w + x) * 4;
            o[0] = (uint8_t)((a.r * (8 - m) + b.r * m + 4) / 8);
            o[1] = (uint8_t)((a.g * (8 - m) + b.g * m + 4) / 8);
            o[2] = (uint8_t)((a.b * (8 - m) + b.b * m + 4) / 8);
            o[3] = punch ? 0 : (uint8_t)((a.a * (8 - m) + b.a * m + 4) / 8);
        }
    }
    #undef BLK
    #undef U32
    #undef LERP4
}
