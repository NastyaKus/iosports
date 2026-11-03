/*
 * CoreGraphics HLE on top of the gfx rasterizer: CGContext (graphics state
 * stack, CTM, clipping, paths, images, gradients), bitmap contexts in the
 * common pixel layouts, CGPath, CGImage, CGColor(Space), CGGradient,
 * CGDataProvider, CGImageSource/Destination, CGFont, UIGraphics image
 * contexts and UIBezierPath.
 *
 * All CG "Ref" types are ObjC objects so CFRetain/CFRelease work on them.
 * CGColorRef is a UIColor. Device space is y-down pixels; a CG bitmap context
 * starts with a flipped CTM (y-up user space) and UIKit contexts with a
 * y-down CTM scaled by the screen scale, exactly as on iOS.
 */
#include "uikit_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>

RT_DECLARE_CLASS(NSObject);
RT_DECLARE_CLASS(UIColor);
RT_DECLARE_CLASS(NSArray);
RT_DEFINE_CLASS(CGContextImpl);
RT_DEFINE_CLASS(CGPathImpl);
RT_DEFINE_CLASS(CGImageImpl);
RT_DEFINE_CLASS(CGColorSpaceImpl);
RT_DEFINE_CLASS(CGGradientImpl);
RT_DEFINE_CLASS(CGDataProviderImpl);
RT_DEFINE_CLASS(CGImageSourceImpl);
RT_DEFINE_CLASS(CGImageDestinationImpl);
RT_DEFINE_CLASS(CGFontImpl);
RT_DEFINE_CLASS(UIBezierPath);

id rt_data_new(const void *b, size_t n);
const void *rt_data_bytes(id d, size_t *n);
void rt_font_register(const char *name, struct gfx_font *gf);

/* =================================================================== paths */
enum { PE_MOVE, PE_LINE, PE_QUAD, PE_CUBIC, PE_CLOSE };
struct pel { int type; CGPoint p[3]; };
enum { H_EMPTY, H_MOVE, H_RECT, H_OVAL, H_LINE, H_GENERIC };
struct pathbuf { struct pel *e; int n, cap; int hint; CGRect hrect; CGPoint start, cur; int has_cur; };
struct cgpath { Class isa; struct pathbuf pb; };

static void pb_free(struct pathbuf *pb) { free(pb->e); memset(pb, 0, sizeof *pb); }
static void pb_copy(struct pathbuf *d, const struct pathbuf *s) {
    *d = *s; d->e = NULL; d->cap = s->n;
    if (s->n) { d->e = malloc(sizeof *d->e * (size_t)s->n); memcpy(d->e, s->e, sizeof *d->e * (size_t)s->n); }
}
static struct pel *pb_push(struct pathbuf *pb, int type) {
    if (pb->n == pb->cap) { pb->cap = pb->cap ? pb->cap * 2 : 16; pb->e = realloc(pb->e, sizeof *pb->e * (size_t)pb->cap); }
    struct pel *e = &pb->e[pb->n++]; memset(e, 0, sizeof *e); e->type = type; return e;
}
static void pb_set_hint(struct pathbuf *pb, int n_before, int kind, CGRect r) {
    if (kind == H_LINE && pb->hint == H_MOVE && n_before == 1) { pb->hint = H_LINE; return; }
    if (n_before == 0) { pb->hint = kind; pb->hrect = r; }
    else pb->hint = H_GENERIC;
}
static inline CGPoint tp(const gfx_mat *m, double x, double y) { CGPoint p = { x, y }; if (m) { double ox, oy; gfx_mat_apply(m, x, y, &ox, &oy); p.x = ox; p.y = oy; } return p; }
static void pb_move(struct pathbuf *pb, CGPoint p) {
    int nb = pb->n;
    if (pb->n && pb->e[pb->n - 1].type == PE_MOVE) pb->e[pb->n - 1].p[0] = p;
    else pb_push(pb, PE_MOVE)->p[0] = p;
    pb->start = pb->cur = p; pb->has_cur = 1;
    if (nb == 0) { pb->hint = H_MOVE; pb->hrect = (CGRect){ p, { 0, 0 } }; } else if (pb->hint != H_MOVE || nb != 1) pb->hint = H_GENERIC;
}
static void pb_ensure_start(struct pathbuf *pb) { if (!pb->has_cur) pb_move(pb, (CGPoint){ 0, 0 }); }
static void pb_line(struct pathbuf *pb, CGPoint p) {
    pb_ensure_start(pb); int nb = pb->n;
    CGPoint a = pb->cur;
    pb_push(pb, PE_LINE)->p[0] = p; pb->cur = p;
    double x0 = fmin(a.x, p.x), y0 = fmin(a.y, p.y);
    pb_set_hint(pb, nb, H_LINE, (CGRect){ { x0, y0 }, { fabs(p.x - a.x), fabs(p.y - a.y) } });
}
static void pb_quad(struct pathbuf *pb, CGPoint c, CGPoint p) { pb_ensure_start(pb); struct pel *e = pb_push(pb, PE_QUAD); e->p[0] = c; e->p[1] = p; pb->cur = p; pb->hint = H_GENERIC; }
static void pb_cubic(struct pathbuf *pb, CGPoint c1, CGPoint c2, CGPoint p) { pb_ensure_start(pb); struct pel *e = pb_push(pb, PE_CUBIC); e->p[0] = c1; e->p[1] = c2; e->p[2] = p; pb->cur = p; pb->hint = pb->hint == H_OVAL ? H_OVAL : H_GENERIC; }
static void pb_close(struct pathbuf *pb) { if (!pb->n || pb->e[pb->n - 1].type == PE_CLOSE) return; pb_push(pb, PE_CLOSE); pb->cur = pb->start; }
static void pb_rect(struct pathbuf *pb, const gfx_mat *m, CGRect r) {
    int nb = pb->n;
    double x0 = r.origin.x, y0 = r.origin.y, x1 = x0 + r.size.width, y1 = y0 + r.size.height;
    pb_move(pb, tp(m, x0, y0)); pb_line(pb, tp(m, x1, y0)); pb_line(pb, tp(m, x1, y1)); pb_line(pb, tp(m, x0, y1)); pb_close(pb);
    pb->hint = H_GENERIC; pb_set_hint(pb, nb, H_RECT, r);
}
#define KAPPA 0.5522847498307936
static void pb_ellipse(struct pathbuf *pb, const gfx_mat *m, CGRect r) {
    int nb = pb->n;
    double cx = r.origin.x + r.size.width / 2, cy = r.origin.y + r.size.height / 2, rx = r.size.width / 2, ry = r.size.height / 2;
    double kx = rx * KAPPA, ky = ry * KAPPA;
    pb_move(pb, tp(m, cx + rx, cy));
    pb_cubic(pb, tp(m, cx + rx, cy + ky), tp(m, cx + kx, cy + ry), tp(m, cx, cy + ry));
    pb_cubic(pb, tp(m, cx - kx, cy + ry), tp(m, cx - rx, cy + ky), tp(m, cx - rx, cy));
    pb_cubic(pb, tp(m, cx - rx, cy - ky), tp(m, cx - kx, cy - ry), tp(m, cx, cy - ry));
    pb_cubic(pb, tp(m, cx + kx, cy - ry), tp(m, cx + rx, cy - ky), tp(m, cx + rx, cy));
    pb_close(pb);
    pb->hint = H_GENERIC; pb_set_hint(pb, nb, H_OVAL, r);
}
/* arc around (cx,cy); clockwise != 0 means decreasing angle (CG convention) */
static void pb_arc(struct pathbuf *pb, const gfx_mat *m, double cx, double cy, double r, double a0, double a1, int clockwise) {
    double sweep = a1 - a0;
    if (clockwise) { if (sweep > 0) sweep = fmod(sweep, 2 * M_PI) - 2 * M_PI; if (sweep < -2 * M_PI) sweep = -2 * M_PI; }
    else { if (sweep < 0) sweep = fmod(sweep, 2 * M_PI) + 2 * M_PI; if (sweep > 2 * M_PI) sweep = 2 * M_PI; }
    if (a1 - a0 != 0 && fabs(sweep) < 1e-12) sweep = 0;
    CGPoint s = tp(m, cx + r * cos(a0), cy + r * sin(a0));
    if (pb->has_cur && pb->n) pb_line(pb, s); else pb_move(pb, s);
    int nseg = (int)ceil(fabs(sweep) / (M_PI / 2) - 1e-9); if (nseg < 1) nseg = 1;
    double step = sweep / nseg, a = a0;
    if (fabs(sweep) < 1e-12) return;
    for (int i = 0; i < nseg; i++) {
        double b = a + step, k = 4.0 / 3.0 * tan(step / 4);
        double x0 = cos(a), y0 = sin(a), x1 = cos(b), y1 = sin(b);
        pb_cubic(pb, tp(m, cx + r * (x0 - k * y0), cy + r * (y0 + k * x0)), tp(m, cx + r * (x1 + k * y1), cy + r * (y1 - k * x1)), tp(m, cx + r * x1, cy + r * y1));
        a = b;
    }
    pb->hint = H_GENERIC;
}
/* tangent arc from the current point through corner (x1,y1) towards (x2,y2); pts in user space, cur in user space */
static void pb_arc_to(struct pathbuf *pb, const gfx_mat *m, CGPoint cur_user, double x1, double y1, double x2, double y2, double r) {
    double x0 = cur_user.x, y0 = cur_user.y;
    double v1x = x0 - x1, v1y = y0 - y1, v2x = x2 - x1, v2y = y2 - y1;
    double l1 = hypot(v1x, v1y), l2 = hypot(v2x, v2y);
    if (l1 < 1e-9 || l2 < 1e-9 || r <= 0) { pb_line(pb, tp(m, x1, y1)); return; }
    v1x /= l1; v1y /= l1; v2x /= l2; v2y /= l2;
    double cosang = v1x * v2x + v1y * v2y, ang = acos(fmax(-1, fmin(1, cosang)));
    if (fabs(sin(ang)) < 1e-9) { pb_line(pb, tp(m, x1, y1)); return; }
    double d = r / tan(ang / 2);
    double t1x = x1 + v1x * d, t1y = y1 + v1y * d, t2x = x1 + v2x * d, t2y = y1 + v2y * d;
    double bx = v1x + v2x, by = v1y + v2y, bl = hypot(bx, by); bx /= bl; by /= bl;
    double h = r / sin(ang / 2), cx = x1 + bx * h, cy = y1 + by * h;
    double a0 = atan2(t1y - cy, t1x - cx), a1 = atan2(t2y - cy, t2x - cx);
    double cross = v1x * v2y - v1y * v2x;   /* turning direction */
    pb_line(pb, tp(m, t1x, t1y));
    pb_arc(pb, m, cx, cy, r, a0, a1, cross < 0 ? 0 : 1);
}
static void pb_append(struct pathbuf *d, const struct pathbuf *s, const gfx_mat *m) {
    int nb = d->n; int hint = s->hint; CGRect hr = s->hrect;
    for (int i = 0; i < s->n; i++) {
        const struct pel *e = &s->e[i];
        switch (e->type) {
        case PE_MOVE: pb_move(d, tp(m, e->p[0].x, e->p[0].y)); break;
        case PE_LINE: pb_line(d, tp(m, e->p[0].x, e->p[0].y)); break;
        case PE_QUAD: pb_quad(d, tp(m, e->p[0].x, e->p[0].y), tp(m, e->p[1].x, e->p[1].y)); break;
        case PE_CUBIC: pb_cubic(d, tp(m, e->p[0].x, e->p[0].y), tp(m, e->p[1].x, e->p[1].y), tp(m, e->p[2].x, e->p[2].y)); break;
        case PE_CLOSE: pb_close(d); break;
        }
    }
    if (nb == 0) { d->hint = hint; d->hrect = hr; } else d->hint = H_GENERIC;
}
static void pb_to_poly(const struct pathbuf *pb, const gfx_mat *m, struct gfx_poly *p) {
    for (int i = 0; i < pb->n; i++) {
        const struct pel *e = &pb->e[i];
        CGPoint a = e->p[0], b = e->p[1], c = e->p[2];
        if (m) { a = tp(m, a.x, a.y); b = tp(m, b.x, b.y); c = tp(m, c.x, c.y); }
        switch (e->type) {
        case PE_MOVE: gfx_poly_move(p, (float)a.x, (float)a.y); break;
        case PE_LINE: gfx_poly_line(p, (float)a.x, (float)a.y); break;
        case PE_QUAD: gfx_poly_quad(p, (float)a.x, (float)a.y, (float)b.x, (float)b.y); break;
        case PE_CUBIC: gfx_poly_cubic(p, (float)a.x, (float)a.y, (float)b.x, (float)b.y, (float)c.x, (float)c.y); break;
        case PE_CLOSE: gfx_poly_close(p); break;
        }
    }
}
static CGRect pb_bounds(const struct pathbuf *pb, int control_points) {
    double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
    if (control_points) {
        for (int i = 0; i < pb->n; i++) {
            int k = pb->e[i].type == PE_QUAD ? 2 : pb->e[i].type == PE_CUBIC ? 3 : pb->e[i].type == PE_CLOSE ? 0 : 1;
            for (int j = 0; j < k; j++) { CGPoint q = pb->e[i].p[j]; x0 = fmin(x0, q.x); y0 = fmin(y0, q.y); x1 = fmax(x1, q.x); y1 = fmax(y1, q.y); }
        }
    } else {
        struct gfx_poly p; gfx_poly_init(&p); pb_to_poly(pb, NULL, &p);
        for (int i = 0; i < p.n; i++) { x0 = fmin(x0, p.xy[i * 2]); y0 = fmin(y0, p.xy[i * 2 + 1]); x1 = fmax(x1, p.xy[i * 2]); y1 = fmax(y1, p.xy[i * 2 + 1]); }
        gfx_poly_free(&p);
    }
    if (x0 > x1) return (CGRect){ { INFINITY, INFINITY }, { 0, 0 } };
    return (CGRect){ { x0, y0 }, { x1 - x0, y1 - y0 } };
}
static int poly_contains(const struct gfx_poly *p, double x, double y, int eo) {
    int wind = 0;
    for (int k = 0; k < p->ncont; k++) {
        int s = p->starts[k], e = k + 1 < p->ncont ? p->starts[k + 1] : p->n;
        for (int i = s; i < e; i++) {
            int j = i + 1 < e ? i + 1 : s;
            double ax = p->xy[i * 2], ay = p->xy[i * 2 + 1], bx = p->xy[j * 2], by = p->xy[j * 2 + 1];
            if ((ay <= y) != (by <= y)) {
                double xc = ax + (y - ay) * (bx - ax) / (by - ay);
                if (xc > x) wind += by > ay ? 1 : -1;
            }
        }
    }
    return eo ? (wind & 1) : wind != 0;
}

/* CGPath API */
static void path_dealloc(id self, SEL s) { (void)s; pb_free(&((struct cgpath *)self)->pb); rt_dealloc_instance(self); }
static struct pathbuf *PB(id p) { return p ? &((struct cgpath *)p)->pb : NULL; }
id iosrt_CGPathCreateMutable(void) { return rt_alloc_instance(RT_CLASS(CGPathImpl), 0); }
id iosrt_CGPathCreateCopy(id p) { id n = iosrt_CGPathCreateMutable(); if (p) pb_copy(PB(n), PB(p)); return n; }
id iosrt_CGPathCreateMutableCopy(id p) { return iosrt_CGPathCreateCopy(p); }
id iosrt_CGPathCreateCopyByTransformingPath(id p, const gfx_mat *m) { id n = iosrt_CGPathCreateMutable(); if (p) pb_append(PB(n), PB(p), m); return n; }
id iosrt_CGPathCreateMutableCopyByTransformingPath(id p, const gfx_mat *m) { return iosrt_CGPathCreateCopyByTransformingPath(p, m); }
id iosrt_CGPathRetain(id p) { return objc_retain(p); }
void iosrt_CGPathRelease(id p) { objc_release(p); }
void iosrt_CGPathMoveToPoint(id p, const gfx_mat *m, double x, double y) { if (p) pb_move(PB(p), tp(m, x, y)); }
void iosrt_CGPathAddLineToPoint(id p, const gfx_mat *m, double x, double y) { if (p) pb_line(PB(p), tp(m, x, y)); }
void iosrt_CGPathAddQuadCurveToPoint(id p, const gfx_mat *m, double cx, double cy, double x, double y) { if (p) pb_quad(PB(p), tp(m, cx, cy), tp(m, x, y)); }
void iosrt_CGPathAddCurveToPoint(id p, const gfx_mat *m, double c1x, double c1y, double c2x, double c2y, double x, double y) { if (p) pb_cubic(PB(p), tp(m, c1x, c1y), tp(m, c2x, c2y), tp(m, x, y)); }
void iosrt_CGPathCloseSubpath(id p) { if (p) pb_close(PB(p)); }
void iosrt_CGPathAddRect(id p, const gfx_mat *m, CGRect r) { if (p) pb_rect(PB(p), m, r); }
void iosrt_CGPathAddRects(id p, const gfx_mat *m, const CGRect *r, size_t n) { for (size_t i = 0; i < n; i++) iosrt_CGPathAddRect(p, m, r[i]); }
void iosrt_CGPathAddLines(id p, const gfx_mat *m, const CGPoint *pts, size_t n) { for (size_t i = 0; i < n; i++) { if (i) iosrt_CGPathAddLineToPoint(p, m, pts[i].x, pts[i].y); else iosrt_CGPathMoveToPoint(p, m, pts[i].x, pts[i].y); } }
void iosrt_CGPathAddEllipseInRect(id p, const gfx_mat *m, CGRect r) { if (p) pb_ellipse(PB(p), m, r); }
void iosrt_CGPathAddArc(id p, const gfx_mat *m, double x, double y, double r, double a0, double a1, long cw) { if (p) pb_arc(PB(p), m, x, y, r, a0, a1, (int)(cw & 0xff)); }
void iosrt_CGPathAddRelativeArc(id p, const gfx_mat *m, double x, double y, double r, double a0, double d) { if (p) pb_arc(PB(p), m, x, y, r, a0, a0 + d, d < 0); }
static CGPoint user_cur(struct pathbuf *pb, const gfx_mat *m) {
    CGPoint c = pb->cur;
    if (m) { gfx_mat inv = gfx_mat_invert(*m); c = tp(&inv, c.x, c.y); }
    return c;
}
void iosrt_CGPathAddArcToPoint(id p, const gfx_mat *m, double x1, double y1, double x2, double y2, double r) {
    if (!p) return;
    struct pathbuf *pb = PB(p);
    if (!pb->has_cur) { pb_move(pb, tp(m, x1, y1)); return; }
    pb_arc_to(pb, m, user_cur(pb, m), x1, y1, x2, y2, r);
}
void iosrt_CGPathAddRoundedRect(id p, const gfx_mat *m, CGRect r, double cw, double ch) {
    if (!p) return;
    struct pathbuf *pb = PB(p);
    if (cw <= 0 || ch <= 0) { pb_rect(pb, m, r); return; }
    cw = fmin(cw, r.size.width / 2); ch = fmin(ch, r.size.height / 2);
    double x0 = r.origin.x, y0 = r.origin.y, x1 = x0 + r.size.width, y1 = y0 + r.size.height, kx = cw * KAPPA, ky = ch * KAPPA;
    pb_move(pb, tp(m, x0 + cw, y0));
    pb_line(pb, tp(m, x1 - cw, y0)); pb_cubic(pb, tp(m, x1 - cw + kx, y0), tp(m, x1, y0 + ch - ky), tp(m, x1, y0 + ch));
    pb_line(pb, tp(m, x1, y1 - ch)); pb_cubic(pb, tp(m, x1, y1 - ch + ky), tp(m, x1 - cw + kx, y1), tp(m, x1 - cw, y1));
    pb_line(pb, tp(m, x0 + cw, y1)); pb_cubic(pb, tp(m, x0 + cw - kx, y1), tp(m, x0, y1 - ch + ky), tp(m, x0, y1 - ch));
    pb_line(pb, tp(m, x0, y0 + ch)); pb_cubic(pb, tp(m, x0, y0 + ch - ky), tp(m, x0 + cw - kx, y0), tp(m, x0 + cw, y0));
    pb_close(pb); pb->hint = H_GENERIC;
}
void iosrt_CGPathAddPath(id p, const gfx_mat *m, id q) { if (p && q) pb_append(PB(p), PB(q), m); }
id iosrt_CGPathCreateWithRect(CGRect r, const gfx_mat *m) { id p = iosrt_CGPathCreateMutable(); iosrt_CGPathAddRect(p, m, r); return p; }
id iosrt_CGPathCreateWithEllipseInRect(CGRect r, const gfx_mat *m) { id p = iosrt_CGPathCreateMutable(); iosrt_CGPathAddEllipseInRect(p, m, r); return p; }
id iosrt_CGPathCreateWithRoundedRect(CGRect r, double cw, double ch, const gfx_mat *m) { id p = iosrt_CGPathCreateMutable(); iosrt_CGPathAddRoundedRect(p, m, r, cw, ch); return p; }
rt_bool iosrt_CGPathIsEmpty(id p) { return !p || PB(p)->n == 0; }
rt_bool iosrt_CGPathIsRect(id p, CGRect *r) { if (!p || PB(p)->hint != H_RECT) return 0; if (r) *r = PB(p)->hrect; return 1; }
CGRect iosrt_CGPathGetBoundingBox(id p) { return p ? pb_bounds(PB(p), 1) : (CGRect){ { INFINITY, INFINITY }, { 0, 0 } }; }
CGRect iosrt_CGPathGetPathBoundingBox(id p) { return p ? pb_bounds(PB(p), 0) : (CGRect){ { INFINITY, INFINITY }, { 0, 0 } }; }
CGPoint iosrt_CGPathGetCurrentPoint(id p) { return p ? PB(p)->cur : (CGPoint){ 0, 0 }; }
rt_bool iosrt_CGPathContainsPoint(id p, const gfx_mat *m, CGPoint pt, long eo) {
    if (!p) return 0;
    struct gfx_poly poly; gfx_poly_init(&poly); pb_to_poly(PB(p), m, &poly);
    int r = poly_contains(&poly, pt.x, pt.y, (int)(eo & 0xff));
    gfx_poly_free(&poly); return r;
}
rt_bool iosrt_CGPathEqualToPath(id a, id b) {
    if (a == b) return 1;
    if (!a || !b || PB(a)->n != PB(b)->n) return 0;
    return !memcmp(PB(a)->e, PB(b)->e, sizeof(struct pel) * (size_t)PB(a)->n);
}
struct cgpath_element { int type; CGPoint *points; };
void iosrt_CGPathApply(id p, void *info, void (*fn)(void *, const struct cgpath_element *)) {
    if (!p || !fn) return;
    struct pathbuf *pb = PB(p);
    for (int i = 0; i < pb->n; i++) { struct cgpath_element el = { pb->e[i].type, pb->e[i].p }; fn(info, &el); }
}
static id path_copy_stroked(const struct pathbuf *pb, const gfx_mat *m, double lw, int cap, int join, double miter);
id iosrt_CGPathCreateCopyByStrokingPath(id p, const gfx_mat *m, double lw, int cap, int join, double miter) {
    return p ? path_copy_stroked(PB(p), m, lw, cap, join, miter) : iosrt_CGPathCreateMutable();
}
static id poly_to_path(const struct gfx_poly *o) {
    id r = iosrt_CGPathCreateMutable(); struct pathbuf *rb = PB(r);
    for (int k = 0; k < o->ncont; k++) {
        int s = o->starts[k], e = k + 1 < o->ncont ? o->starts[k + 1] : o->n;
        for (int i = s; i < e; i++) { CGPoint q = { o->xy[i * 2], o->xy[i * 2 + 1] }; if (i == s) pb_move(rb, q); else pb_line(rb, q); }
        pb_close(rb);
    }
    rb->hint = H_GENERIC;
    return r;
}
static id path_copy_stroked(const struct pathbuf *pb, const gfx_mat *m, double lw, int cap, int join, double miter) {
    struct gfx_poly p, o; gfx_poly_init(&p); gfx_poly_init(&o);
    pb_to_poly(pb, m, &p);
    struct gfx_stroke_style st = { (float)lw, cap, join, (float)miter, NULL, 0, 0 };
    gfx_stroke_to_poly(&p, &st, &o);
    id r = poly_to_path(&o);
    gfx_poly_free(&p); gfx_poly_free(&o);
    return r;
}

/* ============================================================== colors */
struct cgspace { Class isa; int ncomp; int pattern; };
static id g_rgb_space, g_gray_space;
id iosrt_CGColorSpaceCreateDeviceRGB(void) { id s = rt_alloc_instance(RT_CLASS(CGColorSpaceImpl), 0); ((struct cgspace *)s)->ncomp = 3; return s; }
id iosrt_CGColorSpaceCreateDeviceGray(void) { id s = rt_alloc_instance(RT_CLASS(CGColorSpaceImpl), 0); ((struct cgspace *)s)->ncomp = 1; return s; }
id iosrt_CGColorSpaceCreateWithName(id name) { const char *n = name ? rt_str_utf8(name) : ""; return strstr(n, "Gray") ? iosrt_CGColorSpaceCreateDeviceGray() : iosrt_CGColorSpaceCreateDeviceRGB(); }
id iosrt_CGColorSpaceCreatePattern(id base) { (void)base; id s = iosrt_CGColorSpaceCreateDeviceRGB(); ((struct cgspace *)s)->pattern = 1; return s; }
id iosrt_CGColorSpaceRetain(id s) { return objc_retain(s); }
void iosrt_CGColorSpaceRelease(id s) { objc_release(s); }
size_t iosrt_CGColorSpaceGetNumberOfComponents(id s) { return s ? (size_t)((struct cgspace *)s)->ncomp : 3; }
int iosrt_CGColorSpaceGetModel(id s) { return s && ((struct cgspace *)s)->pattern ? 6 : s && ((struct cgspace *)s)->ncomp == 1 ? 0 : 1; }
id iosrt_kCGColorSpaceSRGB, iosrt_kCGColorSpaceGenericRGB, iosrt_kCGColorSpaceGenericGray, iosrt_kCGColorSpaceDeviceRGB, iosrt_kCGColorSpaceGenericGrayGamma2_2;

id iosrt_CGColorCreate(id space, const double *c) {
    int n = space ? ((struct cgspace *)space)->ncomp : 3;
    id r = rt_alloc_instance(RT_CLASS(UIColor), 0); struct uicolor *u = (struct uicolor *)r;
    if (n == 1) { u->r = u->g = u->b = c[0]; u->a = c[1]; } else { u->r = c[0]; u->g = c[1]; u->b = c[2]; u->a = c[3]; }
    return r;
}
id iosrt_CGColorCreateGenericRGB(double r, double g, double b, double a) { double c[4] = { r, g, b, a }; return iosrt_CGColorCreate(NULL, c); }
id iosrt_CGColorCreateSRGB(double r, double g, double b, double a) { return iosrt_CGColorCreateGenericRGB(r, g, b, a); }
id iosrt_CGColorCreateGenericGray(double g, double a) { return iosrt_CGColorCreateGenericRGB(g, g, g, a); }
id iosrt_CGColorCreateCopyWithAlpha(id c, double a) { struct uicolor *u = (struct uicolor *)c; return u ? iosrt_CGColorCreateGenericRGB(u->r, u->g, u->b, a) : NULL; }
id iosrt_CGColorCreateCopy(id c) { return objc_retain(c); }
id iosrt_CGColorRetain(id c) { return objc_retain(c); }
void iosrt_CGColorRelease(id c) { objc_release(c); }
const double *iosrt_CGColorGetComponents(id c) { static const double zero[4]; return c ? &((struct uicolor *)c)->r : zero; }
size_t iosrt_CGColorGetNumberOfComponents(id c) { (void)c; return 4; }
double iosrt_CGColorGetAlpha(id c) { return c ? ((struct uicolor *)c)->a : 0; }
id iosrt_CGColorGetColorSpace(id c) { (void)c; if (!g_rgb_space) g_rgb_space = iosrt_CGColorSpaceCreateDeviceRGB(); return g_rgb_space; }
rt_bool iosrt_CGColorEqualToColor(id a, id b) {
    if (a == b) return 1;
    if (!a || !b) return 0;
    struct uicolor *x = (struct uicolor *)a, *y = (struct uicolor *)b;
    return x->r == y->r && x->g == y->g && x->b == y->b && x->a == y->a && x->pattern == y->pattern;
}
id iosrt_CGColorGetConstantColor(id name) {
    const char *n = name ? rt_str_utf8(name) : "";
    double v = strstr(n, "White") ? 1 : 0, a = strstr(n, "Clear") ? 0 : 1;
    return objc_autorelease(iosrt_CGColorCreateGenericRGB(v, v, v, a));
}
id iosrt_kCGColorWhite, iosrt_kCGColorBlack, iosrt_kCGColorClear;

/* ============================================================== images */
struct cgimage { Class isa; struct gfx_image *im; long is_mask; long gray; id provider; };
static void img_dealloc(id self, SEL s) { (void)s; struct cgimage *c = (struct cgimage *)self; gfx_image_release(c->im); objc_release(c->provider); rt_dealloc_instance(self); }
id rt_cgimage_new(struct gfx_image *im) {
    if (!im) return NULL;
    id r = rt_alloc_instance(RT_CLASS(CGImageImpl), 0);
    ((struct cgimage *)r)->im = gfx_image_retain(im);
    return r;
}
struct gfx_image *rt_cgimage_gfx(id c) { return c && rt_is_kind_of(c, RT_CLASS(CGImageImpl)) ? ((struct cgimage *)c)->im : NULL; }
id iosrt_CGImageRetain(id i) { return objc_retain(i); }
void iosrt_CGImageRelease(id i) { objc_release(i); }
size_t iosrt_CGImageGetWidth(id i) { struct gfx_image *g = rt_cgimage_gfx(i); return g ? (size_t)g->w : 0; }
size_t iosrt_CGImageGetHeight(id i) { struct gfx_image *g = rt_cgimage_gfx(i); return g ? (size_t)g->h : 0; }
size_t iosrt_CGImageGetBitsPerComponent(id i) { (void)i; return 8; }
size_t iosrt_CGImageGetBitsPerPixel(id i) { return i && ((struct cgimage *)i)->is_mask ? 8 : 32; }
size_t iosrt_CGImageGetBytesPerRow(id i) { return iosrt_CGImageGetWidth(i) * (i && ((struct cgimage *)i)->is_mask ? 1 : 4); }
uint32_t iosrt_CGImageGetAlphaInfo(id i) { return i && ((struct cgimage *)i)->is_mask ? 0 : 1; }   /* PremultipliedLast */
uint32_t iosrt_CGImageGetBitmapInfo(id i) { return iosrt_CGImageGetAlphaInfo(i); }
rt_bool iosrt_CGImageIsMask(id i) { return i && ((struct cgimage *)i)->is_mask; }
id iosrt_CGImageGetColorSpace(id i) { (void)i; return iosrt_CGColorGetColorSpace(NULL); }
struct cgprov { Class isa; id data; void *info; void (*release)(void *, const void *, size_t); const void *rel_ptr; size_t rel_size; };
static id provider_with_data(id data) { id p = rt_alloc_instance(RT_CLASS(CGDataProviderImpl), 0); ((struct cgprov *)p)->data = data; return p; }
id iosrt_CGImageGetDataProvider(id i) {
    struct cgimage *c = (struct cgimage *)i;
    if (!c || !c->im) return NULL;
    if (!c->provider) {
        size_t n = (size_t)c->im->w * c->im->h;
        if (c->is_mask) {
            uint8_t *b = malloc(n);
            for (size_t k = 0; k < n; k++) b[k] = (uint8_t)(c->im->px[k] & 255);
            c->provider = provider_with_data(rt_data_new(b, n)); free(b);
        } else c->provider = provider_with_data(rt_data_new(c->im->px, n * 4));
    }
    return c->provider;
}
id iosrt_CGImageCreateWithImageInRect(id i, CGRect r) {
    struct gfx_image *g = rt_cgimage_gfx(i);
    if (!g) return NULL;
    int x0 = (int)floor(r.origin.x), y0 = (int)floor(r.origin.y), x1 = (int)ceil(r.origin.x + r.size.width), y1 = (int)ceil(r.origin.y + r.size.height);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > g->w) x1 = g->w;
    if (y1 > g->h) y1 = g->h;
    if (x1 <= x0 || y1 <= y0) return NULL;
    struct gfx_image *n = gfx_image_new(x1 - x0, y1 - y0);
    for (int y = y0; y < y1; y++) memcpy(n->px + (size_t)(y - y0) * n->w, g->px + (size_t)y * g->w + x0, (size_t)n->w * 4);
    id res = rt_cgimage_new(n); gfx_image_release(n);
    ((struct cgimage *)res)->is_mask = ((struct cgimage *)i)->is_mask;
    return res;
}
/* alpha a mask contributes at image pixel: image masks are inverse, gray images are alpha */
static float mask_alpha(const struct cgimage *m, int x, int y) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= m->im->w) x = m->im->w - 1;
    if (y >= m->im->h) y = m->im->h - 1;
    uint32_t p = m->im->px[(size_t)y * m->im->w + x];
    float lum = (float)(p & 255) / 255.f;
    if (m->is_mask) return 1 - lum;
    if (m->gray) return lum;
    return (float)(p >> 24) / 255.f;
}
id iosrt_CGImageCreateWithMask(id i, id mask) {
    struct gfx_image *g = rt_cgimage_gfx(i);
    struct cgimage *m = (struct cgimage *)mask;
    if (!g || !m || !m->im) return objc_retain(i);
    struct gfx_image *n = gfx_image_new(g->w, g->h);
    for (int y = 0; y < g->h; y++)
        for (int x = 0; x < g->w; x++) {
            float a = mask_alpha(m, x * m->im->w / g->w, y * m->im->h / g->h);
            uint32_t p = g->px[(size_t)y * g->w + x], r = 0;
            for (int k = 0; k < 32; k += 8) r |= (uint32_t)(((p >> k) & 255) * a + 0.5f) << k;
            n->px[(size_t)y * g->w + x] = r;
        }
    id res = rt_cgimage_new(n); gfx_image_release(n); return res;
}
static const uint8_t *prov_bytes(id prov, size_t *n) {
    struct cgprov *p = (struct cgprov *)prov;
    if (!p || !p->data) { *n = 0; return NULL; }
    return rt_data_bytes(p->data, n);
}
id iosrt_CGImageMaskCreate(size_t w, size_t h, size_t bpc, size_t bpp, size_t bpr, id prov, const double *decode, long interp) {
    (void)bpp; (void)interp;
    size_t n; const uint8_t *b = prov_bytes(prov, &n);
    struct gfx_image *g = gfx_image_new((int)w, (int)h);
    for (size_t y = 0; y < h; y++)
        for (size_t x = 0; x < w; x++) {
            unsigned v = 0;
            if (bpc == 8) { size_t o = y * bpr + x; v = o < n ? b[o] : 0; }
            else if (bpc == 1) { size_t o = y * bpr + x / 8; v = o < n && (b[o] >> (7 - x % 8) & 1) ? 255 : 0; }
            else if (bpc == 4) { size_t o = y * bpr + x / 2; v = o < n ? ((x & 1 ? b[o] : b[o] >> 4) & 15) * 17 : 0; }
            if (decode && decode[0] > decode[1]) v = 255 - v;
            g->px[y * w + x] = v | v << 8 | v << 16 | 0xff000000u;
        }
    id r = rt_cgimage_new(g); gfx_image_release(g);
    ((struct cgimage *)r)->is_mask = 1;
    return r;
}
id iosrt_CGImageCreate(size_t w, size_t h, size_t bpc, size_t bpp, size_t bpr, id space, uint32_t info, id prov, const double *decode, long interp, int intent) {
    (void)decode; (void)interp; (void)intent;
    size_t n; const uint8_t *b = prov_bytes(prov, &n);
    if (!b || bpc != 8) return NULL;
    int ncomp = space ? ((struct cgspace *)space)->ncomp : 3;
    unsigned alpha = info & 31, order = info & 0x7000;
    struct gfx_image *g = gfx_image_new((int)w, (int)h);
    size_t step = bpp / 8;
    for (size_t y = 0; y < h; y++)
        for (size_t x = 0; x < w; x++) {
            size_t o = y * bpr + x * step;
            if (o + step > n) continue;
            const uint8_t *q = b + o; unsigned r, gg, bb, a = 255;
            if (ncomp == 1) { r = gg = bb = q[0]; if (step == 2) a = q[1]; if (alpha == 7) { a = q[0]; r = gg = bb = 0; } }
            else if (step == 3) { r = q[0]; gg = q[1]; bb = q[2]; }
            else {
                uint8_t c[4] = { q[0], q[1], q[2], q[3] };
                if (order == 0x2000) { uint8_t t = c[0]; c[0] = c[3]; c[3] = t; t = c[1]; c[1] = c[2]; c[2] = t; }   /* 32 little: reverse */
                int first = alpha == 2 || alpha == 4 || alpha == 6;
                if (first) { a = c[0]; r = c[1]; gg = c[2]; bb = c[3]; } else { r = c[0]; gg = c[1]; bb = c[2]; a = c[3]; }
                if (alpha == 0 || alpha == 5 || alpha == 6) a = 255;
                if (alpha == 3 || alpha == 4) { r = r * a / 255; gg = gg * a / 255; bb = bb * a / 255; }   /* non-premultiplied */
            }
            g->px[y * w + x] = r | gg << 8 | bb << 16 | a << 24;
        }
    id res = rt_cgimage_new(g); gfx_image_release(g);
    ((struct cgimage *)res)->gray = ncomp == 1;
    return res;
}
id iosrt_CGImageCreateCopy(id i) { return objc_retain(i); }

/* data providers */
static void prov_dealloc(id self, SEL s) {
    (void)s; struct cgprov *p = (struct cgprov *)self;
    if (p->release) p->release(p->info, p->rel_ptr, p->rel_size);
    objc_release(p->data); rt_dealloc_instance(self);
}
id iosrt_CGDataProviderCreateWithCFData(id data) { return provider_with_data(objc_retain(data)); }
id iosrt_CGDataProviderCreateWithData(void *info, const void *d, size_t n, void (*rel)(void *, const void *, size_t)) {
    id p = provider_with_data(rt_data_new(d, n)); struct cgprov *x = (struct cgprov *)p;
    x->info = info; x->release = rel; x->rel_ptr = d; x->rel_size = n; return p;
}
id iosrt_CGDataProviderCreateWithFilename(const char *path) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    void *b = malloc(n > 0 ? (size_t)n : 1); size_t got = fread(b, 1, n > 0 ? (size_t)n : 0, f); fclose(f);
    id p = provider_with_data(rt_data_new(b, got)); free(b); return p;
}
id iosrt_CGDataProviderCreateWithURL(id url) { id path = url ? RT_SEND0(id, url, "path") : NULL; return path ? iosrt_CGDataProviderCreateWithFilename(rt_str_utf8(path)) : NULL; }
id iosrt_CGDataProviderCopyData(id p) { return p && ((struct cgprov *)p)->data ? objc_retain(((struct cgprov *)p)->data) : NULL; }
id iosrt_CGDataProviderRetain(id p) { return objc_retain(p); }
void iosrt_CGDataProviderRelease(id p) { objc_release(p); }

/* image sources / destinations */
struct cgisrc { Class isa; id data; };
id iosrt_CGImageSourceCreateWithData(id data, id opts) { (void)opts; if (!data) return NULL; id s = rt_alloc_instance(RT_CLASS(CGImageSourceImpl), 0); ((struct cgisrc *)s)->data = objc_retain(data); return s; }
id iosrt_CGImageSourceCreateWithURL(id url, id opts) {
    id prov = iosrt_CGDataProviderCreateWithURL(url);
    if (!prov) return NULL;
    id s = iosrt_CGImageSourceCreateWithData(((struct cgprov *)prov)->data, opts); objc_release(prov); return s;
}
id iosrt_CGImageSourceCreateWithDataProvider(id prov, id opts) { return prov ? iosrt_CGImageSourceCreateWithData(((struct cgprov *)prov)->data, opts) : NULL; }
size_t iosrt_CGImageSourceGetCount(id s) { return s ? 1 : 0; }
static struct gfx_image *src_decode(id s) {
    if (!s) return NULL;
    size_t n; const void *b = rt_data_bytes(((struct cgisrc *)s)->data, &n);
    return gfx_image_decode(b, n);
}
id iosrt_CGImageSourceCreateImageAtIndex(id s, size_t i, id opts) {
    (void)opts; if (i) return NULL;
    struct gfx_image *g = src_decode(s); if (!g) return NULL;
    id r = rt_cgimage_new(g); gfx_image_release(g); return r;
}
id iosrt_CGImageSourceCreateThumbnailAtIndex(id s, size_t i, id opts) { return iosrt_CGImageSourceCreateImageAtIndex(s, i, opts); }
id iosrt_CGImageSourceCopyPropertiesAtIndex(id s, size_t i, id opts) {
    (void)i; (void)opts;
    struct gfx_image *g = src_decode(s); if (!g) return NULL;
    id keys[2] = { rt_str("PixelWidth"), rt_str("PixelHeight") };
    id vals[2] = { RT_SEND1(id, (id)rt_class_named("NSNumber"), "numberWithInt:", int, g->w), RT_SEND1(id, (id)rt_class_named("NSNumber"), "numberWithInt:", int, g->h) };
    gfx_image_release(g);
    return objc_retain(RT_SEND3(id, (id)rt_class_named("NSDictionary"), "dictionaryWithObjects:forKeys:count:", id *, vals, id *, keys, unsigned long, 2));
}
id iosrt_kCGImagePropertyPixelWidth, iosrt_kCGImagePropertyPixelHeight, iosrt_kCGImagePropertyOrientation, iosrt_kCGImageDestinationLossyCompressionQuality;
struct cgidst { Class isa; id url; id data; int jpeg; id image; double quality; };
id iosrt_CGImageDestinationCreateWithURL(id url, id type, size_t count, id opts) {
    (void)count; (void)opts;
    id d = rt_alloc_instance(RT_CLASS(CGImageDestinationImpl), 0); struct cgidst *x = (struct cgidst *)d;
    x->url = objc_retain(url); x->jpeg = type && strstr(rt_str_utf8(type), "jpeg") != NULL; x->quality = 0.9; return d;
}
id iosrt_CGImageDestinationCreateWithData(id data, id type, size_t count, id opts) { id d = iosrt_CGImageDestinationCreateWithURL(NULL, type, count, opts); ((struct cgidst *)d)->data = objc_retain(data); return d; }
static void dst_props(struct cgidst *x, id props) {
    id q = props ? rt_dict_get(props, rt_str("kCGImageDestinationLossyCompressionQuality")) : NULL;
    if (q) x->quality = RT_SEND0(double, q, "doubleValue");
}
void iosrt_CGImageDestinationAddImage(id d, id img, id props) { if (!d) return; struct cgidst *x = (struct cgidst *)d; objc_retain(img); objc_release(x->image); x->image = img; dst_props(x, props); }
void iosrt_CGImageDestinationSetProperties(id d, id props) { if (d) dst_props((struct cgidst *)d, props); }
rt_bool iosrt_CGImageDestinationFinalize(id d) {
    struct cgidst *x = (struct cgidst *)d; struct gfx_image *g = x ? rt_cgimage_gfx(x->image) : NULL;
    if (!g) return 0;
    size_t n; void *b = x->jpeg ? gfx_image_encode_jpeg(g, (int)(x->quality * 100), &n) : gfx_image_encode_png(g, &n);
    int ok = 1;
    if (x->data) RT_SEND2(void, x->data, "appendBytes:length:", const void *, b, unsigned long, n);
    else if (x->url) { id path = RT_SEND0(id, x->url, "path"); FILE *f = path ? fopen(rt_str_utf8(path), "wb") : NULL; ok = f && fwrite(b, 1, n, f) == n; if (f) fclose(f); }
    free(b); return ok;
}

/* gradients */
struct cggrad { Class isa; int n; float pos[GFX_MAX_STOPS]; float rgba[GFX_MAX_STOPS][4]; };
id iosrt_CGGradientCreateWithColorComponents(id space, const double *comps, const double *locs, size_t n) {
    int nc = space ? ((struct cgspace *)space)->ncomp : 3;
    id g = rt_alloc_instance(RT_CLASS(CGGradientImpl), 0); struct cggrad *x = (struct cggrad *)g;
    if (n > GFX_MAX_STOPS) n = GFX_MAX_STOPS;
    x->n = (int)n;
    for (size_t i = 0; i < n; i++) {
        x->pos[i] = locs ? (float)locs[i] : n > 1 ? (float)i / (float)(n - 1) : 0;
        const double *c = comps + i * (size_t)(nc + 1);
        if (nc == 1) { x->rgba[i][0] = x->rgba[i][1] = x->rgba[i][2] = (float)c[0]; x->rgba[i][3] = (float)c[1]; }
        else for (int k = 0; k < 4; k++) x->rgba[i][k] = (float)c[k];
    }
    return g;
}
id iosrt_CGGradientCreateWithColors(id space, id colors, const double *locs) {
    (void)space;
    size_t n = colors ? rt_array_count(colors) : 0;
    double *comps = calloc(n * 4 + 4, sizeof(double));
    for (size_t i = 0; i < n; i++) { float c[4]; rt_color_rgba(rt_array_at(colors, i), c); for (int k = 0; k < 4; k++) comps[i * 4 + k] = c[k]; }
    id g = iosrt_CGGradientCreateWithColorComponents(NULL, comps, locs, n);
    free(comps); return g;
}
id iosrt_CGGradientRetain(id g) { return objc_retain(g); }
void iosrt_CGGradientRelease(id g) { objc_release(g); }

/* fonts */
struct cgfont { Class isa; struct gfx_font *gf; id name; };
id iosrt_CGFontCreateWithDataProvider(id prov) {
    size_t n; const uint8_t *b = prov_bytes(prov, &n);
    if (!b || !n) return NULL;
    unsigned char *copy = malloc(n); memcpy(copy, b, n);
    struct gfx_font *gf = gfx_font_from_memory(copy, n);
    if (!gf) { free(copy); return NULL; }
    id f = rt_alloc_instance(RT_CLASS(CGFontImpl), 0); struct cgfont *x = (struct cgfont *)f;
    char name[256]; x->gf = gf;
    x->name = objc_retain(rt_str(gfx_font_name(gf, 6, name, sizeof name) ? name : "Unknown"));
    rt_font_register(rt_str_utf8(x->name), gf);
    return f;
}
id iosrt_CGFontCreateWithFontName(id name) { (void)name; return NULL; }
id iosrt_CGFontCopyPostScriptName(id f) { return f ? objc_retain(((struct cgfont *)f)->name) : NULL; }
id iosrt_CGFontCopyFullName(id f) { return iosrt_CGFontCopyPostScriptName(f); }
id iosrt_CGFontRetain(id f) { return objc_retain(f); }
void iosrt_CGFontRelease(id f) { objc_release(f); }
rt_bool iosrt_CTFontManagerRegisterGraphicsFont(id f, id *err) { if (err) *err = NULL; return f != NULL; }
rt_bool iosrt_CTFontManagerUnregisterGraphicsFont(id f, id *err) { (void)f; if (err) *err = NULL; return 1; }
rt_bool iosrt_CTFontManagerRegisterFontsForURL(id url, int scope, id *err) {
    (void)scope; if (err) *err = NULL;
    id prov = iosrt_CGDataProviderCreateWithURL(url); if (!prov) return 0;
    id f = iosrt_CGFontCreateWithDataProvider(prov); objc_release(prov);
    int ok = f != NULL; objc_release(f); return ok;
}

/* ============================================================== contexts */
enum { FMT_NATIVE, FMT_BGRA, FMT_CHAN, FMT_GRAY, FMT_ALPHA };
struct gstate {
    gfx_mat ctm; float fill[4], stroke[4]; id fill_pattern; float lw; int cap, join; float miter; float alpha; int blend;
    float dash[16]; int ndash; float dash_phase; int cx0, cy0, cx1, cy1; uint8_t *mask; int fill_ncomp, stroke_ncomp; gfx_mat text_mat;
};
struct cgctx {
    Class isa; struct gfx_surface surf; uint32_t *own; struct gstate gs; struct gstate *stack; int nstack, cstack;
    struct pathbuf path; double scale; rt_sb *trace; double tscale;
    /* bitmap bookkeeping */
    int is_bitmap, fmt, w, h; size_t bpr; uint8_t *ext; int ext_owned, ext_valid; int off[4]; uint32_t info;
};
static void gs_init(struct gstate *g) {
    memset(g, 0, sizeof *g);
    g->ctm = gfx_mat_identity(); g->fill[3] = 1; g->stroke[3] = 1; g->lw = 1; g->miter = 10; g->alpha = 1;
    g->fill_ncomp = g->stroke_ncomp = 3; g->text_mat = gfx_mat_identity();
}
static void ctx_dealloc(id self, SEL s) {
    (void)s; struct cgctx *c = (struct cgctx *)self;
    for (int i = 0; i < c->nstack; i++) { free(c->stack[i].mask); objc_release(c->stack[i].fill_pattern); }
    free(c->stack); objc_release(c->gs.fill_pattern);
    free(c->surf.mask); c->surf.mask = NULL;
    free(c->own); if (c->ext_owned) free(c->ext);
    pb_free(&c->path);
    rt_dealloc_instance(self);
}
static struct cgctx *ctx_alloc(void) { struct cgctx *c = (struct cgctx *)rt_alloc_instance(RT_CLASS(CGContextImpl), 0); gs_init(&c->gs); c->scale = 1; return c; }
struct cgctx *rt_cg_bitmap_new(int pw, int ph, double scale, int uikit_flip) {
    if (pw < 1) pw = 1;
    if (ph < 1) ph = 1;
    struct cgctx *c = ctx_alloc();
    c->own = calloc((size_t)pw * ph, 4);
    gfx_surface_init(&c->surf, c->own, pw, ph, pw);
    c->w = pw; c->h = ph; c->bpr = (size_t)pw * 4; c->is_bitmap = 1; c->scale = scale;
    if (uikit_flip) c->gs.ctm = (gfx_mat){ scale, 0, 0, scale, 0, 0 };
    else c->gs.ctm = (gfx_mat){ 1, 0, 0, -1, 0, ph };
    return c;
}
struct gfx_surface *rt_cg_surface(struct cgctx *c) { return c ? &c->surf : NULL; }
void rt_cg_set_trace(struct cgctx *c, rt_sb *t, double s) { c->trace = t; c->tscale = s; }
rt_sb *rt_cg_trace(struct cgctx *c, double *scale) { if (scale) *scale = c ? c->tscale : 1; return c ? c->trace : NULL; }
gfx_mat rt_cg_ctm(struct cgctx *c) { return c ? c->gs.ctm : gfx_mat_identity(); }
float rt_cg_alpha(struct cgctx *c) { return c ? c->gs.alpha : 1; }
int rt_cg_blend(struct cgctx *c) { return c ? c->gs.blend : 0; }
void rt_cg_trace_line(struct cgctx *c, const char *fmt, ...) {
    if (!c || !c->trace) return;
    char buf[1024]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    rt_sb_puts(c->trace, buf); rt_sb_putc(c->trace, '\n');
}
int rt_cg_device_rect_points(struct cgctx *c, CGRect r, CGRect *out) {
    gfx_mat m = c->gs.ctm;
    if (fabs(m.b) > 1e-9 || fabs(m.c) > 1e-9) return 0;
    double x0, y0, x1, y1;
    gfx_mat_apply(&m, r.origin.x, r.origin.y, &x0, &y0);
    gfx_mat_apply(&m, r.origin.x + r.size.width, r.origin.y + r.size.height, &x1, &y1);
    double s = c->tscale > 0 ? c->tscale : 1;
    out->origin.x = fmin(x0, x1) / s; out->origin.y = fmin(y0, y1) / s;
    out->size.width = fabs(x1 - x0) / s; out->size.height = fabs(y1 - y0) / s;
    return 1;
}
static double clean(double v) { double r = round(v * 1000) / 1000; return r == 0 ? 0 : r; }
static void trace_color(char *b, size_t n, const float *c, float alpha) {
    snprintf(b, n, "rgba(%d,%d,%d,%.2f)", (int)lroundf(c[0] * 255), (int)lroundf(c[1] * 255), (int)lroundf(c[2] * 255), c[3] * alpha);
}

/* bitmap layouts */
static void ext_to_int(struct cgctx *c) {
    for (int y = 0; y < c->h; y++) {
        const uint8_t *row = c->ext + (size_t)y * c->bpr; uint32_t *d = c->own + (size_t)y * c->w;
        for (int x = 0; x < c->w; x++) {
            unsigned r, g, b, a;
            if (c->fmt == FMT_GRAY) { r = g = b = row[x]; a = 255; }
            else if (c->fmt == FMT_ALPHA) { a = row[x]; r = g = b = 0; }
            else { const uint8_t *p = row + x * 4; r = p[c->off[0]]; g = p[c->off[1]]; b = p[c->off[2]]; a = c->off[3] >= 0 ? p[c->off[3]] : 255; }
            d[x] = r | g << 8 | b << 16 | a << 24;
        }
    }
}
static void int_to_ext(struct cgctx *c) {
    for (int y = 0; y < c->h; y++) {
        uint8_t *row = c->ext + (size_t)y * c->bpr; const uint32_t *s = c->own + (size_t)y * c->w;
        for (int x = 0; x < c->w; x++) {
            uint32_t v = s[x]; unsigned r = v & 255, g = (v >> 8) & 255, b = (v >> 16) & 255, a = v >> 24;
            if (c->fmt == FMT_GRAY) row[x] = (uint8_t)((r * 3 + g * 6 + b + 5) / 10 + (255 - a) * 0);
            else if (c->fmt == FMT_ALPHA) row[x] = (uint8_t)a;
            else { uint8_t *p = row + x * 4; p[c->off[0]] = (uint8_t)r; p[c->off[1]] = (uint8_t)g; p[c->off[2]] = (uint8_t)b; if (c->off[3] >= 0) p[c->off[3]] = (uint8_t)a; else p[6 - c->off[0] - c->off[1] - c->off[2]] = 255; }
        }
    }
}
static void pre_draw(struct cgctx *c) { if (c->ext_valid) { ext_to_int(c); c->ext_valid = 0; } }

struct cgctx *iosrt_CGBitmapContextCreate(void *data, size_t w, size_t h, size_t bpc, size_t bpr, id space, uint32_t info) {
    if (!w || !h) return NULL;
    int ncomp = space ? ((struct cgspace *)space)->ncomp : 0;
    unsigned alpha = info & 31, order = info & 0x7000;
    struct cgctx *c = ctx_alloc();
    c->is_bitmap = 1; c->w = (int)w; c->h = (int)h; c->info = info;
    c->gs.ctm = (gfx_mat){ 1, 0, 0, -1, 0, (double)h };
    int fmt = FMT_NATIVE;
    if (ncomp == 1 && bpc == 8) fmt = FMT_GRAY;
    else if (ncomp == 0 && alpha == 7) fmt = FMT_ALPHA;
    else if (bpc == 8) {
        /* byte offsets of R,G,B,A in memory */
        int first = alpha == 2 || alpha == 4 || alpha == 6, has_a = !(alpha == 0 || alpha == 5 || alpha == 6);
        int o[4];
        if (first) { o[3] = 0; o[0] = 1; o[1] = 2; o[2] = 3; } else { o[0] = 0; o[1] = 1; o[2] = 2; o[3] = 3; }
        if (order == 0x2000) for (int k = 0; k < 4; k++) o[k] = 3 - o[k];
        if (!has_a) { if (alpha == 0) { /* RGB without padding is 24bpp: unsupported -> skip-last */ } }
        memcpy(c->off, o, sizeof o);
        if (!has_a) c->off[3] = -1;
        if (o[0] == 0 && o[1] == 1 && o[2] == 2 && o[3] == 3) fmt = FMT_NATIVE;
        else if (o[0] == 2 && o[1] == 1 && o[2] == 0 && o[3] == 3) fmt = FMT_BGRA;
        else fmt = FMT_CHAN;
    }
    size_t bpp = fmt == FMT_GRAY || fmt == FMT_ALPHA ? 1 : 4;
    if (!bpr) bpr = w * bpp;
    c->bpr = bpr; c->fmt = fmt;
    if (!data) { data = calloc(h, bpr); c->ext_owned = 1; }
    c->ext = data;
    if ((fmt == FMT_NATIVE || fmt == FMT_BGRA) && bpr % 4 == 0) {
        gfx_surface_init(&c->surf, (uint32_t *)data, (int)w, (int)h, (int)(bpr / 4));
        c->surf.bgra = fmt == FMT_BGRA;
        if (c->ext_owned) { c->own = (uint32_t *)data; c->ext_owned = 0; }
        c->ext = NULL;
    } else {
        if (fmt == FMT_NATIVE || fmt == FMT_BGRA) fmt = c->fmt = FMT_CHAN;
        c->own = calloc(w * h, 4);
        gfx_surface_init(&c->surf, c->own, (int)w, (int)h, (int)w);
        c->ext_valid = 1;   /* caller-provided pixels are the initial content */
    }
    return c;
}
struct cgctx *iosrt_CGBitmapContextCreateWithData(void *data, size_t w, size_t h, size_t bpc, size_t bpr, id space, uint32_t info, void *rel, void *relinfo) {
    (void)rel; (void)relinfo; return iosrt_CGBitmapContextCreate(data, w, h, bpc, bpr, space, info);
}
void *iosrt_CGBitmapContextGetData(struct cgctx *c) {
    if (!c) return NULL;
    if (c->ext) { if (!c->ext_valid) { int_to_ext(c); c->ext_valid = 1; } return c->ext; }
    return c->surf.px;
}
size_t iosrt_CGBitmapContextGetWidth(struct cgctx *c) { return c ? (size_t)c->w : 0; }
size_t iosrt_CGBitmapContextGetHeight(struct cgctx *c) { return c ? (size_t)c->h : 0; }
size_t iosrt_CGBitmapContextGetBytesPerRow(struct cgctx *c) { return c ? c->bpr : 0; }
size_t iosrt_CGBitmapContextGetBitsPerComponent(struct cgctx *c) { (void)c; return 8; }
size_t iosrt_CGBitmapContextGetBitsPerPixel(struct cgctx *c) { return c && (c->fmt == FMT_GRAY || c->fmt == FMT_ALPHA) ? 8 : 32; }
uint32_t iosrt_CGBitmapContextGetBitmapInfo(struct cgctx *c) { return c ? c->info : 0; }
uint32_t iosrt_CGBitmapContextGetAlphaInfo(struct cgctx *c) { return c ? c->info & 31 : 0; }
id iosrt_CGBitmapContextGetColorSpace(struct cgctx *c) { (void)c; return iosrt_CGColorGetColorSpace(NULL); }
id rt_cgimage_from_ctx(struct cgctx *c) {
    if (!c) return NULL;
    pre_draw(c);
    struct gfx_image *g = gfx_image_new(c->surf.w, c->surf.h);
    for (int y = 0; y < c->surf.h; y++) {
        const uint32_t *s = c->surf.px + (size_t)y * c->surf.stride; uint32_t *d = g->px + (size_t)y * g->w;
        if (c->surf.bgra) for (int x = 0; x < c->surf.w; x++) { uint32_t v = s[x]; d[x] = (v & 0xff00ff00u) | (v & 0xff) << 16 | ((v >> 16) & 0xff); }
        else memcpy(d, s, (size_t)c->surf.w * 4);
    }
    if (c->fmt == FMT_GRAY) for (size_t i = 0; i < (size_t)g->w * g->h; i++) { uint32_t v = g->px[i]; unsigned l = ((v & 255) * 3 + ((v >> 8) & 255) * 6 + ((v >> 16) & 255)) / 10; g->px[i] = l | l << 8 | l << 16 | 0xff000000u; }
    if (c->fmt == FMT_NATIVE && c->off[3] < 0 && c->is_bitmap && (c->info & 31) >= 5) for (size_t i = 0; i < (size_t)g->w * g->h; i++) g->px[i] |= 0xff000000u;
    id r = rt_cgimage_new(g); gfx_image_release(g);
    ((struct cgimage *)r)->gray = c->fmt == FMT_GRAY;
    return r;
}
id iosrt_CGBitmapContextCreateImage(struct cgctx *c) { return rt_cgimage_from_ctx(c); }

struct cgctx *iosrt_CGContextRetain(struct cgctx *c) { return (struct cgctx *)objc_retain((id)c); }
void iosrt_CGContextRelease(struct cgctx *c) { objc_release((id)c); }
void iosrt_CGContextFlush(struct cgctx *c) { (void)c; }
void iosrt_CGContextSynchronize(struct cgctx *c) { (void)c; }

/* --- graphics state */
static void save_clip(struct cgctx *c, struct gstate *g) {
    g->cx0 = c->surf.cx0; g->cy0 = c->surf.cy0; g->cx1 = c->surf.cx1; g->cy1 = c->surf.cy1;
    g->mask = NULL;
    if (c->surf.mask) { size_t n = (size_t)c->surf.w * c->surf.h; g->mask = malloc(n); memcpy(g->mask, c->surf.mask, n); }
}
void iosrt_CGContextSaveGState(struct cgctx *c) {
    if (!c) return;
    if (c->nstack == c->cstack) { c->cstack = c->cstack ? c->cstack * 2 : 8; c->stack = realloc(c->stack, sizeof *c->stack * (size_t)c->cstack); }
    struct gstate *g = &c->stack[c->nstack++];
    *g = c->gs; objc_retain(g->fill_pattern);
    save_clip(c, g);
}
void iosrt_CGContextRestoreGState(struct cgctx *c) {
    if (!c || !c->nstack) return;
    struct gstate *g = &c->stack[--c->nstack];
    objc_release(c->gs.fill_pattern);
    c->gs = *g;
    free(c->surf.mask); c->surf.mask = g->mask;
    c->surf.cx0 = g->cx0; c->surf.cy0 = g->cy0; c->surf.cx1 = g->cx1; c->surf.cy1 = g->cy1;
    c->gs.mask = NULL;
}
void iosrt_CGContextConcatCTM(struct cgctx *c, gfx_mat m) { if (c) c->gs.ctm = gfx_mat_mul(m, c->gs.ctm); }
void iosrt_CGContextTranslateCTM(struct cgctx *c, double x, double y) { iosrt_CGContextConcatCTM(c, (gfx_mat){ 1, 0, 0, 1, x, y }); }
void iosrt_CGContextScaleCTM(struct cgctx *c, double x, double y) { iosrt_CGContextConcatCTM(c, (gfx_mat){ x, 0, 0, y, 0, 0 }); }
void iosrt_CGContextRotateCTM(struct cgctx *c, double a) { iosrt_CGContextConcatCTM(c, (gfx_mat){ cos(a), sin(a), -sin(a), cos(a), 0, 0 }); }
gfx_mat iosrt_CGContextGetCTM(struct cgctx *c) {
    if (!c) return gfx_mat_identity();
    /* user -> "default user space" of the context: strip the base transform */
    gfx_mat base = c->is_bitmap && c->fmt != -1 ? gfx_mat_identity() : gfx_mat_identity();
    (void)base; return c->gs.ctm;
}
gfx_mat iosrt_CGContextGetUserSpaceToDeviceSpaceTransform(struct cgctx *c) { return c ? c->gs.ctm : gfx_mat_identity(); }
CGPoint iosrt_CGContextConvertPointToDeviceSpace(struct cgctx *c, CGPoint p) { gfx_mat m = rt_cg_ctm(c); return tp(&m, p.x, p.y); }
CGPoint iosrt_CGContextConvertPointToUserSpace(struct cgctx *c, CGPoint p) { gfx_mat m = gfx_mat_invert(rt_cg_ctm(c)); return tp(&m, p.x, p.y); }
CGRect iosrt_CGContextConvertRectToDeviceSpace(struct cgctx *c, CGRect r);
void iosrt_CGContextSetAlpha(struct cgctx *c, double a) { if (c) c->gs.alpha = (float)fmax(0, fmin(1, a)); }
void iosrt_CGContextSetBlendMode(struct cgctx *c, int m) {
    if (!c) return;
    c->gs.blend = m == 16 ? GFX_BLEND_CLEAR : m == 17 ? GFX_BLEND_COPY : m == 1 ? GFX_BLEND_MULTIPLY : m == 2 ? GFX_BLEND_SCREEN :
                  m == 23 ? GFX_BLEND_DEST_OUT : m == 22 ? GFX_BLEND_DEST_IN : GFX_BLEND_NORMAL;
}
static void set_rgba(float *d, double r, double g, double b, double a) { d[0] = (float)r; d[1] = (float)g; d[2] = (float)b; d[3] = (float)a; }
void iosrt_CGContextSetRGBFillColor(struct cgctx *c, double r, double g, double b, double a) { if (c) { set_rgba(c->gs.fill, r, g, b, a); objc_release(c->gs.fill_pattern); c->gs.fill_pattern = NULL; } }
void iosrt_CGContextSetRGBStrokeColor(struct cgctx *c, double r, double g, double b, double a) { if (c) set_rgba(c->gs.stroke, r, g, b, a); }
void iosrt_CGContextSetGrayFillColor(struct cgctx *c, double g, double a) { iosrt_CGContextSetRGBFillColor(c, g, g, g, a); }
void iosrt_CGContextSetGrayStrokeColor(struct cgctx *c, double g, double a) { iosrt_CGContextSetRGBStrokeColor(c, g, g, g, a); }
void iosrt_CGContextSetFillColorWithColor(struct cgctx *c, id col) {
    if (!c) return;
    float v[4]; rt_color_rgba(col, v); memcpy(c->gs.fill, v, sizeof v);
    id pat = col ? ((struct uicolor *)col)->pattern : NULL;
    objc_retain(pat); objc_release(c->gs.fill_pattern); c->gs.fill_pattern = pat;
}
void iosrt_CGContextSetStrokeColorWithColor(struct cgctx *c, id col) { if (c) rt_color_rgba(col, c->gs.stroke); }
void iosrt_CGContextSetFillColorSpace(struct cgctx *c, id s) { if (c) c->gs.fill_ncomp = s ? ((struct cgspace *)s)->ncomp : 3; }
void iosrt_CGContextSetStrokeColorSpace(struct cgctx *c, id s) { if (c) c->gs.stroke_ncomp = s ? ((struct cgspace *)s)->ncomp : 3; }
void iosrt_CGContextSetFillColor(struct cgctx *c, const double *v) { if (!c || !v) return; if (c->gs.fill_ncomp == 1) set_rgba(c->gs.fill, v[0], v[0], v[0], v[1]); else set_rgba(c->gs.fill, v[0], v[1], v[2], v[3]); }
void iosrt_CGContextSetStrokeColor(struct cgctx *c, const double *v) { if (!c || !v) return; if (c->gs.stroke_ncomp == 1) set_rgba(c->gs.stroke, v[0], v[0], v[0], v[1]); else set_rgba(c->gs.stroke, v[0], v[1], v[2], v[3]); }
void iosrt_CGContextSetLineWidth(struct cgctx *c, double w) { if (c) c->gs.lw = (float)w; }
void iosrt_CGContextSetLineCap(struct cgctx *c, int cap) { if (c) c->gs.cap = cap; }
void iosrt_CGContextSetLineJoin(struct cgctx *c, int j) { if (c) c->gs.join = j; }
void iosrt_CGContextSetMiterLimit(struct cgctx *c, double m) { if (c) c->gs.miter = (float)m; }
void iosrt_CGContextSetLineDash(struct cgctx *c, double phase, const double *l, size_t n) {
    if (!c) return;
    if (n > 16) n = 16;
    c->gs.ndash = l ? (int)n : 0; c->gs.dash_phase = (float)phase;
    for (size_t i = 0; i < n && l; i++) c->gs.dash[i] = (float)l[i];
}
void iosrt_CGContextSetFlatness(struct cgctx *c, double f) { (void)c; (void)f; }
void iosrt_CGContextSetShouldAntialias(struct cgctx *c, long v) { (void)c; (void)v; }
void iosrt_CGContextSetAllowsAntialiasing(struct cgctx *c, long v) { (void)c; (void)v; }
void iosrt_CGContextSetShouldSmoothFonts(struct cgctx *c, long v) { (void)c; (void)v; }
void iosrt_CGContextSetShouldSubpixelQuantizeFonts(struct cgctx *c, long v) { (void)c; (void)v; }
void iosrt_CGContextSetShouldSubpixelPositionFonts(struct cgctx *c, long v) { (void)c; (void)v; }
void iosrt_CGContextSetInterpolationQuality(struct cgctx *c, int q) { (void)c; (void)q; }
int iosrt_CGContextGetInterpolationQuality(struct cgctx *c) { (void)c; return 0; }
void iosrt_CGContextSetRenderingIntent(struct cgctx *c, int i) { (void)c; (void)i; }
void iosrt_CGContextSetShadow(struct cgctx *c, CGSize o, double b) { (void)c; (void)o; (void)b; }
void iosrt_CGContextSetShadowWithColor(struct cgctx *c, CGSize o, double b, id col) { (void)c; (void)o; (void)b; (void)col; }
void iosrt_CGContextSetTextDrawingMode(struct cgctx *c, int m) { (void)c; (void)m; }
void iosrt_CGContextSetTextMatrix(struct cgctx *c, gfx_mat m) { if (c) c->gs.text_mat = m; }
gfx_mat iosrt_CGContextGetTextMatrix(struct cgctx *c) { return c ? c->gs.text_mat : gfx_mat_identity(); }
void iosrt_CGContextSetTextPosition(struct cgctx *c, double x, double y) { if (c) { c->gs.text_mat.tx = x; c->gs.text_mat.ty = y; } }
CGPoint iosrt_CGContextGetTextPosition(struct cgctx *c) { return c ? (CGPoint){ c->gs.text_mat.tx, c->gs.text_mat.ty } : (CGPoint){ 0, 0 }; }
void iosrt_CGContextSetCharacterSpacing(struct cgctx *c, double s) { (void)c; (void)s; }
void iosrt_CGContextSetFontSize(struct cgctx *c, double s) { (void)c; (void)s; }
void iosrt_CGContextSetFont(struct cgctx *c, id f) { (void)c; (void)f; }

/* --- current path (stored in device space) */
void iosrt_CGContextBeginPath(struct cgctx *c) { if (c) { pb_free(&c->path); } }
void iosrt_CGContextMoveToPoint(struct cgctx *c, double x, double y) { if (c) pb_move(&c->path, tp(&c->gs.ctm, x, y)); }
void iosrt_CGContextAddLineToPoint(struct cgctx *c, double x, double y) { if (c) pb_line(&c->path, tp(&c->gs.ctm, x, y)); }
void iosrt_CGContextAddCurveToPoint(struct cgctx *c, double a, double b, double d, double e, double x, double y) { if (c) pb_cubic(&c->path, tp(&c->gs.ctm, a, b), tp(&c->gs.ctm, d, e), tp(&c->gs.ctm, x, y)); }
void iosrt_CGContextAddQuadCurveToPoint(struct cgctx *c, double a, double b, double x, double y) { if (c) pb_quad(&c->path, tp(&c->gs.ctm, a, b), tp(&c->gs.ctm, x, y)); }
void iosrt_CGContextClosePath(struct cgctx *c) { if (c) pb_close(&c->path); }
void iosrt_CGContextAddRect(struct cgctx *c, CGRect r) { if (c) pb_rect(&c->path, &c->gs.ctm, r); }
void iosrt_CGContextAddRects(struct cgctx *c, const CGRect *r, size_t n) { for (size_t i = 0; i < n; i++) iosrt_CGContextAddRect(c, r[i]); }
void iosrt_CGContextAddLines(struct cgctx *c, const CGPoint *p, size_t n) { for (size_t i = 0; i < n; i++) { if (i) iosrt_CGContextAddLineToPoint(c, p[i].x, p[i].y); else iosrt_CGContextMoveToPoint(c, p[i].x, p[i].y); } }
void iosrt_CGContextAddEllipseInRect(struct cgctx *c, CGRect r) { if (c) pb_ellipse(&c->path, &c->gs.ctm, r); }
void iosrt_CGContextAddArc(struct cgctx *c, double x, double y, double r, double a0, double a1, int cw) { if (c) pb_arc(&c->path, &c->gs.ctm, x, y, r, a0, a1, cw); }
void iosrt_CGContextAddArcToPoint(struct cgctx *c, double x1, double y1, double x2, double y2, double r) {
    if (!c) return;
    if (!c->path.has_cur) { iosrt_CGContextMoveToPoint(c, x1, y1); return; }
    pb_arc_to(&c->path, &c->gs.ctm, user_cur(&c->path, &c->gs.ctm), x1, y1, x2, y2, r);
}
void iosrt_CGContextAddPath(struct cgctx *c, id p) { if (c && p) pb_append(&c->path, PB(p), &c->gs.ctm); }
rt_bool iosrt_CGContextIsPathEmpty(struct cgctx *c) { return !c || c->path.n == 0; }
CGPoint iosrt_CGContextGetPathCurrentPoint(struct cgctx *c) { return c ? user_cur(&c->path, &c->gs.ctm) : (CGPoint){ 0, 0 }; }
CGRect iosrt_CGContextGetPathBoundingBox(struct cgctx *c) {
    if (!c || !c->path.n) return (CGRect){ { INFINITY, INFINITY }, { 0, 0 } };
    gfx_mat inv = gfx_mat_invert(c->gs.ctm); struct pathbuf u = { 0 }; pb_append(&u, &c->path, &inv);
    CGRect r = pb_bounds(&u, 1); pb_free(&u); return r;
}
id iosrt_CGContextCopyPath(struct cgctx *c) {
    id p = iosrt_CGPathCreateMutable();
    if (c && c->path.n) { gfx_mat inv = gfx_mat_invert(c->gs.ctm); pb_append(PB(p), &c->path, &inv); }
    return p;
}
rt_bool iosrt_CGContextPathContainsPoint(struct cgctx *c, CGPoint p, int mode) {
    if (!c) return 0;
    struct gfx_poly poly; gfx_poly_init(&poly); pb_to_poly(&c->path, NULL, &poly);
    CGPoint d = tp(&c->gs.ctm, p.x, p.y);
    int r = poly_contains(&poly, d.x, d.y, mode == 1 || mode == 4);
    gfx_poly_free(&poly); return r;
}

/* --- painting */
static void make_paint(struct cgctx *c, struct gfx_paint *pt, const float *rgba) {
    memset(pt, 0, sizeof *pt);
    pt->kind = GFX_PAINT_SOLID; pt->blend = c->gs.blend; pt->alpha = c->gs.alpha;
    memcpy(pt->color, rgba, sizeof pt->color);
}
static void fill_pattern(struct cgctx *c, struct gfx_poly *poly, int rule);
static void trace_fill(struct cgctx *c, const struct pathbuf *pb, const float *col, int stroke) {
    if (!c->trace) return;
    char cs[64]; trace_color(cs, sizeof cs, col, c->gs.alpha);
    double s = c->tscale > 0 ? c->tscale : 1;
    CGRect b = pb_bounds(pb, 1);
    double x = clean(b.origin.x / s), y = clean(b.origin.y / s), w = clean(b.size.width / s), h = clean(b.size.height / s);
    if (pb->hint == H_LINE && stroke && pb->n == 2) {
        CGPoint a = pb->e[0].p[0], e = pb->e[1].p[0];
        rt_cg_trace_line(c, "line %g,%g %gx%g %s lw=%g", clean(a.x / s), clean(a.y / s), clean((e.x - a.x) / s), clean((e.y - a.y) / s), cs, clean(c->gs.lw * sqrt(fabs(c->gs.ctm.a * c->gs.ctm.d - c->gs.ctm.b * c->gs.ctm.c)) / s));
        return;
    }
    const char *name = pb->hint == H_RECT ? (stroke ? "rectStroke" : "rect") : pb->hint == H_OVAL ? (stroke ? "ovalStroke" : "oval") : (stroke ? "pathStroke" : "path");
    if (stroke) rt_cg_trace_line(c, "%s %g,%g %gx%g %s lw=%g", name, x, y, w, h, cs, clean(c->gs.lw * sqrt(fabs(c->gs.ctm.a * c->gs.ctm.d - c->gs.ctm.b * c->gs.ctm.c)) / s));
    else rt_cg_trace_line(c, "%s %g,%g %gx%g %s", name, x, y, w, h, cs);
}
static void fill_current(struct cgctx *c, int rule) {
    if (!c || !c->path.n) return;
    pre_draw(c);
    trace_fill(c, &c->path, c->gs.fill, 0);
    struct gfx_poly poly; gfx_poly_init(&poly); pb_to_poly(&c->path, NULL, &poly);
    if (c->gs.fill_pattern) fill_pattern(c, &poly, rule);
    else { struct gfx_paint pt; make_paint(c, &pt, c->gs.fill); gfx_fill_poly(&c->surf, &poly, rule, &pt); }
    gfx_poly_free(&poly);
}
static void stroke_current(struct cgctx *c) {
    if (!c || !c->path.n) return;
    pre_draw(c);
    trace_fill(c, &c->path, c->gs.stroke, 1);
    /* stroke in user space (line width/dash are user units), then map to device */
    gfx_mat inv = gfx_mat_invert(c->gs.ctm);
    struct gfx_poly up, out; gfx_poly_init(&up); gfx_poly_init(&out);
    pb_to_poly(&c->path, &inv, &up);
    float lw = c->gs.lw;
    double det = fabs(c->gs.ctm.a * c->gs.ctm.d - c->gs.ctm.b * c->gs.ctm.c);
    if (lw * sqrt(det) < 1.0 && lw >= 0) lw = (float)(1.0 / sqrt(det > 0 ? det : 1));   /* hairline: at least 1 device pixel */
    struct gfx_stroke_style st = { lw, c->gs.cap, c->gs.join, c->gs.miter, c->gs.dash, c->gs.ndash, c->gs.dash_phase };
    gfx_stroke_to_poly(&up, &st, &out);
    for (int i = 0; i < out.n; i++) { double x, y; gfx_mat_apply(&c->gs.ctm, out.xy[i * 2], out.xy[i * 2 + 1], &x, &y); out.xy[i * 2] = (float)x; out.xy[i * 2 + 1] = (float)y; }
    struct gfx_paint pt; make_paint(c, &pt, c->gs.stroke);
    gfx_fill_poly(&c->surf, &out, GFX_FILL_NONZERO, &pt);
    gfx_poly_free(&up); gfx_poly_free(&out);
}
void iosrt_CGContextFillPath(struct cgctx *c) { fill_current(c, GFX_FILL_NONZERO); iosrt_CGContextBeginPath(c); }
void iosrt_CGContextEOFillPath(struct cgctx *c) { fill_current(c, GFX_FILL_EVENODD); iosrt_CGContextBeginPath(c); }
void iosrt_CGContextStrokePath(struct cgctx *c) { stroke_current(c); iosrt_CGContextBeginPath(c); }
void iosrt_CGContextDrawPath(struct cgctx *c, int mode) {
    if (mode == 0 || mode == 3) fill_current(c, GFX_FILL_NONZERO);
    if (mode == 1 || mode == 4) fill_current(c, GFX_FILL_EVENODD);
    if (mode >= 2) stroke_current(c);
    iosrt_CGContextBeginPath(c);
}
void iosrt_CGContextReplacePathWithStrokedPath(struct cgctx *c) {
    if (!c) return;
    gfx_mat inv = gfx_mat_invert(c->gs.ctm);
    struct pathbuf u = { 0 }; pb_append(&u, &c->path, &inv);
    id p = path_copy_stroked(&u, NULL, c->gs.lw, c->gs.cap, c->gs.join, c->gs.miter);
    pb_free(&u); pb_free(&c->path);
    pb_append(&c->path, PB(p), &c->gs.ctm); objc_release(p);
}
static void with_temp_path(struct cgctx *c, void (*build)(struct cgctx *, CGRect), CGRect r, int stroke) {
    if (!c) return;
    struct pathbuf saved = c->path; memset(&c->path, 0, sizeof c->path);
    build(c, r);
    if (stroke) stroke_current(c); else fill_current(c, GFX_FILL_NONZERO);
    pb_free(&c->path); c->path = saved;
}
static void b_rect(struct cgctx *c, CGRect r) { iosrt_CGContextAddRect(c, r); }
static void b_oval(struct cgctx *c, CGRect r) { iosrt_CGContextAddEllipseInRect(c, r); }
void iosrt_CGContextFillRect(struct cgctx *c, CGRect r) { with_temp_path(c, b_rect, r, 0); }
void iosrt_CGContextFillRects(struct cgctx *c, const CGRect *r, size_t n) { for (size_t i = 0; i < n; i++) iosrt_CGContextFillRect(c, r[i]); }
void iosrt_CGContextStrokeRect(struct cgctx *c, CGRect r) { with_temp_path(c, b_rect, r, 1); }
void iosrt_CGContextStrokeRectWithWidth(struct cgctx *c, CGRect r, double w) { if (!c) return; float o = c->gs.lw; c->gs.lw = (float)w; iosrt_CGContextStrokeRect(c, r); c->gs.lw = o; }
void iosrt_CGContextFillEllipseInRect(struct cgctx *c, CGRect r) { with_temp_path(c, b_oval, r, 0); }
void iosrt_CGContextStrokeEllipseInRect(struct cgctx *c, CGRect r) { with_temp_path(c, b_oval, r, 1); }
void iosrt_CGContextStrokeLineSegments(struct cgctx *c, const CGPoint *p, size_t n) {
    if (!c) return;
    struct pathbuf saved = c->path; memset(&c->path, 0, sizeof c->path);
    for (size_t i = 0; i + 1 < n; i += 2) {
        if (c->trace) { pb_free(&c->path); iosrt_CGContextMoveToPoint(c, p[i].x, p[i].y); iosrt_CGContextAddLineToPoint(c, p[i + 1].x, p[i + 1].y); stroke_current(c); pb_free(&c->path); continue; }
        iosrt_CGContextMoveToPoint(c, p[i].x, p[i].y); iosrt_CGContextAddLineToPoint(c, p[i + 1].x, p[i + 1].y);
    }
    if (!c->trace) stroke_current(c);
    pb_free(&c->path); c->path = saved;
}
void iosrt_CGContextClearRect(struct cgctx *c, CGRect r) {
    if (!c) return;
    pre_draw(c);
    struct pathbuf pb = { 0 }; pb_rect(&pb, &c->gs.ctm, r);
    struct gfx_poly poly; gfx_poly_init(&poly); pb_to_poly(&pb, NULL, &poly);
    struct gfx_paint pt; memset(&pt, 0, sizeof pt); pt.blend = GFX_BLEND_CLEAR; pt.alpha = 1;
    gfx_fill_poly(&c->surf, &poly, GFX_FILL_NONZERO, &pt);
    gfx_poly_free(&poly); pb_free(&pb);
    if (c->trace) rt_cg_trace_line(c, "clear");
}

/* --- clipping */
static void clip_current(struct cgctx *c, int rule) {
    if (!c) return;
    pre_draw(c);
    struct gfx_poly poly; gfx_poly_init(&poly); pb_to_poly(&c->path, NULL, &poly);
    gfx_clip_poly(&c->surf, &poly, rule);
    gfx_poly_free(&poly); iosrt_CGContextBeginPath(c);
}
void iosrt_CGContextClip(struct cgctx *c) { clip_current(c, GFX_FILL_NONZERO); }
void iosrt_CGContextEOClip(struct cgctx *c) { clip_current(c, GFX_FILL_EVENODD); }
void iosrt_CGContextClipToRect(struct cgctx *c, CGRect r) {
    if (!c) return;
    struct pathbuf saved = c->path; memset(&c->path, 0, sizeof c->path);
    iosrt_CGContextAddRect(c, r); clip_current(c, GFX_FILL_NONZERO);
    c->path = saved;
}
void iosrt_CGContextClipToRects(struct cgctx *c, const CGRect *r, size_t n) {
    if (!c) return;
    struct pathbuf saved = c->path; memset(&c->path, 0, sizeof c->path);
    for (size_t i = 0; i < n; i++) iosrt_CGContextAddRect(c, r[i]);
    clip_current(c, GFX_FILL_NONZERO); c->path = saved;
}
static gfx_mat image_to_device_cg(struct cgctx *c, CGRect r, int iw, int ih) {
    /* image row 0 is the top; CG user space is y-up: top row lands at rect max y */
    gfx_mat m = { r.size.width / iw, 0, 0, -r.size.height / ih, r.origin.x, r.origin.y + r.size.height };
    return gfx_mat_mul(m, c->gs.ctm);
}
void iosrt_CGContextClipToMask(struct cgctx *c, CGRect r, id mask) {
    struct cgimage *m = (struct cgimage *)mask;
    if (!c || !m || !m->im) return;
    pre_draw(c);
    struct gfx_image *alpha = gfx_image_new(m->im->w, m->im->h);
    for (int y = 0; y < m->im->h; y++) for (int x = 0; x < m->im->w; x++) { unsigned a = (unsigned)(mask_alpha(m, x, y) * 255 + 0.5f); alpha->px[(size_t)y * m->im->w + x] = a << 24; }
    gfx_clip_image_mask(&c->surf, alpha, image_to_device_cg(c, r, m->im->w, m->im->h), 0);
    gfx_image_release(alpha);
}
CGRect iosrt_CGContextGetClipBoundingBox(struct cgctx *c) {
    if (!c) return (CGRect){ { 0, 0 }, { 0, 0 } };
    gfx_mat inv = gfx_mat_invert(c->gs.ctm); double x0, y0, x1, y1;
    gfx_mat_apply(&inv, c->surf.cx0, c->surf.cy0, &x0, &y0); gfx_mat_apply(&inv, c->surf.cx1, c->surf.cy1, &x1, &y1);
    return (CGRect){ { fmin(x0, x1), fmin(y0, y1) }, { fabs(x1 - x0), fabs(y1 - y0) } };
}

/* --- images and gradients */
static void draw_image_dev(struct cgctx *c, struct gfx_image *im, gfx_mat m, float alpha, int blend, const float *tint, CGRect user_r) {
    pre_draw(c);
    gfx_draw_image(&c->surf, im, m, alpha * c->gs.alpha, blend, tint);
    if (c->trace) {
        CGRect pr;
        if (rt_cg_device_rect_points(c, user_r, &pr)) rt_cg_trace_line(c, "image %g,%g %gx%g px=%dx%d", clean(pr.origin.x), clean(pr.origin.y), clean(pr.size.width), clean(pr.size.height), im->w, im->h);
        else rt_cg_trace_line(c, "image (transformed) px=%dx%d", im->w, im->h);
    }
}
void iosrt_CGContextDrawImage(struct cgctx *c, CGRect r, id img) {
    struct cgimage *ci = (struct cgimage *)img;
    if (!c || !ci || !rt_is_kind_of(img, RT_CLASS(CGImageImpl)) || !ci->im) return;
    if (ci->is_mask) {   /* stencil: paint fill color through the mask */
        struct gfx_image *a = gfx_image_new(ci->im->w, ci->im->h);
        for (int y = 0; y < a->h; y++) for (int x = 0; x < a->w; x++) a->px[(size_t)y * a->w + x] = (uint32_t)(mask_alpha(ci, x, y) * 255 + 0.5f) << 24;
        draw_image_dev(c, a, image_to_device_cg(c, r, a->w, a->h), 1, c->gs.blend, c->gs.fill, r);
        gfx_image_release(a); return;
    }
    draw_image_dev(c, ci->im, image_to_device_cg(c, r, ci->im->w, ci->im->h), 1, c->gs.blend, NULL, r);
}
void iosrt_CGContextDrawTiledImage(struct cgctx *c, CGRect r, id img) {
    if (!c || !img) return;
    CGRect clip = iosrt_CGContextGetClipBoundingBox(c);
    if (r.size.width <= 0 || r.size.height <= 0) return;
    double sx = floor((clip.origin.x - r.origin.x) / r.size.width), sy = floor((clip.origin.y - r.origin.y) / r.size.height);
    int n = 0;
    for (double y = r.origin.y + sy * r.size.height; y < clip.origin.y + clip.size.height && n < 4096; y += r.size.height)
        for (double x = r.origin.x + sx * r.size.width; x < clip.origin.x + clip.size.width && n < 4096; x += r.size.width, n++)
            iosrt_CGContextDrawImage(c, (CGRect){ { x, y }, r.size }, img);
}
void rt_cg_draw_image_uikit(struct cgctx *c, id img, CGRect r, float alpha, int blend, const float *tint) {
    struct gfx_image *g = rt_cgimage_gfx(img);
    if (!c || !g || r.size.width == 0 || r.size.height == 0) return;
    gfx_mat m = { r.size.width / g->w, 0, 0, r.size.height / g->h, r.origin.x, r.origin.y };
    draw_image_dev(c, g, gfx_mat_mul(m, c->gs.ctm), alpha, blend, tint, r);
}
void rt_cg_draw_image_9slice(struct cgctx *c, id img, CGRect r, UIEdgeInsets cp, double scale, float alpha, const float *tint, int tile) {
    struct gfx_image *g = rt_cgimage_gfx(img);
    if (!c || !g) return;
    (void)tile;
    double L = cp.left, R = cp.right, T = cp.top, B = cp.bottom;   /* pixels */
    if (L + R > g->w) { L = R = g->w / 2.0; }
    if (T + B > g->h) { T = B = g->h / 2.0; }
    double dl = L / scale, dr = R / scale, dt = T / scale, db = B / scale;
    if (dl + dr > r.size.width) { double k = r.size.width / (dl + dr); dl *= k; dr *= k; }
    if (dt + db > r.size.height) { double k = r.size.height / (dt + db); dt *= k; db *= k; }
    double sx[4] = { 0, L, g->w - R, g->w }, sy[4] = { 0, T, g->h - B, g->h };
    double dx[4] = { r.origin.x, r.origin.x + dl, r.origin.x + r.size.width - dr, r.origin.x + r.size.width };
    double dy[4] = { r.origin.y, r.origin.y + dt, r.origin.y + r.size.height - db, r.origin.y + r.size.height };
    pre_draw(c);
    rt_sb *saved = c->trace; c->trace = NULL;
    for (int j = 0; j < 3; j++)
        for (int i = 0; i < 3; i++) {
            double sw = sx[i + 1] - sx[i], sh = sy[j + 1] - sy[j], dw = dx[i + 1] - dx[i], dh = dy[j + 1] - dy[j];
            if (sw < 0.5 || sh < 0.5 || dw <= 0 || dh <= 0) continue;
            /* clip to the destination cell and map the source cell onto it */
            iosrt_CGContextSaveGState(c);
            iosrt_CGContextClipToRect(c, (CGRect){ { dx[i], dy[j] }, { dw, dh } });
            gfx_mat m = { dw / sw, 0, 0, dh / sh, dx[i] - sx[i] * dw / sw, dy[j] - sy[j] * dh / sh };
            gfx_draw_image(&c->surf, g, gfx_mat_mul(m, c->gs.ctm), alpha * c->gs.alpha, c->gs.blend, tint);
            iosrt_CGContextRestoreGState(c);
        }
    c->trace = saved;
    if (c->trace) { CGRect pr; if (rt_cg_device_rect_points(c, r, &pr)) rt_cg_trace_line(c, "image9 %g,%g %gx%g px=%dx%d", clean(pr.origin.x), clean(pr.origin.y), clean(pr.size.width), clean(pr.size.height), g->w, g->h); }
}
static void fill_pattern(struct cgctx *c, struct gfx_poly *poly, int rule) {
    id cg = rt_uiimage_cgimage(c->gs.fill_pattern);
    struct gfx_image *g = rt_cgimage_gfx(cg);
    if (!g) return;
    /* tile the pattern image (in user space, UIKit orientation) over the polygon's bounds */
    iosrt_CGContextSaveGState(c);
    gfx_clip_poly(&c->surf, poly, rule);
    CGSize sz = rt_uiimage_size(c->gs.fill_pattern);
    CGRect clip = iosrt_CGContextGetClipBoundingBox(c);
    rt_sb *saved = c->trace; c->trace = NULL;
    if (sz.width > 0 && sz.height > 0)
        for (double y = floor(clip.origin.y / sz.height) * sz.height; y < clip.origin.y + clip.size.height; y += sz.height)
            for (double x = floor(clip.origin.x / sz.width) * sz.width; x < clip.origin.x + clip.size.width; x += sz.width)
                rt_cg_draw_image_uikit(c, cg, (CGRect){ { x, y }, sz }, 1, c->gs.blend, NULL);
    c->trace = saved;
    iosrt_CGContextRestoreGState(c);
}
static void grad_paint(struct cgctx *c, id grad, struct gfx_paint *pt, unsigned opts) {
    struct cggrad *g = (struct cggrad *)grad;
    memset(pt, 0, sizeof *pt);
    pt->blend = c->gs.blend; pt->alpha = c->gs.alpha; pt->nstops = g->n;
    memcpy(pt->stop_pos, g->pos, sizeof g->pos); memcpy(pt->stop_rgba, g->rgba, sizeof g->rgba);
    pt->extend_start = opts & 1; pt->extend_end = (opts & 2) != 0;
}
static void fill_clip_region(struct cgctx *c, struct gfx_paint *pt) {
    gfx_fill_rect(&c->surf, (float)c->surf.cx0, (float)c->surf.cy0, (float)c->surf.cx1, (float)c->surf.cy1, pt);
}
void iosrt_CGContextDrawLinearGradient(struct cgctx *c, id grad, CGPoint a, CGPoint b, unsigned opts) {
    if (!c || !grad) return;
    pre_draw(c);
    struct gfx_paint pt; grad_paint(c, grad, &pt, opts); pt.kind = GFX_PAINT_LINEAR;
    CGPoint da = tp(&c->gs.ctm, a.x, a.y), db = tp(&c->gs.ctm, b.x, b.y);
    pt.gx0 = (float)da.x; pt.gy0 = (float)da.y; pt.gx1 = (float)db.x; pt.gy1 = (float)db.y;
    fill_clip_region(c, &pt);
    if (c->trace) rt_cg_trace_line(c, "linearGradient stops=%d", ((struct cggrad *)grad)->n);
}
void iosrt_CGContextDrawRadialGradient(struct cgctx *c, id grad, CGPoint c0, double r0, CGPoint c1, double r1, unsigned opts) {
    if (!c || !grad) return;
    pre_draw(c);
    struct gfx_paint pt; grad_paint(c, grad, &pt, opts); pt.kind = GFX_PAINT_RADIAL;
    double k = sqrt(fabs(c->gs.ctm.a * c->gs.ctm.d - c->gs.ctm.b * c->gs.ctm.c));
    CGPoint d0 = tp(&c->gs.ctm, c0.x, c0.y), d1 = tp(&c->gs.ctm, c1.x, c1.y);
    pt.gx0 = (float)d0.x; pt.gy0 = (float)d0.y; pt.gx1 = (float)d1.x; pt.gy1 = (float)d1.y; pt.gr0 = (float)(r0 * k); pt.gr1 = (float)(r1 * k);
    fill_clip_region(c, &pt);
    if (c->trace) rt_cg_trace_line(c, "radialGradient stops=%d", ((struct cggrad *)grad)->n);
}

/* ======================================================= UIGraphics stack */
static __thread struct cgctx *ctx_stack[64]; static __thread int nctx;
struct imgctx { struct cgctx *c; double scale; };
static __thread struct imgctx img_stack[32]; static __thread int nimg;
void rt_cg_push(struct cgctx *c) { if (nctx < 64) ctx_stack[nctx++] = c; }
void rt_cg_pop(void) { if (nctx) nctx--; }
struct cgctx *rt_cg_top(void) { return nctx ? ctx_stack[nctx - 1] : NULL; }
struct cgctx *iosrt_UIGraphicsGetCurrentContext(void) { return rt_cg_top(); }
void iosrt_UIGraphicsPushContext(struct cgctx *c) { rt_cg_push(c); }
void iosrt_UIGraphicsPopContext(void) { rt_cg_pop(); }
void iosrt_UIGraphicsBeginImageContextWithOptions(CGSize sz, long opaque, double scale) {
    (void)opaque;
    if (scale <= 0) scale = rt_scr_scale;
    struct cgctx *c = rt_cg_bitmap_new((int)ceil(sz.width * scale), (int)ceil(sz.height * scale), scale, 1);
    if (nimg < 32) { img_stack[nimg].c = c; img_stack[nimg].scale = scale; nimg++; }
    rt_cg_push(c);
}
void iosrt_UIGraphicsBeginImageContext(CGSize sz) { iosrt_UIGraphicsBeginImageContextWithOptions(sz, 0, 1); }
id iosrt_UIGraphicsGetImageFromCurrentImageContext(void) {
    if (!nimg) return NULL;
    struct imgctx *ic = &img_stack[nimg - 1];
    id cg = rt_cgimage_from_ctx(ic->c);
    id ui = rt_uiimage_from_cg(cg, ic->scale);
    objc_release(cg); return ui;
}
void iosrt_UIGraphicsEndImageContext(void) {
    if (!nimg) return;
    struct cgctx *c = img_stack[--nimg].c;
    /* pop it from the context stack too (wherever it is, normally on top) */
    for (int i = nctx - 1; i >= 0; i--) if (ctx_stack[i] == c) { memmove(&ctx_stack[i], &ctx_stack[i + 1], sizeof(ctx_stack[0]) * (size_t)(nctx - i - 1)); nctx--; break; }
    objc_release((id)c);
}
void iosrt_UIRectFill(CGRect r) { iosrt_CGContextFillRect(rt_cg_top(), r); }
void iosrt_UIRectFillUsingBlendMode(CGRect r, int mode) {
    struct cgctx *c = rt_cg_top(); if (!c) return;
    int b = c->gs.blend; iosrt_CGContextSetBlendMode(c, mode); iosrt_CGContextFillRect(c, r); c->gs.blend = b;
}
void iosrt_UIRectFrame(CGRect r) {
    struct cgctx *c = rt_cg_top(); if (!c) return;
    float s[4]; memcpy(s, c->gs.stroke, sizeof s); memcpy(c->gs.stroke, c->gs.fill, sizeof s);
    float lw = c->gs.lw; c->gs.lw = 1;
    iosrt_CGContextStrokeRect(c, (CGRect){ { r.origin.x + 0.5, r.origin.y + 0.5 }, { r.size.width - 1, r.size.height - 1 } });
    c->gs.lw = lw; memcpy(c->gs.stroke, s, sizeof s);
}
void iosrt_UIRectClip(CGRect r) { iosrt_CGContextClipToRect(rt_cg_top(), r); }

/* ======================================================== affine transforms */
gfx_mat iosrt_CGAffineTransformMake(double a, double b, double c, double d, double tx, double ty) { return (gfx_mat){ a, b, c, d, tx, ty }; }
gfx_mat iosrt_CGAffineTransformMakeTranslation(double x, double y) { return (gfx_mat){ 1, 0, 0, 1, x, y }; }
gfx_mat iosrt_CGAffineTransformMakeScale(double x, double y) { return (gfx_mat){ x, 0, 0, y, 0, 0 }; }
gfx_mat iosrt_CGAffineTransformMakeRotation(double a) { return (gfx_mat){ cos(a), sin(a), -sin(a), cos(a), 0, 0 }; }
gfx_mat iosrt_CGAffineTransformConcat(gfx_mat a, gfx_mat b) { return gfx_mat_mul(a, b); }
gfx_mat iosrt_CGAffineTransformTranslate(gfx_mat t, double x, double y) { return gfx_mat_mul((gfx_mat){ 1, 0, 0, 1, x, y }, t); }
gfx_mat iosrt_CGAffineTransformScale(gfx_mat t, double x, double y) { return gfx_mat_mul((gfx_mat){ x, 0, 0, y, 0, 0 }, t); }
gfx_mat iosrt_CGAffineTransformRotate(gfx_mat t, double a) { return gfx_mat_mul(iosrt_CGAffineTransformMakeRotation(a), t); }
gfx_mat iosrt_CGAffineTransformInvert(gfx_mat t) { return gfx_mat_invert(t); }
rt_bool iosrt_CGAffineTransformIsIdentity(gfx_mat t) { return t.a == 1 && t.b == 0 && t.c == 0 && t.d == 1 && t.tx == 0 && t.ty == 0; }
rt_bool iosrt_CGAffineTransformEqualToTransform(gfx_mat a, gfx_mat b) { return !memcmp(&a, &b, sizeof a); }
CGPoint iosrt_CGPointApplyAffineTransform(CGPoint p, gfx_mat t) { return tp(&t, p.x, p.y); }
CGSize iosrt_CGSizeApplyAffineTransform(CGSize s, gfx_mat t) { return (CGSize){ t.a * s.width + t.c * s.height, t.b * s.width + t.d * s.height }; }
CGRect iosrt_CGRectApplyAffineTransform(CGRect r, gfx_mat t) {
    double xs[4] = { r.origin.x, r.origin.x + r.size.width, r.origin.x, r.origin.x + r.size.width };
    double ys[4] = { r.origin.y, r.origin.y, r.origin.y + r.size.height, r.origin.y + r.size.height };
    double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
    for (int i = 0; i < 4; i++) { CGPoint p = tp(&t, xs[i], ys[i]); x0 = fmin(x0, p.x); y0 = fmin(y0, p.y); x1 = fmax(x1, p.x); y1 = fmax(y1, p.y); }
    return (CGRect){ { x0, y0 }, { x1 - x0, y1 - y0 } };
}
CGRect iosrt_CGContextConvertRectToDeviceSpace(struct cgctx *c, CGRect r) { return iosrt_CGRectApplyAffineTransform(r, rt_cg_ctm(c)); }
CGRect iosrt_CGRectStandardize(CGRect r) {
    if (r.size.width < 0) { r.origin.x += r.size.width; r.size.width = -r.size.width; }
    if (r.size.height < 0) { r.origin.y += r.size.height; r.size.height = -r.size.height; }
    return r;
}
void iosrt_CGRectDivide(CGRect r, CGRect *slice, CGRect *rem, double amount, int edge) {
    r = iosrt_CGRectStandardize(r);
    CGRect s = r, m = r;
    switch (edge) {
    case 0: amount = fmin(amount, r.size.width); s.size.width = amount; m.origin.x += amount; m.size.width -= amount; break;            /* MinX */
    case 1: amount = fmin(amount, r.size.height); s.size.height = amount; m.origin.y += amount; m.size.height -= amount; break;         /* MinY */
    case 2: amount = fmin(amount, r.size.width); s.origin.x = r.origin.x + r.size.width - amount; s.size.width = amount; m.size.width -= amount; break;   /* MaxX */
    default: amount = fmin(amount, r.size.height); s.origin.y = r.origin.y + r.size.height - amount; s.size.height = amount; m.size.height -= amount; break;
    }
    if (slice) *slice = s;
    if (rem) *rem = m;
}
CGRect iosrt_CGRectMake(double x, double y, double w, double h) { return (CGRect){ { x, y }, { w, h } }; }
CGPoint iosrt_CGPointMake(double x, double y) { return (CGPoint){ x, y }; }
CGSize iosrt_CGSizeMake(double w, double h) { return (CGSize){ w, h }; }
rt_bool iosrt_CGRectIsInfinite(CGRect r) { return isinf(r.size.width) || r.size.width > 1e300; }

/* ======================================================= rect geometry */
/* geometry functions that are real exports in CoreGraphics */
CGRect iosrt_CGRectZero, iosrt_CGRectNull = { { INFINITY, INFINITY }, { 0, 0 } }, iosrt_CGRectInfinite = { { -8.98846567431158e307, -8.98846567431158e307 }, { 1.7976931348623157e308, 1.7976931348623157e308 } };
CGPoint iosrt_CGPointZero; CGSize iosrt_CGSizeZero;
gfx_mat iosrt_CGAffineTransformIdentity = { 1, 0, 0, 1, 0, 0 };
static CGRect std_rect(CGRect r) { if (r.size.width < 0) { r.origin.x += r.size.width; r.size.width = -r.size.width; } if (r.size.height < 0) { r.origin.y += r.size.height; r.size.height = -r.size.height; } return r; }
double iosrt_CGRectGetMinX(CGRect r) { return std_rect(r).origin.x; }
double iosrt_CGRectGetMinY(CGRect r) { return std_rect(r).origin.y; }
double iosrt_CGRectGetMaxX(CGRect r) { r = std_rect(r); return r.origin.x + r.size.width; }
double iosrt_CGRectGetMaxY(CGRect r) { r = std_rect(r); return r.origin.y + r.size.height; }
double iosrt_CGRectGetMidX(CGRect r) { r = std_rect(r); return r.origin.x + r.size.width / 2; }
double iosrt_CGRectGetMidY(CGRect r) { r = std_rect(r); return r.origin.y + r.size.height / 2; }
double iosrt_CGRectGetWidth(CGRect r) { return fabs(r.size.width); }
double iosrt_CGRectGetHeight(CGRect r) { return fabs(r.size.height); }
rt_bool iosrt_CGRectIsEmpty(CGRect r) { return r.size.width == 0 || r.size.height == 0 || isinf(r.origin.x); }
rt_bool iosrt_CGRectIsNull(CGRect r) { return isinf(r.origin.x); }
rt_bool iosrt_CGRectEqualToRect(CGRect a, CGRect b) { return !memcmp(&a, &b, sizeof a); }
rt_bool iosrt_CGPointEqualToPoint(CGPoint a, CGPoint b) { return a.x == b.x && a.y == b.y; }
rt_bool iosrt_CGSizeEqualToSize(CGSize a, CGSize b) { return a.width == b.width && a.height == b.height; }
rt_bool iosrt_CGRectContainsPoint(CGRect r, CGPoint p) { r = std_rect(r); return p.x >= r.origin.x && p.y >= r.origin.y && p.x < r.origin.x + r.size.width && p.y < r.origin.y + r.size.height; }
rt_bool iosrt_CGRectContainsRect(CGRect a, CGRect b) { a = std_rect(a); b = std_rect(b); return b.origin.x >= a.origin.x && b.origin.y >= a.origin.y && b.origin.x + b.size.width <= a.origin.x + a.size.width && b.origin.y + b.size.height <= a.origin.y + a.size.height; }
rt_bool iosrt_CGRectIntersectsRect(CGRect a, CGRect b) { a = std_rect(a); b = std_rect(b); return a.origin.x < b.origin.x + b.size.width && b.origin.x < a.origin.x + a.size.width && a.origin.y < b.origin.y + b.size.height && b.origin.y < a.origin.y + a.size.height; }
CGRect iosrt_CGRectInset(CGRect r, double dx, double dy) { r = std_rect(r); r.origin.x += dx; r.origin.y += dy; r.size.width -= 2 * dx; r.size.height -= 2 * dy; if (r.size.width < 0 || r.size.height < 0) return iosrt_CGRectNull; return r; }
CGRect iosrt_CGRectOffset(CGRect r, double dx, double dy) { r.origin.x += dx; r.origin.y += dy; return r; }
CGRect iosrt_CGRectIntegral(CGRect r) { r = std_rect(r); double x0 = floor(r.origin.x), y0 = floor(r.origin.y); r.size.width = ceil(r.origin.x + r.size.width) - x0; r.size.height = ceil(r.origin.y + r.size.height) - y0; r.origin.x = x0; r.origin.y = y0; return r; }
CGRect iosrt_CGRectUnion(CGRect a, CGRect b) { a = std_rect(a); b = std_rect(b); double x0 = fmin(a.origin.x, b.origin.x), y0 = fmin(a.origin.y, b.origin.y); double x1 = fmax(a.origin.x + a.size.width, b.origin.x + b.size.width), y1 = fmax(a.origin.y + a.size.height, b.origin.y + b.size.height); return (CGRect){ { x0, y0 }, { x1 - x0, y1 - y0 } }; }
CGRect iosrt_CGRectIntersection(CGRect a, CGRect b) { if (!iosrt_CGRectIntersectsRect(a, b)) return iosrt_CGRectNull; a = std_rect(a); b = std_rect(b); double x0 = fmax(a.origin.x, b.origin.x), y0 = fmax(a.origin.y, b.origin.y); double x1 = fmin(a.origin.x + a.size.width, b.origin.x + b.size.width), y1 = fmin(a.origin.y + a.size.height, b.origin.y + b.size.height); return (CGRect){ { x0, y0 }, { x1 - x0, y1 - y0 } }; }
id iosrt_NSStringFromCGRect(CGRect r) { char b[160]; snprintf(b, sizeof b, "{{%g, %g}, {%g, %g}}", r.origin.x, r.origin.y, r.size.width, r.size.height); return rt_str(b); }
id iosrt_NSStringFromCGPoint(CGPoint p) { char b[96]; snprintf(b, sizeof b, "{%g, %g}", p.x, p.y); return rt_str(b); }
id iosrt_NSStringFromCGSize(CGSize p) { char b[96]; snprintf(b, sizeof b, "{%g, %g}", p.width, p.height); return rt_str(b); }

static double num_at(const char **p) { while (**p && !((**p >= '0' && **p <= '9') || **p == '-' || **p == '.')) (*p)++; char *e; double v = strtod(*p, &e); *p = e; return v; }
CGRect iosrt_CGRectFromString(id s) { const char *p = s ? rt_str_utf8(s) : ""; CGRect r; r.origin.x = num_at(&p); r.origin.y = num_at(&p); r.size.width = num_at(&p); r.size.height = num_at(&p); return r; }
CGPoint iosrt_CGPointFromString(id s) { const char *p = s ? rt_str_utf8(s) : ""; CGPoint r; r.x = num_at(&p); r.y = num_at(&p); return r; }
CGSize iosrt_CGSizeFromString(id s) { const char *p = s ? rt_str_utf8(s) : ""; CGSize r; r.width = num_at(&p); r.height = num_at(&p); return r; }
id iosrt_NSStringFromCGAffineTransform(gfx_mat t) { char b[256]; snprintf(b, sizeof b, "[%g, %g, %g, %g, %g, %g]", t.a, t.b, t.c, t.d, t.tx, t.ty); return rt_str(b); }
id iosrt_NSStringFromUIEdgeInsets(UIEdgeInsets e) { char b[160]; snprintf(b, sizeof b, "{%g, %g, %g, %g}", e.top, e.left, e.bottom, e.right); return rt_str(b); }
UIEdgeInsets iosrt_UIEdgeInsetsFromString(id s) { const char *p = s ? rt_str_utf8(s) : ""; UIEdgeInsets e; e.top = num_at(&p); e.left = num_at(&p); e.bottom = num_at(&p); e.right = num_at(&p); return e; }

/* =========================================================== UIBezierPath */
struct uibez { Class isa; id path; double lw; long cap, join; double miter; double dash[16]; long ndash; double phase; long eo; double flat; };
static id bz_new(Class c) {
    id b = rt_alloc_instance(c ? c : RT_CLASS(UIBezierPath), 0); struct uibez *x = (struct uibez *)b;
    x->path = iosrt_CGPathCreateMutable(); x->lw = 1; x->miter = 10; x->flat = 0.6; return b;
}
static id bz_init(id self, SEL s) { (void)s; struct uibez *x = (struct uibez *)self; if (!x->path) { x->path = iosrt_CGPathCreateMutable(); x->lw = 1; x->miter = 10; x->flat = 0.6; } return self; }
static void bz_dealloc(id self, SEL s) { (void)s; objc_release(((struct uibez *)self)->path); rt_dealloc_instance(self); }
static struct pathbuf *BPB(id self) { return PB(((struct uibez *)self)->path); }
static id bz_bezierPath(Class c, SEL s) { (void)s; return objc_autorelease(bz_new(c)); }
static id bz_rect(Class c, SEL s, CGRect r) { (void)s; id b = bz_new(c); iosrt_CGPathAddRect(((struct uibez *)b)->path, NULL, r); return objc_autorelease(b); }
static id bz_oval(Class c, SEL s, CGRect r) { (void)s; id b = bz_new(c); iosrt_CGPathAddEllipseInRect(((struct uibez *)b)->path, NULL, r); return objc_autorelease(b); }
static id bz_rounded(Class c, SEL s, CGRect r, double rad) { (void)s; id b = bz_new(c); iosrt_CGPathAddRoundedRect(((struct uibez *)b)->path, NULL, r, rad, rad); return objc_autorelease(b); }
static id bz_roundedCorners(Class c, SEL s, CGRect r, unsigned long corners, CGSize radii) {
    (void)s; id b = bz_new(c); struct pathbuf *pb = PB(((struct uibez *)b)->path);
    double rx = fmin(radii.width, r.size.width / 2), ry = fmin(radii.height, r.size.height / 2);
    double x0 = r.origin.x, y0 = r.origin.y, x1 = x0 + r.size.width, y1 = y0 + r.size.height, kx = rx * KAPPA, ky = ry * KAPPA;
    int tl = corners & 1, tr = corners & 2, bl = corners & 4, br = corners & 8;
    pb_move(pb, (CGPoint){ x0 + (tl ? rx : 0), y0 });
    pb_line(pb, (CGPoint){ x1 - (tr ? rx : 0), y0 });
    if (tr) pb_cubic(pb, (CGPoint){ x1 - rx + kx, y0 }, (CGPoint){ x1, y0 + ry - ky }, (CGPoint){ x1, y0 + ry });
    pb_line(pb, (CGPoint){ x1, y1 - (br ? ry : 0) });
    if (br) pb_cubic(pb, (CGPoint){ x1, y1 - ry + ky }, (CGPoint){ x1 - rx + kx, y1 }, (CGPoint){ x1 - rx, y1 });
    pb_line(pb, (CGPoint){ x0 + (bl ? rx : 0), y1 });
    if (bl) pb_cubic(pb, (CGPoint){ x0 + rx - kx, y1 }, (CGPoint){ x0, y1 - ry + ky }, (CGPoint){ x0, y1 - ry });
    pb_line(pb, (CGPoint){ x0, y0 + (tl ? ry : 0) });
    if (tl) pb_cubic(pb, (CGPoint){ x0, y0 + ry - ky }, (CGPoint){ x0 + rx - kx, y0 }, (CGPoint){ x0 + rx, y0 });
    pb_close(pb); pb->hint = H_GENERIC;
    return objc_autorelease(b);
}
/* UIKit's clockwise is in y-down space, i.e. increasing angle = CG counter-clockwise flag */
static void bz_addArc(id self, SEL s, CGPoint c, double r, double a0, double a1, long cw) { (void)s; pb_arc(BPB(self), NULL, c.x, c.y, r, a0, a1, !(cw & 0xff)); }
static id bz_arc(Class c, SEL s, CGPoint ctr, double r, double a0, double a1, long cw) { id b = bz_new(c); bz_addArc(b, s, ctr, r, a0, a1, cw); return objc_autorelease(b); }
static id bz_withCGPath(Class c, SEL s, id p) { (void)s; id b = bz_new(c); if (p) pb_append(BPB(b), PB(p), NULL); return objc_autorelease(b); }
static void bz_move(id self, SEL s, CGPoint p) { (void)s; pb_move(BPB(self), p); }
static void bz_line(id self, SEL s, CGPoint p) { (void)s; pb_line(BPB(self), p); }
static void bz_curve(id self, SEL s, CGPoint p, CGPoint c1, CGPoint c2) { (void)s; pb_cubic(BPB(self), c1, c2, p); }
static void bz_quad(id self, SEL s, CGPoint p, CGPoint c1) { (void)s; pb_quad(BPB(self), c1, p); }
static void bz_close(id self, SEL s) { (void)s; pb_close(BPB(self)); }
static void bz_removeAll(id self, SEL s) { (void)s; pb_free(BPB(self)); }
static void bz_append(id self, SEL s, id other) { (void)s; if (other) pb_append(BPB(self), BPB(other), NULL); }
static id bz_CGPath(id self, SEL s) { (void)s; return ((struct uibez *)self)->path; }
static void bz_setCGPath(id self, SEL s, id p) { (void)s; struct uibez *x = (struct uibez *)self; id n = iosrt_CGPathCreateCopy(p); objc_release(x->path); x->path = n; }
static double bz_lw(id self, SEL s) { (void)s; return ((struct uibez *)self)->lw; }
static void bz_setLw(id self, SEL s, double v) { (void)s; ((struct uibez *)self)->lw = v; }
static long bz_cap(id self, SEL s) { (void)s; return ((struct uibez *)self)->cap; }
static void bz_setCap(id self, SEL s, long v) { (void)s; ((struct uibez *)self)->cap = v; }
static long bz_join(id self, SEL s) { (void)s; return ((struct uibez *)self)->join; }
static void bz_setJoin(id self, SEL s, long v) { (void)s; ((struct uibez *)self)->join = v; }
static double bz_miter(id self, SEL s) { (void)s; return ((struct uibez *)self)->miter; }
static void bz_setMiter(id self, SEL s, double v) { (void)s; ((struct uibez *)self)->miter = v; }
static double bz_flat(id self, SEL s) { (void)s; return ((struct uibez *)self)->flat; }
static void bz_setFlat(id self, SEL s, double v) { (void)s; ((struct uibez *)self)->flat = v; }
static rt_bool bz_eo(id self, SEL s) { (void)s; return ((struct uibez *)self)->eo != 0; }
static void bz_setEo(id self, SEL s, long v) { (void)s; ((struct uibez *)self)->eo = v & 0xff; }
static void bz_setDash(id self, SEL s, const double *d, long n, double phase) {
    (void)s; struct uibez *x = (struct uibez *)self;
    if (n > 16) n = 16;
    x->ndash = d ? n : 0; x->phase = phase;
    for (long i = 0; i < x->ndash; i++) x->dash[i] = d[i];
}
static void bz_apply_style(id self, struct cgctx *c) {
    struct uibez *x = (struct uibez *)self;
    iosrt_CGContextSetLineWidth(c, x->lw); iosrt_CGContextSetLineCap(c, (int)x->cap); iosrt_CGContextSetLineJoin(c, (int)x->join);
    iosrt_CGContextSetMiterLimit(c, x->miter); iosrt_CGContextSetLineDash(c, x->phase, x->ndash ? x->dash : NULL, (size_t)x->ndash);
}
static void bz_fill(id self, SEL s) {
    (void)s; struct cgctx *c = rt_cg_top(); if (!c) return;
    iosrt_CGContextSaveGState(c); iosrt_CGContextBeginPath(c);
    iosrt_CGContextAddPath(c, ((struct uibez *)self)->path);
    if (((struct uibez *)self)->eo) iosrt_CGContextEOFillPath(c); else iosrt_CGContextFillPath(c);
    iosrt_CGContextRestoreGState(c);
}
static void bz_stroke(id self, SEL s) {
    (void)s; struct cgctx *c = rt_cg_top(); if (!c) return;
    iosrt_CGContextSaveGState(c); bz_apply_style(self, c); iosrt_CGContextBeginPath(c);
    iosrt_CGContextAddPath(c, ((struct uibez *)self)->path); iosrt_CGContextStrokePath(c);
    iosrt_CGContextRestoreGState(c);
}
static void bz_fillBlend(id self, SEL s, int mode, double a) {
    struct cgctx *c = rt_cg_top(); if (!c) return;
    iosrt_CGContextSaveGState(c); iosrt_CGContextSetBlendMode(c, mode); iosrt_CGContextSetAlpha(c, a * c->gs.alpha); bz_fill(self, s); iosrt_CGContextRestoreGState(c);
}
static void bz_strokeBlend(id self, SEL s, int mode, double a) {
    struct cgctx *c = rt_cg_top(); if (!c) return;
    iosrt_CGContextSaveGState(c); iosrt_CGContextSetBlendMode(c, mode); iosrt_CGContextSetAlpha(c, a * c->gs.alpha); bz_stroke(self, s); iosrt_CGContextRestoreGState(c);
}
static void bz_addClip(id self, SEL s) {
    (void)s; struct cgctx *c = rt_cg_top(); if (!c) return;
    iosrt_CGContextBeginPath(c); iosrt_CGContextAddPath(c, ((struct uibez *)self)->path);
    if (((struct uibez *)self)->eo) iosrt_CGContextEOClip(c); else iosrt_CGContextClip(c);
}
static rt_bool bz_contains(id self, SEL s, CGPoint p) { (void)s; return iosrt_CGPathContainsPoint(((struct uibez *)self)->path, NULL, p, ((struct uibez *)self)->eo); }
static CGRect bz_bounds(id self, SEL s) { (void)s; CGRect r = iosrt_CGPathGetBoundingBox(((struct uibez *)self)->path); if (isinf(r.origin.x)) r = (CGRect){ { 0, 0 }, { 0, 0 } }; return r; }
static CGPoint bz_current(id self, SEL s) { (void)s; return BPB(self)->cur; }
static rt_bool bz_isEmpty(id self, SEL s) { (void)s; return BPB(self)->n == 0; }
static void bz_applyTransform(id self, SEL s, gfx_mat t) {
    (void)s; struct uibez *x = (struct uibez *)self;
    id n = iosrt_CGPathCreateCopyByTransformingPath(x->path, &t); objc_release(x->path); x->path = n;
}
static id bz_copy(id self, SEL s, void *zone) {
    (void)s; (void)zone; struct uibez *x = (struct uibez *)self;
    id b = rt_alloc_instance(self->isa, 0); struct uibez *y = (struct uibez *)b;
    *y = *x; y->isa = self->isa; y->path = iosrt_CGPathCreateCopy(x->path);
    return b;
}
static id bz_reversing(id self, SEL s) { (void)s; return objc_autorelease(bz_copy(self, NULL, NULL)); }

void rt_cg_init(void) {
    Class o = RT_CLASS(NSObject);
    NEWCLASS(CGContextImpl, o, sizeof(struct cgctx) - sizeof(void *)); IM(RT_CLASS(CGContextImpl), "dealloc", ctx_dealloc);
    NEWCLASS(CGPathImpl, o, sizeof(struct cgpath) - sizeof(void *)); IM(RT_CLASS(CGPathImpl), "dealloc", path_dealloc);
    NEWCLASS(CGImageImpl, o, sizeof(struct cgimage) - sizeof(void *)); IM(RT_CLASS(CGImageImpl), "dealloc", img_dealloc);
    NEWCLASS(CGColorSpaceImpl, o, sizeof(struct cgspace) - sizeof(void *));
    NEWCLASS(CGGradientImpl, o, sizeof(struct cggrad) - sizeof(void *));
    NEWCLASS(CGDataProviderImpl, o, sizeof(struct cgprov) - sizeof(void *)); IM(RT_CLASS(CGDataProviderImpl), "dealloc", prov_dealloc);
    NEWCLASS(CGImageSourceImpl, o, sizeof(struct cgisrc) - sizeof(void *));
    NEWCLASS(CGImageDestinationImpl, o, sizeof(struct cgidst) - sizeof(void *));
    NEWCLASS(CGFontImpl, o, sizeof(struct cgfont) - sizeof(void *));
    iosrt_kCGColorSpaceSRGB = rt_str_new("kCGColorSpaceSRGB");
    iosrt_kCGColorSpaceGenericRGB = rt_str_new("kCGColorSpaceGenericRGB");
    iosrt_kCGColorSpaceDeviceRGB = rt_str_new("kCGColorSpaceDeviceRGB");
    iosrt_kCGColorSpaceGenericGray = rt_str_new("kCGColorSpaceGenericGray");
    iosrt_kCGColorSpaceGenericGrayGamma2_2 = rt_str_new("kCGColorSpaceGenericGrayGamma2_2");
    iosrt_kCGColorWhite = rt_str_new("kCGColorWhite"); iosrt_kCGColorBlack = rt_str_new("kCGColorBlack"); iosrt_kCGColorClear = rt_str_new("kCGColorClear");
    iosrt_kCGImagePropertyPixelWidth = rt_str_new("PixelWidth"); iosrt_kCGImagePropertyPixelHeight = rt_str_new("PixelHeight");
    iosrt_kCGImagePropertyOrientation = rt_str_new("Orientation");
    iosrt_kCGImageDestinationLossyCompressionQuality = rt_str_new("kCGImageDestinationLossyCompressionQuality");
    g_gray_space = iosrt_CGColorSpaceCreateDeviceGray();

    NEWCLASS(UIBezierPath, o, sizeof(struct uibez) - sizeof(void *));
    Class b = RT_CLASS(UIBezierPath);
    CM(b, "bezierPath", bz_bezierPath); CM(b, "bezierPathWithRect:", bz_rect); CM(b, "bezierPathWithOvalInRect:", bz_oval);
    CM(b, "bezierPathWithRoundedRect:cornerRadius:", bz_rounded); CM(b, "bezierPathWithRoundedRect:byRoundingCorners:cornerRadii:", bz_roundedCorners);
    CM(b, "bezierPathWithArcCenter:radius:startAngle:endAngle:clockwise:", bz_arc); CM(b, "bezierPathWithCGPath:", bz_withCGPath);
    IM(b, "init", bz_init); IM(b, "dealloc", bz_dealloc);
    IM(b, "moveToPoint:", bz_move); IM(b, "addLineToPoint:", bz_line); IM(b, "addCurveToPoint:controlPoint1:controlPoint2:", bz_curve);
    IM(b, "addQuadCurveToPoint:controlPoint:", bz_quad); IM(b, "addArcWithCenter:radius:startAngle:endAngle:clockwise:", bz_addArc);
    IM(b, "closePath", bz_close); IM(b, "removeAllPoints", bz_removeAll); IM(b, "appendPath:", bz_append);
    IM(b, "CGPath", bz_CGPath); IM(b, "setCGPath:", bz_setCGPath);
    IM(b, "lineWidth", bz_lw); IM(b, "setLineWidth:", bz_setLw); IM(b, "lineCapStyle", bz_cap); IM(b, "setLineCapStyle:", bz_setCap);
    IM(b, "lineJoinStyle", bz_join); IM(b, "setLineJoinStyle:", bz_setJoin); IM(b, "miterLimit", bz_miter); IM(b, "setMiterLimit:", bz_setMiter);
    IM(b, "flatness", bz_flat); IM(b, "setFlatness:", bz_setFlat);
    IM(b, "usesEvenOddFillRule", bz_eo); IM(b, "setUsesEvenOddFillRule:", bz_setEo); IM(b, "setLineDash:count:phase:", bz_setDash);
    IM(b, "fill", bz_fill); IM(b, "stroke", bz_stroke); IM(b, "fillWithBlendMode:alpha:", bz_fillBlend); IM(b, "strokeWithBlendMode:alpha:", bz_strokeBlend);
    IM(b, "addClip", bz_addClip); IM(b, "containsPoint:", bz_contains); IM(b, "bounds", bz_bounds); IM(b, "currentPoint", bz_current);
    IM(b, "isEmpty", bz_isEmpty); IM(b, "applyTransform:", bz_applyTransform); IM(b, "copyWithZone:", bz_copy); IM(b, "copy", bz_copy);
    IM(b, "bezierPathByReversingPath", bz_reversing);
}
void rt_cg_fill_color(struct cgctx *c, float *out) { if (c) memcpy(out, c->gs.fill, sizeof c->gs.fill); }
double rt_cg_path_length(id path) {
    if (!path) return 0;
    struct gfx_poly q; gfx_poly_init(&q); pb_to_poly(PB(path), NULL, &q);
    double len = 0;
    for (int k = 0; k < q.ncont; k++) {
        int st = q.starts[k], en = k + 1 < q.ncont ? q.starts[k + 1] : q.n;
        for (int i = st + 1; i < en; i++) len += hypot(q.xy[i * 2] - q.xy[i * 2 - 2], q.xy[i * 2 + 1] - q.xy[i * 2 - 1]);
        if (q.closed[k] && en - st > 1) len += hypot(q.xy[st * 2] - q.xy[(en - 1) * 2], q.xy[st * 2 + 1] - q.xy[(en - 1) * 2 + 1]);
    }
    gfx_poly_free(&q);
    return len;
}
