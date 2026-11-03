/*
 * iosrt software rasterizer. Everything UIKit/CoreGraphics/QuartzCore draws
 * ends up here: antialiased path fills (nonzero / even-odd), strokes,
 * images under any affine transform, glyphs, clipping (rect + coverage mask).
 *
 * Pixels are premultiplied RGBA, byte order R,G,B,A in memory (what Android's
 * ARGB_8888 Bitmap and stb_image_write expect), i.e. 0xAABBGGRR as uint32.
 */
#ifndef IOSRT_GFX_H
#define IOSRT_GFX_H
#include <stdint.h>
#include <stddef.h>

typedef struct { double a, b, c, d, tx, ty; } gfx_mat;   /* == CGAffineTransform */
static inline gfx_mat gfx_mat_identity(void) { gfx_mat m = { 1, 0, 0, 1, 0, 0 }; return m; }
gfx_mat gfx_mat_mul(gfx_mat first, gfx_mat then);         /* apply `first`, then `then` */
gfx_mat gfx_mat_invert(gfx_mat m);
static inline void gfx_mat_apply(const gfx_mat *m, double x, double y, double *ox, double *oy) {
    *ox = m->a * x + m->c * y + m->tx; *oy = m->b * x + m->d * y + m->ty;
}

/* ---- images (refcounted, premultiplied RGBA) */
struct gfx_image { int w, h; uint32_t *px; int refs; };
struct gfx_image *gfx_image_new(int w, int h);           /* zeroed, refs = 1 */
struct gfx_image *gfx_image_retain(struct gfx_image *im);
void gfx_image_release(struct gfx_image *im);
struct gfx_image *gfx_image_decode(const void *data, size_t len);   /* PNG/JPEG/GIF/BMP (CgBI ok) */
struct gfx_image *gfx_image_load_file(const char *path);
/* encoders: malloc'ed buffer, *len set */
void *gfx_image_encode_png(const struct gfx_image *im, size_t *len);
void *gfx_image_encode_jpeg(const struct gfx_image *im, int quality, size_t *len);
int gfx_image_write_png(const struct gfx_image *im, const char *path);

/* ---- path in device space, flattened to polygons */
struct gfx_poly { float *xy; int n, cap; int *starts; uint8_t *closed; int ncont, ccap; };
void gfx_poly_init(struct gfx_poly *p);
void gfx_poly_free(struct gfx_poly *p);
void gfx_poly_move(struct gfx_poly *p, float x, float y);
void gfx_poly_line(struct gfx_poly *p, float x, float y);
void gfx_poly_cubic(struct gfx_poly *p, float x1, float y1, float x2, float y2, float x3, float y3);
void gfx_poly_quad(struct gfx_poly *p, float x1, float y1, float x2, float y2);
void gfx_poly_close(struct gfx_poly *p);
void gfx_poly_bounds(const struct gfx_poly *p, float *x0, float *y0, float *x1, float *y1);

enum { GFX_CAP_BUTT, GFX_CAP_ROUND, GFX_CAP_SQUARE };
enum { GFX_JOIN_MITER, GFX_JOIN_ROUND, GFX_JOIN_BEVEL };
struct gfx_stroke_style { float width; int cap, join; float miter; const float *dash; int ndash; float dash_phase; };
/* outline of a stroke as positively oriented contours (fill with nonzero) */
void gfx_stroke_to_poly(const struct gfx_poly *path, const struct gfx_stroke_style *st, struct gfx_poly *out);

/* ---- surfaces and painting */
struct gfx_surface {
    uint32_t *px; int w, h, stride;          /* stride in pixels */
    int cx0, cy0, cx1, cy1;                  /* clip rect, device pixels, [x0,x1) */
    uint8_t *mask;                           /* optional coverage clip (w*h), NULL = none */
    int bgra;                                /* memory order B,G,R,A instead of R,G,B,A */
};
void gfx_surface_init(struct gfx_surface *s, uint32_t *px, int w, int h, int stride);
void gfx_surface_reset_clip(struct gfx_surface *s);

enum { GFX_BLEND_NORMAL, GFX_BLEND_COPY, GFX_BLEND_CLEAR, GFX_BLEND_MULTIPLY, GFX_BLEND_SCREEN, GFX_BLEND_DEST_OUT, GFX_BLEND_DEST_IN };
enum { GFX_PAINT_SOLID, GFX_PAINT_LINEAR, GFX_PAINT_RADIAL, GFX_PAINT_IMAGE };
#define GFX_MAX_STOPS 16
struct gfx_paint {
    int kind; int blend; float alpha;          /* alpha multiplies everything */
    float color[4];                             /* solid: straight RGBA 0..1 */
    /* gradients (device space) */
    int nstops; float stop_pos[GFX_MAX_STOPS]; float stop_rgba[GFX_MAX_STOPS][4];
    float gx0, gy0, gx1, gy1, gr0, gr1; int extend_start, extend_end;
    /* image: device -> image pixel space */
    const struct gfx_image *image; gfx_mat inv; int smooth;
    const float *tint;                          /* image used as alpha mask, painted with tint (template images) */
};
enum { GFX_FILL_NONZERO, GFX_FILL_EVENODD };
void gfx_fill_poly(struct gfx_surface *s, const struct gfx_poly *p, int rule, const struct gfx_paint *paint);
void gfx_fill_rect(struct gfx_surface *s, float x0, float y0, float x1, float y1, const struct gfx_paint *paint); /* axis aligned */
/* draw an image so that image pixel space (0..w,0..h) maps through `m` to device */
void gfx_draw_image(struct gfx_surface *s, const struct gfx_image *im, gfx_mat m, float alpha, int blend, const float *tint);
/* intersect the clip with a polygon (builds/updates the coverage mask) */
void gfx_clip_poly(struct gfx_surface *s, const struct gfx_poly *p, int rule);
/* intersect the clip with an image's alpha channel mapped through m */
void gfx_clip_image_mask(struct gfx_surface *s, const struct gfx_image *im, gfx_mat m, int use_luminance);

/* ---- text */
struct gfx_font;                               /* opaque, loaded lazily */
struct gfx_font *gfx_font_get(int bold);       /* NULL if no font file is available */
struct gfx_font *gfx_font_from_memory(unsigned char *data, size_t len);   /* takes ownership of data */
int gfx_font_name(struct gfx_font *f, int name_id, char *out, size_t n); /* name table (6 = PostScript) */
/* metrics in units of point size 1.0 */
void gfx_font_vmetrics(struct gfx_font *f, float *ascent, float *descent, float *linegap);
float gfx_text_width(struct gfx_font *f, const char *utf8, size_t n, float size);   /* approximate if f == NULL */
/* draw utf8 with baseline origin (x,y) in device pixels at device pixel size; color straight RGBA */
void gfx_draw_text(struct gfx_surface *s, struct gfx_font *f, const char *utf8, size_t n, float x, float y, float px_size, const float *rgba, int fake_bold);

#endif
