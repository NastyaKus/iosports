/*
 * QuartzCore HLE: CALayer trees (every UIView is backed by one), CAShapeLayer,
 * CAGradientLayer, CATextLayer, CATransform3D, CAAnimation family with
 * render-time interpolation of the common key paths (opacity, transform.*,
 * position, strokeStart/End), CATransaction completion blocks,
 * CAMediaTimingFunction and CADisplayLink.
 *
 * Rendering walks the layer tree into a CGContext: each layer concatenates
 * translate(position) * transform * translate(-anchor*size - bounds.origin),
 * then draws background, contents, its delegate's content (UIView drawing),
 * subclass content, sublayers (by zPosition) and finally the border.
 */
#include "uikit_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

RT_DECLARE_CLASS(NSObject);
RT_DECLARE_CLASS(NSMutableArray);
RT_DECLARE_CLASS(NSValue);
RT_DECLARE_CLASS(NSNumber);
RT_DECLARE_CLASS(UIView);
RT_DEFINE_CLASS(CALayer);
RT_DEFINE_CLASS(CAShapeLayer);
RT_DEFINE_CLASS(CAGradientLayer);
RT_DEFINE_CLASS(CATextLayer);
RT_DEFINE_CLASS(CAScrollLayer);
RT_DEFINE_CLASS(CAReplicatorLayer);
RT_DEFINE_CLASS(CAEAGLLayer);
RT_DEFINE_CLASS(CATransformLayer);
RT_DEFINE_CLASS(CAAnimation);
RT_DEFINE_CLASS(CAPropertyAnimation);
RT_DEFINE_CLASS(CABasicAnimation);
RT_DEFINE_CLASS(CASpringAnimation);
RT_DEFINE_CLASS(CAKeyframeAnimation);
RT_DEFINE_CLASS(CAAnimationGroup);
RT_DEFINE_CLASS(CATransition);
RT_DEFINE_CLASS(CAMediaTimingFunction);
RT_DEFINE_CLASS(CATransaction);
RT_DEFINE_CLASS(CADisplayLink);

void iosrt_dispatch_after_f(uint64_t when, void *q, void *ctx, void (*fn)(void *));
uint64_t iosrt_dispatch_time(uint64_t when, int64_t delta);
extern char iosrt_dispatch_main_q[];
void rt_view_draw(id view, struct cgctx *c, CGRect bounds);         /* uikit.c */
void rt_view_layer_resized(id view, CGSize old_size);              /* uikit.c */

double iosrt_CACurrentMediaTime(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec + ts.tv_nsec / 1e9; }

/* ============================================================ CATransform3D */
const CATransform3D iosrt_CATransform3DIdentity = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
static CATransform3D t3_mul(CATransform3D a, CATransform3D b) {
    const double *A = &a.m11, *B = &b.m11; CATransform3D r; double *R = &r.m11;
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) { double s = 0; for (int k = 0; k < 4; k++) s += A[i * 4 + k] * B[k * 4 + j]; R[i * 4 + j] = s; }
    return r;
}
CATransform3D iosrt_CATransform3DConcat(CATransform3D a, CATransform3D b) { return t3_mul(a, b); }
CATransform3D iosrt_CATransform3DMakeTranslation(double x, double y, double z) { CATransform3D t = iosrt_CATransform3DIdentity; t.m41 = x; t.m42 = y; t.m43 = z; return t; }
CATransform3D iosrt_CATransform3DMakeScale(double x, double y, double z) { CATransform3D t = iosrt_CATransform3DIdentity; t.m11 = x; t.m22 = y; t.m33 = z; return t; }
CATransform3D iosrt_CATransform3DMakeRotation(double a, double x, double y, double z) {
    double l = sqrt(x * x + y * y + z * z);
    if (l < 1e-12) return iosrt_CATransform3DIdentity;
    x /= l; y /= l; z /= l;
    double c = cos(a), s = sin(a), t = 1 - c;
    CATransform3D r = iosrt_CATransform3DIdentity;
    r.m11 = t * x * x + c;     r.m12 = t * x * y + z * s; r.m13 = t * x * z - y * s;
    r.m21 = t * x * y - z * s; r.m22 = t * y * y + c;     r.m23 = t * y * z + x * s;
    r.m31 = t * x * z + y * s; r.m32 = t * y * z - x * s; r.m33 = t * z * z + c;
    return r;
}
CATransform3D iosrt_CATransform3DTranslate(CATransform3D t, double x, double y, double z) { return t3_mul(iosrt_CATransform3DMakeTranslation(x, y, z), t); }
CATransform3D iosrt_CATransform3DScale(CATransform3D t, double x, double y, double z) { return t3_mul(iosrt_CATransform3DMakeScale(x, y, z), t); }
CATransform3D iosrt_CATransform3DRotate(CATransform3D t, double a, double x, double y, double z) { return t3_mul(iosrt_CATransform3DMakeRotation(a, x, y, z), t); }
rt_bool iosrt_CATransform3DIsIdentity(CATransform3D t) { return !memcmp(&t, &iosrt_CATransform3DIdentity, sizeof t); }
rt_bool iosrt_CATransform3DEqualToTransform(CATransform3D a, CATransform3D b) { return !memcmp(&a, &b, sizeof a); }
CATransform3D iosrt_CATransform3DMakeAffineTransform(gfx_mat m) { CATransform3D t = iosrt_CATransform3DIdentity; t.m11 = m.a; t.m12 = m.b; t.m21 = m.c; t.m22 = m.d; t.m41 = m.tx; t.m42 = m.ty; return t; }
gfx_mat iosrt_CATransform3DGetAffineTransform(CATransform3D t) { return (gfx_mat){ t.m11, t.m12, t.m21, t.m22, t.m41, t.m42 }; }
rt_bool iosrt_CATransform3DIsAffine(CATransform3D t) { return t.m13 == 0 && t.m14 == 0 && t.m23 == 0 && t.m24 == 0 && t.m31 == 0 && t.m32 == 0 && t.m33 == 1 && t.m34 == 0 && t.m43 == 0 && t.m44 == 1; }
CATransform3D iosrt_CATransform3DInvert(CATransform3D t) {
    /* general 4x4 inverse (cofactors) */
    double *m = &t.m11, inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    double det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (fabs(det) < 1e-12) return t;
    CATransform3D r; double *R = &r.m11;
    for (int i = 0; i < 16; i++) R[i] = inv[i] / det;
    return r;
}

/* ================================================================= layers */
struct calayer {
    Class isa;
    CGRect bounds; CGPoint position, anchor; double zpos; CATransform3D transform, sublayerTransform;
    id sublayers; id superlayer; id delegate;
    id bg; double corner, borderWidth; id borderColor; double opacity; long hidden, masks;
    id contents; CGRect contentsRect, contentsCenter; id gravity; double contentsScale;
    id mask; id name; id animations; id values; long needsDisplay, doubleSided, opaque, flipped, rasterize, needsLayout;
    id shadowColor; double shadowOpacity, shadowRadius; CGSize shadowOffset; id shadowPath; double speed;
    id actions; id style;
};
struct cashape { struct calayer l; id path, fill, stroke; double lw; id cap, join; double miter; id dash; double dashPhase, strokeStart, strokeEnd; id rule; };
struct cagrad { struct calayer l; id colors, locations; CGPoint start, end; id type; };
struct catext { struct calayer l; id string; id font; double fontSize; id fg; id align; long wraps; id truncation; };
#define L(x) ((struct calayer *)(x))

static id ns_num(double v) { return RT_SEND1(id, (id)RT_CLASS(NSNumber), "numberWithDouble:", double, v); }
static double num(id v) { return v ? RT_SEND0(double, v, "doubleValue") : 0; }
static id val_bytes(const void *b, const char *type) { return RT_SEND2(id, (id)RT_CLASS(NSValue), "valueWithBytes:objCType:", const void *, b, const char *, type); }
#define T3_TYPE "{CATransform3D=dddddddddddddddd}"
static id val_t3(CATransform3D t) { return val_bytes(&t, T3_TYPE); }
static CATransform3D t3_of(id v) {
    CATransform3D t = iosrt_CATransform3DIdentity;
    if (!v) return t;
    if (rt_is_kind_of(v, RT_CLASS(NSNumber))) return t;
    const char *ty = RT_SEND0(const char *, v, "objCType");
    if (ty && strstr(ty, "CATransform3D")) RT_SEND1(void, v, "getValue:", void *, &t);
    else if (ty && strstr(ty, "CGAffineTransform")) { gfx_mat m; RT_SEND1(void, v, "getValue:", void *, &m); t = iosrt_CATransform3DMakeAffineTransform(m); }
    return t;
}
static CGPoint pt_of(id v) { CGPoint p = { 0, 0 }; if (v && !rt_is_kind_of(v, RT_CLASS(NSNumber))) RT_SEND1(void, v, "getValue:", void *, &p); return p; }
static CGRect rect_of(id v) { CGRect r = { { 0, 0 }, { 0, 0 } }; if (v && !rt_is_kind_of(v, RT_CLASS(NSNumber))) RT_SEND1(void, v, "getValue:", void *, &r); return r; }

static void layer_setup(id self) {
    struct calayer *l = L(self);
    l->anchor = (CGPoint){ 0.5, 0.5 }; l->opacity = 1; l->transform = l->sublayerTransform = iosrt_CATransform3DIdentity;
    l->contentsRect = (CGRect){ { 0, 0 }, { 1, 1 } }; l->contentsCenter = l->contentsRect; l->contentsScale = 1; l->speed = 1;
    if (rt_is_kind_of(self, RT_CLASS(CAShapeLayer))) {
        struct cashape *s = (struct cashape *)self;
        s->fill = objc_retain(rt_color_make(0, 0, 0, 1)); s->lw = 1; s->miter = 10; s->strokeEnd = 1;
    }
    if (rt_is_kind_of(self, RT_CLASS(CAGradientLayer))) { struct cagrad *g = (struct cagrad *)self; g->start = (CGPoint){ 0.5, 0 }; g->end = (CGPoint){ 0.5, 1 }; }
    if (rt_is_kind_of(self, RT_CLASS(CATextLayer))) { struct catext *t = (struct catext *)self; t->fontSize = 36; t->fg = objc_retain(rt_color_make(1, 1, 1, 1)); }
}
static id ly_init(id self, SEL s) { (void)s; layer_setup(self); return self; }
static id ly_layer(Class c, SEL s) { (void)s; return objc_autorelease(ly_init(rt_alloc_instance(c, 0), NULL)); }
static void ly_dealloc(id self, SEL s) {
    (void)s; struct calayer *l = L(self);
    for (size_t i = 0; l->sublayers && i < rt_array_count(l->sublayers); i++) L(rt_array_at(l->sublayers, i))->superlayer = NULL;
    objc_release(l->sublayers); objc_release(l->bg); objc_release(l->borderColor); objc_release(l->contents); objc_release(l->gravity);
    objc_release(l->mask); objc_release(l->name); objc_release(l->animations); objc_release(l->values); objc_release(l->shadowColor);
    objc_release(l->shadowPath); objc_release(l->actions); objc_release(l->style);
    if (rt_is_kind_of(self, RT_CLASS(CAShapeLayer))) { struct cashape *x = (struct cashape *)self; objc_release(x->path); objc_release(x->fill); objc_release(x->stroke); objc_release(x->cap); objc_release(x->join); objc_release(x->dash); objc_release(x->rule); }
    if (rt_is_kind_of(self, RT_CLASS(CAGradientLayer))) { struct cagrad *x = (struct cagrad *)self; objc_release(x->colors); objc_release(x->locations); objc_release(x->type); }
    if (rt_is_kind_of(self, RT_CLASS(CATextLayer))) { struct catext *x = (struct catext *)self; objc_release(x->string); objc_release(x->font); objc_release(x->fg); objc_release(x->align); objc_release(x->truncation); }
    rt_dealloc_instance(self);
}
static void dirty(void) { rt_ui_mark_dirty(); }
#define SET_ID(field) do { objc_retain(v); objc_release(l->field); l->field = v; dirty(); } while (0)

/* geometry */
static CGRect ly_bounds(id self, SEL s) { (void)s; return L(self)->bounds; }
static void set_bounds(id self, CGRect r) {
    struct calayer *l = L(self); CGSize old = l->bounds.size;
    l->bounds = r; dirty();
    if (old.width != r.size.width || old.height != r.size.height) {
        l->needsLayout = 1;
        if (l->delegate && rt_is_kind_of(l->delegate, RT_CLASS(UIView))) rt_view_layer_resized(l->delegate, old);
    }
}
static void ly_setBounds(id self, SEL s, CGRect r) { (void)s; set_bounds(self, r); }
static CGPoint ly_position(id self, SEL s) { (void)s; return L(self)->position; }
static void ly_setPosition(id self, SEL s, CGPoint p) { (void)s; L(self)->position = p; dirty(); }
static CGPoint ly_anchor(id self, SEL s) { (void)s; return L(self)->anchor; }
static void ly_setAnchor(id self, SEL s, CGPoint p) { (void)s; L(self)->anchor = p; dirty(); }
static double ly_zpos(id self, SEL s) { (void)s; return L(self)->zpos; }
static void ly_setZpos(id self, SEL s, double z) { (void)s; L(self)->zpos = z; dirty(); }
static CATransform3D ly_transform(id self, SEL s) { (void)s; return L(self)->transform; }
static void ly_setTransform(id self, SEL s, CATransform3D t) { (void)s; L(self)->transform = t; dirty(); }
static gfx_mat ly_affine(id self, SEL s) { (void)s; return iosrt_CATransform3DGetAffineTransform(L(self)->transform); }
static void ly_setAffine(id self, SEL s, gfx_mat m) { (void)s; L(self)->transform = iosrt_CATransform3DMakeAffineTransform(m); dirty(); }
static CATransform3D ly_subT(id self, SEL s) { (void)s; return L(self)->sublayerTransform; }
static void ly_setSubT(id self, SEL s, CATransform3D t) { (void)s; L(self)->sublayerTransform = t; dirty(); }
/* layer-local -> superlayer space */
static gfx_mat local_mat_with(const struct calayer *l, CATransform3D t, CGPoint pos) {
    double aw = l->anchor.x * l->bounds.size.width, ah = l->anchor.y * l->bounds.size.height;
    gfx_mat m = { 1, 0, 0, 1, -l->bounds.origin.x - aw, -l->bounds.origin.y - ah };
    m = gfx_mat_mul(m, iosrt_CATransform3DGetAffineTransform(t));
    return gfx_mat_mul(m, (gfx_mat){ 1, 0, 0, 1, pos.x, pos.y });
}
static gfx_mat local_mat(id layer) { return local_mat_with(L(layer), L(layer)->transform, L(layer)->position); }
static gfx_mat to_root(id layer) {
    gfx_mat m = gfx_mat_identity();
    for (id x = layer; x; x = L(x)->superlayer) {
        m = gfx_mat_mul(m, local_mat(x));
        id sup = L(x)->superlayer;
        if (sup && !iosrt_CATransform3DIsIdentity(L(sup)->sublayerTransform)) m = gfx_mat_mul(m, iosrt_CATransform3DGetAffineTransform(L(sup)->sublayerTransform));
    }
    return m;
}
gfx_mat rt_layer_to_root(id layer) { return to_root(layer); }
static CGRect ly_frame(id self, SEL s) {
    (void)s; struct calayer *l = L(self);
    gfx_mat m = local_mat(self);
    return iosrt_CGRectApplyAffineTransform((CGRect){ l->bounds.origin, l->bounds.size }, m);
}
static void ly_setFrame(id self, SEL s, CGRect f) {
    (void)s; struct calayer *l = L(self);
    if (f.size.width < 0) { f.origin.x += f.size.width; f.size.width = -f.size.width; }
    if (f.size.height < 0) { f.origin.y += f.size.height; f.size.height = -f.size.height; }
    l->position = (CGPoint){ f.origin.x + l->anchor.x * f.size.width, f.origin.y + l->anchor.y * f.size.height };
    if (!iosrt_CATransform3DIsIdentity(l->transform)) {
        /* keep bounds as is when transformed (iOS: frame is undefined then); just recenter */
        dirty(); return;
    }
    set_bounds(self, (CGRect){ l->bounds.origin, f.size });
}
static CGPoint ly_convertPointTo(id self, SEL s, CGPoint p, id other) {
    (void)s; gfx_mat m = to_root(self);
    if (other) m = gfx_mat_mul(m, gfx_mat_invert(to_root(other)));
    double x, y; gfx_mat_apply(&m, p.x, p.y, &x, &y); return (CGPoint){ x, y };
}
static CGPoint ly_convertPointFrom(id self, SEL s, CGPoint p, id other) { return other ? ly_convertPointTo(other, s, p, self) : ({ gfx_mat m = gfx_mat_invert(to_root(self)); double x, y; gfx_mat_apply(&m, p.x, p.y, &x, &y); (CGPoint){ x, y }; }); }
static CGRect ly_convertRectTo(id self, SEL s, CGRect r, id other) {
    (void)s; gfx_mat m = to_root(self);
    if (other) m = gfx_mat_mul(m, gfx_mat_invert(to_root(other)));
    return iosrt_CGRectApplyAffineTransform(r, m);
}
static CGRect ly_convertRectFrom(id self, SEL s, CGRect r, id other) {
    if (other) return ly_convertRectTo(other, s, r, self);
    return iosrt_CGRectApplyAffineTransform(r, gfx_mat_invert(to_root(self)));
}
static rt_bool ly_contains(id self, SEL s, CGPoint p) { (void)s; return iosrt_CGRectContainsPoint(L(self)->bounds, p); }
static id ly_hitTest(id self, SEL s, CGPoint p) {
    struct calayer *l = L(self);
    if (l->hidden) return NULL;
    /* p is in the superlayer's coordinates */
    gfx_mat inv = gfx_mat_invert(local_mat(self)); double x, y; gfx_mat_apply(&inv, p.x, p.y, &x, &y);
    CGPoint lp = { x, y };
    if (l->masks && !iosrt_CGRectContainsPoint(l->bounds, lp)) return NULL;
    for (size_t i = l->sublayers ? rt_array_count(l->sublayers) : 0; i-- > 0;) {
        id h = ly_hitTest(rt_array_at(l->sublayers, i), s, lp);
        if (h) return h;
    }
    return iosrt_CGRectContainsPoint(l->bounds, lp) ? self : NULL;
}

/* hierarchy */
static id ly_sublayers(id self, SEL s) { (void)s; return L(self)->sublayers; }
static id ly_superlayer(id self, SEL s) { (void)s; return L(self)->superlayer; }
static void ly_removeFromSuperlayer(id self, SEL s) {
    (void)s; struct calayer *l = L(self); id sup = l->superlayer;
    if (!sup) return;
    l->superlayer = NULL;
    objc_retain(self);
    RT_SEND1(void, L(sup)->sublayers, "removeObjectIdenticalTo:", id, self);
    objc_autorelease(self); dirty();
}
static void ly_insertAt(id self, SEL s, id sub, unsigned long idx) {
    (void)s; if (!sub || sub == self) return;
    struct calayer *l = L(self);
    objc_retain(sub);
    if (L(sub)->superlayer) ly_removeFromSuperlayer(sub, NULL);
    if (!l->sublayers) l->sublayers = RT_SEND0(id, RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "alloc"), "init");
    size_t n = rt_array_count(l->sublayers);
    if (idx > n) idx = n;
    RT_SEND2(void, l->sublayers, "insertObject:atIndex:", id, sub, unsigned long, idx);
    L(sub)->superlayer = self;
    objc_release(sub); dirty();
}
static void ly_addSublayer(id self, SEL s, id sub) { ly_insertAt(self, s, sub, (unsigned long)-1 >> 1); }
static long index_of(id self, id sub) {
    struct calayer *l = L(self);
    for (size_t i = 0; l->sublayers && i < rt_array_count(l->sublayers); i++) if (rt_array_at(l->sublayers, i) == sub) return (long)i;
    return -1;
}
static void ly_insertBelow(id self, SEL s, id sub, id sib) { long i = index_of(self, sib); ly_insertAt(self, s, sub, i < 0 ? 0 : (unsigned long)i); i = index_of(self, sib); (void)i; }
static void ly_insertAbove(id self, SEL s, id sub, id sib) {
    objc_retain(sub);
    if (L(sub)->superlayer == self) ly_removeFromSuperlayer(sub, NULL);
    long i = index_of(self, sib); ly_insertAt(self, s, sub, i < 0 ? (unsigned long)-1 >> 1 : (unsigned long)i + 1);
    objc_release(sub);
}
static void ly_replace(id self, SEL s, id old, id nw) { long i = index_of(self, old); if (i < 0) return; ly_removeFromSuperlayer(old, NULL); ly_insertAt(self, s, nw, (unsigned long)i); }
static void ly_setSublayers(id self, SEL s, id arr) {
    struct calayer *l = L(self);
    while (l->sublayers && rt_array_count(l->sublayers)) ly_removeFromSuperlayer(rt_array_at(l->sublayers, 0), NULL);
    for (size_t i = 0; arr && i < rt_array_count(arr); i++) ly_addSublayer(self, s, rt_array_at(arr, i));
}

/* appearance properties */
static id ly_delegate(id self, SEL s) { (void)s; return L(self)->delegate; }
static void ly_setDelegate(id self, SEL s, id d) { (void)s; L(self)->delegate = d; }
static id ly_bg(id self, SEL s) { (void)s; return L(self)->bg; }
static void ly_setBg(id self, SEL s, id v) { (void)s; struct calayer *l = L(self); SET_ID(bg); }
static double ly_corner(id self, SEL s) { (void)s; return L(self)->corner; }
static void ly_setCorner(id self, SEL s, double v) { (void)s; L(self)->corner = v; dirty(); }
static double ly_bw(id self, SEL s) { (void)s; return L(self)->borderWidth; }
static void ly_setBw(id self, SEL s, double v) { (void)s; L(self)->borderWidth = v; dirty(); }
static id ly_bc(id self, SEL s) { (void)s; return L(self)->borderColor; }
static void ly_setBc(id self, SEL s, id v) { (void)s; struct calayer *l = L(self); SET_ID(borderColor); }
static float ly_opacity(id self, SEL s) { (void)s; return (float)L(self)->opacity; }
static void ly_setOpacity(id self, SEL s, float v) { (void)s; L(self)->opacity = v; dirty(); }
static rt_bool ly_hidden(id self, SEL s) { (void)s; return L(self)->hidden != 0; }
static void ly_setHidden(id self, SEL s, long v) { (void)s; L(self)->hidden = v & 0xff; dirty(); }
static rt_bool ly_masks(id self, SEL s) { (void)s; return L(self)->masks != 0; }
static void ly_setMasks(id self, SEL s, long v) { (void)s; L(self)->masks = v & 0xff; dirty(); }
static id ly_contents(id self, SEL s) { (void)s; return L(self)->contents; }
static void ly_setContents(id self, SEL s, id v) { (void)s; struct calayer *l = L(self); SET_ID(contents); }
static CGRect ly_contentsRect(id self, SEL s) { (void)s; return L(self)->contentsRect; }
static void ly_setContentsRect(id self, SEL s, CGRect r) { (void)s; L(self)->contentsRect = r; dirty(); }
static CGRect ly_contentsCenter(id self, SEL s) { (void)s; return L(self)->contentsCenter; }
static void ly_setContentsCenter(id self, SEL s, CGRect r) { (void)s; L(self)->contentsCenter = r; dirty(); }
static id ly_gravity(id self, SEL s) { (void)s; return L(self)->gravity; }
static void ly_setGravity(id self, SEL s, id v) { (void)s; struct calayer *l = L(self); SET_ID(gravity); }
static double ly_cscale(id self, SEL s) { (void)s; return L(self)->contentsScale; }
static void ly_setCscale(id self, SEL s, double v) { (void)s; L(self)->contentsScale = v; }
static id ly_mask(id self, SEL s) { (void)s; return L(self)->mask; }
static void ly_setMask(id self, SEL s, id v) { (void)s; struct calayer *l = L(self); SET_ID(mask); }
static id ly_name(id self, SEL s) { (void)s; return L(self)->name; }
static void ly_setName(id self, SEL s, id v) { (void)s; struct calayer *l = L(self); SET_ID(name); }
static id ly_shadowColor(id self, SEL s) { (void)s; return L(self)->shadowColor; }
static void ly_setShadowColor(id self, SEL s, id v) { (void)s; struct calayer *l = L(self); SET_ID(shadowColor); }
static float ly_shadowOpacity(id self, SEL s) { (void)s; return (float)L(self)->shadowOpacity; }
static void ly_setShadowOpacity(id self, SEL s, float v) { (void)s; L(self)->shadowOpacity = v; }
static double ly_shadowRadius(id self, SEL s) { (void)s; return L(self)->shadowRadius; }
static void ly_setShadowRadius(id self, SEL s, double v) { (void)s; L(self)->shadowRadius = v; }
static CGSize ly_shadowOffset(id self, SEL s) { (void)s; return L(self)->shadowOffset; }
static void ly_setShadowOffset(id self, SEL s, CGSize v) { (void)s; L(self)->shadowOffset = v; }
static id ly_shadowPath(id self, SEL s) { (void)s; return L(self)->shadowPath; }
static void ly_setShadowPath(id self, SEL s, id v) { (void)s; struct calayer *l = L(self); SET_ID(shadowPath); }
static float ly_speed(id self, SEL s) { (void)s; return (float)L(self)->speed; }
static void ly_setSpeed(id self, SEL s, float v) { (void)s; L(self)->speed = v; }
static id ly_actions(id self, SEL s) { (void)s; return L(self)->actions; }
static void ly_setActions(id self, SEL s, id v) { (void)s; struct calayer *l = L(self); objc_retain(v); objc_release(l->actions); l->actions = v; }
static rt_bool ly_bool0(id self, SEL s) { (void)self; (void)s; return 0; }
static void ly_noopL(id self, SEL s, long v) { (void)self; (void)s; (void)v; }
static void ly_noopD(id self, SEL s, double v) { (void)self; (void)s; (void)v; }
static void ly_noop1(id self, SEL s, id v) { (void)self; (void)s; (void)v; }
static void ly_noop(id self, SEL s) { (void)self; (void)s; }
static double ly_one(id self, SEL s) { (void)self; (void)s; return 1; }
static id ly_self(id self, SEL s) { (void)s; return self; }
static id ly_nil(id self, SEL s) { (void)self; (void)s; return NULL; }
static id ly_nil1(id self, SEL s, id a) { (void)self; (void)s; (void)a; return NULL; }
static rt_bool ly_needsDisplay(id self, SEL s) { (void)s; return L(self)->needsDisplay != 0; }
static void ly_setNeedsDisplay(id self, SEL s) { (void)s; L(self)->needsDisplay = 1; dirty(); }
static void ly_setNeedsDisplayInRect(id self, SEL s, CGRect r) { (void)r; ly_setNeedsDisplay(self, s); }
static void ly_setNeedsLayout(id self, SEL s) { (void)s; L(self)->needsLayout = 1; dirty(); if (L(self)->delegate && rt_responds(L(self)->delegate, rt_sel("setNeedsLayout")) && rt_is_kind_of(L(self)->delegate, RT_CLASS(UIView))) RT_SEND0(void, L(self)->delegate, "setNeedsLayout"); }
static rt_bool ly_needsLayout(id self, SEL s) { (void)s; return L(self)->needsLayout != 0; }
static void ly_layoutIfNeeded(id self, SEL s) {
    (void)s; struct calayer *l = L(self);
    if (l->delegate && rt_is_kind_of(l->delegate, RT_CLASS(UIView))) { RT_SEND0(void, l->delegate, "layoutIfNeeded"); return; }
    if (l->needsLayout) { l->needsLayout = 0; RT_SEND0(void, self, "layoutSublayers"); }
}
static void ly_layoutSublayers(id self, SEL s) {
    (void)s; id d = L(self)->delegate;
    if (d && !rt_is_kind_of(d, RT_CLASS(UIView)) && rt_responds(d, rt_sel("layoutSublayersOfLayer:"))) RT_SEND1(void, d, "layoutSublayersOfLayer:", id, self);
}
static void ly_display(id self, SEL s) { (void)s; L(self)->needsDisplay = 0; }
static void ly_drawInContext(id self, SEL s, struct cgctx *c) {
    (void)s; id d = L(self)->delegate;
    if (d && !rt_is_kind_of(d, RT_CLASS(UIView)) && rt_responds(d, rt_sel("drawLayer:inContext:"))) RT_SEND2(void, d, "drawLayer:inContext:", id, self, void *, c);
}
static void ly_renderInContext(id self, SEL s, struct cgctx *c) {
    (void)s; if (!c) return;
    /* render as if this layer were the root: undo its own position transform */
    iosrt_CGContextSaveGState(c);
    gfx_mat inv = gfx_mat_invert(local_mat(self));
    iosrt_CGContextConcatCTM(c, gfx_mat_mul(gfx_mat_identity(), inv));
    iosrt_CGContextTranslateCTM(c, L(self)->bounds.origin.x, L(self)->bounds.origin.y);
    rt_layer_render(self, c);
    iosrt_CGContextRestoreGState(c);
}
static CGSize ly_preferredFrameSize(id self, SEL s) { (void)s; return L(self)->bounds.size; }

/* --------------------------------------------------- shape/gradient/text */
#define SH(x) ((struct cashape *)(x))
static id sh_path(id self, SEL s) { (void)s; return SH(self)->path; }
static void sh_setPath(id self, SEL s, id v) { (void)s; struct cashape *l = SH(self); id c = v ? iosrt_CGPathCreateCopyByTransformingPath(v, NULL) : NULL; objc_release(l->path); l->path = c; dirty(); }
static id sh_fill(id self, SEL s) { (void)s; return SH(self)->fill; }
static void sh_setFill(id self, SEL s, id v) { (void)s; struct cashape *l = SH(self); SET_ID(fill); }
static id sh_stroke(id self, SEL s) { (void)s; return SH(self)->stroke; }
static void sh_setStroke(id self, SEL s, id v) { (void)s; struct cashape *l = SH(self); SET_ID(stroke); }
static double sh_lw(id self, SEL s) { (void)s; return SH(self)->lw; }
static void sh_setLw(id self, SEL s, double v) { (void)s; SH(self)->lw = v; dirty(); }
static double sh_miter(id self, SEL s) { (void)s; return SH(self)->miter; }
static void sh_setMiter(id self, SEL s, double v) { (void)s; SH(self)->miter = v; dirty(); }
static id sh_cap(id self, SEL s) { (void)s; return SH(self)->cap; }
static void sh_setCap(id self, SEL s, id v) { (void)s; struct cashape *l = SH(self); SET_ID(cap); }
static id sh_join(id self, SEL s) { (void)s; return SH(self)->join; }
static void sh_setJoin(id self, SEL s, id v) { (void)s; struct cashape *l = SH(self); SET_ID(join); }
static id sh_dash(id self, SEL s) { (void)s; return SH(self)->dash; }
static void sh_setDash(id self, SEL s, id v) { (void)s; struct cashape *l = SH(self); SET_ID(dash); }
static double sh_dashPhase(id self, SEL s) { (void)s; return SH(self)->dashPhase; }
static void sh_setDashPhase(id self, SEL s, double v) { (void)s; SH(self)->dashPhase = v; dirty(); }
static double sh_strokeStart(id self, SEL s) { (void)s; return SH(self)->strokeStart; }
static void sh_setStrokeStart(id self, SEL s, double v) { (void)s; SH(self)->strokeStart = v; dirty(); }
static double sh_strokeEnd(id self, SEL s) { (void)s; return SH(self)->strokeEnd; }
static void sh_setStrokeEnd(id self, SEL s, double v) { (void)s; SH(self)->strokeEnd = v; dirty(); }
static id sh_rule(id self, SEL s) { (void)s; return SH(self)->rule; }
static void sh_setRule(id self, SEL s, id v) { (void)s; struct cashape *l = SH(self); SET_ID(rule); }
#define GR(x) ((struct cagrad *)(x))
static id gr_colors(id self, SEL s) { (void)s; return GR(self)->colors; }
static void gr_setColors(id self, SEL s, id v) { (void)s; struct cagrad *l = GR(self); id c = v ? RT_SEND0(id, v, "copy") : NULL; objc_release(l->colors); l->colors = c; dirty(); }
static id gr_locations(id self, SEL s) { (void)s; return GR(self)->locations; }
static void gr_setLocations(id self, SEL s, id v) { (void)s; struct cagrad *l = GR(self); id c = v ? RT_SEND0(id, v, "copy") : NULL; objc_release(l->locations); l->locations = c; dirty(); }
static CGPoint gr_start(id self, SEL s) { (void)s; return GR(self)->start; }
static void gr_setStart(id self, SEL s, CGPoint p) { (void)s; GR(self)->start = p; dirty(); }
static CGPoint gr_end(id self, SEL s) { (void)s; return GR(self)->end; }
static void gr_setEnd(id self, SEL s, CGPoint p) { (void)s; GR(self)->end = p; dirty(); }
static id gr_type(id self, SEL s) { (void)s; return GR(self)->type; }
static void gr_setType(id self, SEL s, id v) { (void)s; struct cagrad *l = GR(self); SET_ID(type); }
#define TX(x) ((struct catext *)(x))
static id tx_string(id self, SEL s) { (void)s; return TX(self)->string; }
static void tx_setString(id self, SEL s, id v) { (void)s; struct catext *l = TX(self); id c = v ? RT_SEND0(id, v, "copy") : NULL; objc_release(l->string); l->string = c; dirty(); }
static id tx_font(id self, SEL s) { (void)s; return TX(self)->font; }
static void tx_setFont(id self, SEL s, id v) { (void)s; struct catext *l = TX(self); SET_ID(font); }
static double tx_fontSize(id self, SEL s) { (void)s; return TX(self)->fontSize; }
static void tx_setFontSize(id self, SEL s, double v) { (void)s; TX(self)->fontSize = v; dirty(); }
static id tx_fg(id self, SEL s) { (void)s; return TX(self)->fg; }
static void tx_setFg(id self, SEL s, id v) { (void)s; struct catext *l = TX(self); SET_ID(fg); }
static id tx_align(id self, SEL s) { (void)s; return TX(self)->align; }
static void tx_setAlign(id self, SEL s, id v) { (void)s; struct catext *l = TX(self); SET_ID(align); }
static rt_bool tx_wraps(id self, SEL s) { (void)s; return TX(self)->wraps != 0; }
static void tx_setWraps(id self, SEL s, long v) { (void)s; TX(self)->wraps = v & 0xff; dirty(); }
static void tx_setTrunc(id self, SEL s, id v) { (void)s; struct catext *l = TX(self); SET_ID(truncation); }

/* ============================================================ animations */
struct caanim {
    Class isa; id keyPath, from, to, by, values, keyTimes, timing, fillMode, delegate, animations, type, subtype, path, calcMode, completion_info;
    double duration, beginTime, timeOffset, speed, repeatDuration; float repeatCount; long autoreverses, removedOnCompletion, additive, cumulative;
    double t0; long started;   /* runtime: absolute start time */
};
#define AN(x) ((struct caanim *)(x))
static id an_init(id self, SEL s) { (void)s; AN(self)->removedOnCompletion = 1; AN(self)->speed = 1; return self; }
static id an_animation(Class c, SEL s) { (void)s; return objc_autorelease(an_init(rt_alloc_instance(c, 0), NULL)); }
static id an_withKeyPath(Class c, SEL s, id kp) { (void)s; id a = an_animation(c, NULL); AN(a)->keyPath = objc_retain(kp); return a; }
static void an_dealloc(id self, SEL s) {
    (void)s; struct caanim *a = AN(self);
    id *ids[] = { &a->keyPath, &a->from, &a->to, &a->by, &a->values, &a->keyTimes, &a->timing, &a->fillMode, &a->animations, &a->type, &a->subtype, &a->path, &a->calcMode, &a->delegate };
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) objc_release(*ids[i]);
    rt_dealloc_instance(self);
}
static id an_copy(id self, SEL s, void *z) {
    (void)s; (void)z; struct caanim *a = AN(self);
    id c = rt_alloc_instance(self->isa, 0); struct caanim *b = AN(c);
    *b = *a; b->isa = self->isa;
    id *ids[] = { &b->keyPath, &b->from, &b->to, &b->by, &b->values, &b->keyTimes, &b->timing, &b->fillMode, &b->animations, &b->type, &b->subtype, &b->path, &b->calcMode, &b->delegate };
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) objc_retain(*ids[i]);
    b->started = 0;
    return c;
}
#define AN_ID(get, set, field) \
    static id get(id self, SEL s) { (void)s; return AN(self)->field; } \
    static void set(id self, SEL s, id v) { (void)s; objc_retain(v); objc_release(AN(self)->field); AN(self)->field = v; }
#define AN_D(get, set, field, T) \
    static T get(id self, SEL s) { (void)s; return (T)AN(self)->field; } \
    static void set(id self, SEL s, T v) { (void)s; AN(self)->field = v; }
AN_ID(an_keyPath, an_setKeyPath, keyPath) AN_ID(an_from, an_setFrom, from) AN_ID(an_to, an_setTo, to) AN_ID(an_by, an_setBy, by)
AN_ID(an_values, an_setValues, values) AN_ID(an_keyTimes, an_setKeyTimes, keyTimes) AN_ID(an_timing, an_setTiming, timing)
AN_ID(an_fillMode, an_setFillMode, fillMode) AN_ID(an_delegate, an_setDelegate, delegate) AN_ID(an_animations, an_setAnimations, animations)
AN_ID(an_type, an_setType, type) AN_ID(an_subtype, an_setSubtype, subtype) AN_ID(an_path, an_setPath, path) AN_ID(an_calc, an_setCalc, calcMode)
AN_D(an_duration, an_setDuration, duration, double) AN_D(an_beginTime, an_setBeginTime, beginTime, double) AN_D(an_timeOffset, an_setTimeOffset, timeOffset, double)
AN_D(an_repeatDuration, an_setRepeatDuration, repeatDuration, double) AN_D(an_repeatCount, an_setRepeatCount, repeatCount, float) AN_D(an_speed, an_setSpeed, speed, float)
static rt_bool an_autoreverses(id self, SEL s) { (void)s; return AN(self)->autoreverses != 0; }
static void an_setAutoreverses(id self, SEL s, long v) { (void)s; AN(self)->autoreverses = v & 0xff; }
static rt_bool an_removed(id self, SEL s) { (void)s; return AN(self)->removedOnCompletion != 0; }
static void an_setRemoved(id self, SEL s, long v) { (void)s; AN(self)->removedOnCompletion = v & 0xff; }
static rt_bool an_additive(id self, SEL s) { (void)s; return AN(self)->additive != 0; }
static void an_setAdditive(id self, SEL s, long v) { (void)s; AN(self)->additive = v & 0xff; }
static rt_bool an_cumulative(id self, SEL s) { (void)s; return AN(self)->cumulative != 0; }
static void an_setCumulative(id self, SEL s, long v) { (void)s; AN(self)->cumulative = v & 0xff; }
static double anim_duration(id a) {
    double d = AN(a)->duration > 0 ? AN(a)->duration : 0.25;
    if (rt_is_kind_of(a, RT_CLASS(CAAnimationGroup)) && AN(a)->duration <= 0) {
        d = 0; for (size_t i = 0; AN(a)->animations && i < rt_array_count(AN(a)->animations); i++) { id x = rt_array_at(AN(a)->animations, i); double e = AN(x)->beginTime + anim_duration(x); if (e > d) d = e; }
    }
    return d;
}
static double anim_total(id a) {
    double d = anim_duration(a) * (AN(a)->autoreverses ? 2 : 1);
    float rc = AN(a)->repeatCount;
    if (rc > 1e30f || isinf(rc)) return INFINITY;
    if (rc > 0) d *= rc;
    if (AN(a)->repeatDuration > 0) d = AN(a)->repeatDuration;
    return d / (AN(a)->speed > 0 ? AN(a)->speed : 1);
}

/* timing functions */
struct catf { Class isa; float c1x, c1y, c2x, c2y; };
static id tf_make(Class c, float a, float b, float d, float e) { id t = rt_alloc_instance(c ? c : RT_CLASS(CAMediaTimingFunction), 0); struct catf *x = (struct catf *)t; x->c1x = a; x->c1y = b; x->c2x = d; x->c2y = e; return objc_autorelease(t); }
static id tf_withName(Class c, SEL s, id name) {
    (void)s; const char *n = name ? rt_str_utf8(name) : "";
    if (!strcmp(n, "easeIn")) return tf_make(c, .42f, 0, 1, 1);
    if (!strcmp(n, "easeOut")) return tf_make(c, 0, 0, .58f, 1);
    if (!strcmp(n, "easeInEaseOut")) return tf_make(c, .42f, 0, .58f, 1);
    if (!strcmp(n, "default")) return tf_make(c, .25f, .1f, .25f, 1);
    return tf_make(c, 0, 0, 1, 1);
}
static id tf_withPoints(Class c, SEL s, float a, float b, float d, float e) { (void)s; return tf_make(c, a, b, d, e); }
static id tf_initPoints(id self, SEL s, float a, float b, float d, float e) { (void)s; struct catf *x = (struct catf *)self; x->c1x = a; x->c1y = b; x->c2x = d; x->c2y = e; return self; }
static void tf_getPoint(id self, SEL s, unsigned long i, float *v) {
    (void)s; struct catf *x = (struct catf *)self;
    float p[4][2] = { { 0, 0 }, { x->c1x, x->c1y }, { x->c2x, x->c2y }, { 1, 1 } };
    if (i < 4 && v) { v[0] = p[i][0]; v[1] = p[i][1]; }
}
static double bez(double a, double b, double t) { double u = 1 - t; return 3 * u * u * t * a + 3 * u * t * t * b + t * t * t; }
static double tf_eval(id tf, double x) {
    if (!tf) return x;
    struct catf *f = (struct catf *)tf;
    double lo = 0, hi = 1, t = x;
    for (int i = 0; i < 30; i++) { t = (lo + hi) / 2; if (bez(f->c1x, f->c2x, t) < x) lo = t; else hi = t; }
    return bez(f->c1y, f->c2y, t);
}

/* active animation progress in [0,1] for `a` at time now (NAN when not active and not filling) */
static double anim_progress(id a, double now, int *running) {
    struct caanim *x = AN(a);
    double d = anim_duration(a), sp = x->speed > 0 ? x->speed : 1;
    double el = (now - x->t0) * sp + x->timeOffset;
    double total = anim_total(a) * sp;
    *running = 1;
    if (el < 0) { *running = 1; return 0; }
    if (el >= total) {
        *running = 0;
        if (x->removedOnCompletion) return NAN;
        const char *fm = x->fillMode ? rt_str_utf8(x->fillMode) : "removed";
        if (strcmp(fm, "forwards") && strcmp(fm, "both")) return NAN;
        el = total - 1e-9;
    }
    double cyc = d * (x->autoreverses ? 2 : 1);
    double p = fmod(el, cyc) / d;
    if (p > 1) p = 2 - p;
    return tf_eval(x->timing, p < 0 ? 0 : p > 1 ? 1 : p);
}
/* interpolate an NSNumber/NSValue-ish value; returns double for numbers */
static double interp_num(id a, double p, double base) {
    struct caanim *x = AN(a);
    if (x->values && rt_array_count(x->values)) {
        size_t n = rt_array_count(x->values);
        if (n == 1) return num(rt_array_at(x->values, 0));
        double pos = p * (double)(n - 1); size_t i = (size_t)floor(pos); if (i >= n - 1) i = n - 2;
        if (x->keyTimes && rt_array_count(x->keyTimes) == n) {
            for (i = 0; i + 2 < n && num(rt_array_at(x->keyTimes, i + 1)) < p; i++) {}
            double k0 = num(rt_array_at(x->keyTimes, i)), k1 = num(rt_array_at(x->keyTimes, i + 1));
            pos = i + (k1 > k0 ? (p - k0) / (k1 - k0) : 1);
        }
        double f = pos - (double)i, v0 = num(rt_array_at(x->values, i)), v1 = num(rt_array_at(x->values, i + 1));
        return v0 + (v1 - v0) * f;
    }
    double from = x->from ? num(x->from) : base, to;
    if (x->to) to = num(x->to); else if (x->by) to = from + num(x->by); else to = base;
    if (!x->from && x->by && x->to) from = to - num(x->by);
    double v = from + (to - from) * p;
    return x->additive ? base + v : v;
}
static CGPoint interp_pt(id a, double p, CGPoint base) {
    struct caanim *x = AN(a);
    if (x->values && rt_array_count(x->values) > 1) {
        size_t n = rt_array_count(x->values); double pos = p * (double)(n - 1); size_t i = (size_t)floor(pos); if (i >= n - 1) i = n - 2;
        CGPoint a0 = pt_of(rt_array_at(x->values, i)), a1 = pt_of(rt_array_at(x->values, i + 1)); double f = pos - (double)i;
        return (CGPoint){ a0.x + (a1.x - a0.x) * f, a0.y + (a1.y - a0.y) * f };
    }
    CGPoint from = x->from ? pt_of(x->from) : base, to = x->to ? pt_of(x->to) : base;
    if (!x->to && x->by) { CGPoint b = pt_of(x->by); to = (CGPoint){ from.x + b.x, from.y + b.y }; }
    return (CGPoint){ from.x + (to.x - from.x) * p, from.y + (to.y - from.y) * p };
}
static CATransform3D interp_t3(id a, double p, CATransform3D base) {
    struct caanim *x = AN(a);
    CATransform3D from = x->from ? t3_of(x->from) : base, to = x->to ? t3_of(x->to) : base;
    if (x->values && rt_array_count(x->values) > 1) {
        size_t n = rt_array_count(x->values); double pos = p * (double)(n - 1); size_t i = (size_t)floor(pos); if (i >= n - 1) i = n - 2;
        from = t3_of(rt_array_at(x->values, i)); to = t3_of(rt_array_at(x->values, i + 1)); p = pos - (double)i;
    }
    CATransform3D r; double *R = &r.m11, *A = &from.m11, *B = &to.m11;
    for (int i = 0; i < 16; i++) R[i] = A[i] + (B[i] - A[i]) * p;
    return r;
}
/* presentation values for rendering */
struct pres { double opacity; CATransform3D transform; CGPoint position; double strokeStart, strokeEnd; };
static int g_animating;
static void apply_anim(id layer, id a, double now, struct pres *pr, double parent_p) {
    int running;
    double p = isnan(parent_p) ? anim_progress(a, now, &running) : parent_p;
    if (isnan(parent_p) && running) g_animating = 1;
    if (isnan(p)) return;
    if (rt_is_kind_of(a, RT_CLASS(CAAnimationGroup))) {
        for (size_t i = 0; AN(a)->animations && i < rt_array_count(AN(a)->animations); i++) {
            id sub = rt_array_at(AN(a)->animations, i);
            double gd = anim_duration(a), sd = anim_duration(sub), b = AN(sub)->beginTime;
            double t = p * gd - b, sp = sd > 0 ? t / sd : 1;
            if (t < 0) continue;
            if (sp > 1) { if (AN(sub)->removedOnCompletion && !AN(a)->fillMode) continue; sp = 1; }
            apply_anim(layer, sub, now, pr, tf_eval(AN(sub)->timing, sp));
        }
        return;
    }
    const char *kp = AN(a)->keyPath ? rt_str_utf8(AN(a)->keyPath) : "";
    if (!strcmp(kp, "opacity")) pr->opacity = interp_num(a, p, pr->opacity);
    else if (!strcmp(kp, "transform")) pr->transform = interp_t3(a, p, pr->transform);
    else if (!strcmp(kp, "transform.rotation.z") || !strcmp(kp, "transform.rotation")) {
        double ang = interp_num(a, p, 0);
        pr->transform = t3_mul(iosrt_CATransform3DMakeRotation(ang, 0, 0, 1), AN(a)->additive || !AN(a)->from ? pr->transform : iosrt_CATransform3DIdentity);
    }
    else if (!strcmp(kp, "transform.scale")) { double k = interp_num(a, p, 1); pr->transform = t3_mul(iosrt_CATransform3DMakeScale(k, k, 1), pr->transform); }
    else if (!strcmp(kp, "transform.scale.x")) { double k = interp_num(a, p, 1); pr->transform = t3_mul(iosrt_CATransform3DMakeScale(k, 1, 1), pr->transform); }
    else if (!strcmp(kp, "transform.scale.y")) { double k = interp_num(a, p, 1); pr->transform = t3_mul(iosrt_CATransform3DMakeScale(1, k, 1), pr->transform); }
    else if (!strcmp(kp, "transform.translation.x")) { double v = interp_num(a, p, 0); pr->transform = t3_mul(pr->transform, iosrt_CATransform3DMakeTranslation(v, 0, 0)); }
    else if (!strcmp(kp, "transform.translation.y")) { double v = interp_num(a, p, 0); pr->transform = t3_mul(pr->transform, iosrt_CATransform3DMakeTranslation(0, v, 0)); }
    else if (!strcmp(kp, "position")) pr->position = interp_pt(a, p, pr->position);
    else if (!strcmp(kp, "position.x")) pr->position.x = interp_num(a, p, pr->position.x);
    else if (!strcmp(kp, "position.y")) pr->position.y = interp_num(a, p, pr->position.y);
    else if (!strcmp(kp, "strokeEnd")) pr->strokeEnd = interp_num(a, p, pr->strokeEnd);
    else if (!strcmp(kp, "strokeStart")) pr->strokeStart = interp_num(a, p, pr->strokeStart);
}
static void presentation(id layer, struct pres *pr) {
    struct calayer *l = L(layer);
    pr->opacity = l->opacity; pr->transform = l->transform; pr->position = l->position;
    pr->strokeStart = rt_is_kind_of(layer, RT_CLASS(CAShapeLayer)) ? SH(layer)->strokeStart : 0;
    pr->strokeEnd = rt_is_kind_of(layer, RT_CLASS(CAShapeLayer)) ? SH(layer)->strokeEnd : 1;
    if (!l->animations || !RT_SEND0(unsigned long, l->animations, "count")) return;
    double now = iosrt_CACurrentMediaTime();
    id keys = RT_SEND0(id, l->animations, "allKeys");
    for (size_t i = 0; i < rt_array_count(keys); i++) apply_anim(layer, rt_dict_get(l->animations, rt_array_at(keys, i)), now, pr, NAN);
}
int rt_layers_animating(void) { return g_animating; }

/* animation lifecycle */
struct anim_done { id layer; id anim; id key; };
static void anim_finish(void *p) {
    struct anim_done *d = p;
    struct calayer *l = L(d->layer);
    id cur = l->animations ? rt_dict_get(l->animations, d->key) : NULL;
    if (cur == d->anim) {
        if (AN(d->anim)->removedOnCompletion) RT_SEND1(void, l->animations, "removeObjectForKey:", id, d->key);
        id del = AN(d->anim)->delegate;
        if (del && rt_responds(del, rt_sel("animationDidStop:finished:"))) RT_SEND2(void, del, "animationDidStop:finished:", id, d->anim, long, 1);
    }
    dirty();
    objc_release(d->layer); objc_release(d->anim); objc_release(d->key); free(d);
}
static void anim_start(void *p) {
    struct anim_done *d = p;
    id del = AN(d->anim)->delegate;
    if (del && rt_responds(del, rt_sel("animationDidStart:"))) RT_SEND1(void, del, "animationDidStart:", id, d->anim);
    objc_release(d->layer); objc_release(d->anim); objc_release(d->key); free(d);
}
static double g_txn_max;   /* longest animation added in the current transaction */
static id ly_animationForKey(id self, SEL s, id key) { (void)s; return L(self)->animations && key ? rt_dict_get(L(self)->animations, key) : NULL; }
static void ly_addAnimation(id self, SEL s, id anim, id key) {
    (void)s; if (!anim) return;
    struct calayer *l = L(self);
    if (!l->animations) l->animations = RT_SEND0(id, RT_SEND0(id, (id)rt_class_named("NSMutableDictionary"), "alloc"), "init");
    id copy = objc_autorelease(an_copy(anim, NULL, NULL));
    double now = iosrt_CACurrentMediaTime();
    AN(copy)->t0 = AN(copy)->beginTime > 1 ? AN(copy)->beginTime : now + AN(copy)->beginTime;
    AN(copy)->started = 1;
    static unsigned long seq;
    if (!key) { char b[32]; snprintf(b, sizeof b, "iosrt_anim_%lu", ++seq); key = rt_str(b); }
    RT_SEND2(void, l->animations, "setObject:forKey:", id, copy, id, key);
    double total = anim_total(copy) + (AN(copy)->t0 - now);
    if (total > g_txn_max && isfinite(total)) g_txn_max = total;
    struct anim_done *st = malloc(sizeof *st); st->layer = objc_retain(self); st->anim = objc_retain(copy); st->key = objc_retain(key);
    iosrt_dispatch_after_f(iosrt_dispatch_time(0, 0), iosrt_dispatch_main_q, st, anim_start);
    if (isfinite(total)) {
        struct anim_done *d = malloc(sizeof *d); d->layer = objc_retain(self); d->anim = objc_retain(copy); d->key = objc_retain(key);
        iosrt_dispatch_after_f(iosrt_dispatch_time(0, (int64_t)(total * 1e9)), iosrt_dispatch_main_q, d, anim_finish);
    }
    dirty();
}
static void ly_removeAnimation(id self, SEL s, id key) {
    (void)s; struct calayer *l = L(self); if (!l->animations || !key) return;
    id a = rt_dict_get(l->animations, key);
    if (a) {
        objc_retain(a);
        RT_SEND1(void, l->animations, "removeObjectForKey:", id, key);
        id del = AN(a)->delegate;
        if (del && rt_responds(del, rt_sel("animationDidStop:finished:"))) RT_SEND2(void, del, "animationDidStop:finished:", id, a, long, 0);
        objc_release(a);
    }
    dirty();
}
static void ly_removeAll(id self, SEL s) {
    struct calayer *l = L(self); if (!l->animations) return;
    id keys = RT_SEND0(id, l->animations, "allKeys");
    for (size_t i = 0; i < rt_array_count(keys); i++) ly_removeAnimation(self, s, rt_array_at(keys, i));
}
static id ly_animationKeys(id self, SEL s) { (void)s; struct calayer *l = L(self); return l->animations && RT_SEND0(unsigned long, l->animations, "count") ? RT_SEND0(id, l->animations, "allKeys") : NULL; }
static id ly_presentationLayer(id self, SEL s) { (void)s; return self; }

/* KVC on layers (values used by animations and NIB user-defined attributes) */
static void ly_setValueForKey(id self, SEL s, id v, id key);
static id ly_valueForKey(id self, SEL s, id key) {
    (void)s; struct calayer *l = L(self); const char *k = key ? rt_str_utf8(key) : "";
    if (!strcmp(k, "opacity")) return ns_num(l->opacity);
    if (!strcmp(k, "cornerRadius")) return ns_num(l->corner);
    if (!strcmp(k, "borderWidth")) return ns_num(l->borderWidth);
    if (!strcmp(k, "zPosition")) return ns_num(l->zpos);
    if (!strcmp(k, "hidden")) return ns_num((double)l->hidden);
    if (!strcmp(k, "position")) return val_bytes(&l->position, "{CGPoint=dd}");
    if (!strcmp(k, "anchorPoint")) return val_bytes(&l->anchor, "{CGPoint=dd}");
    if (!strcmp(k, "bounds")) return val_bytes(&l->bounds, "{CGRect={CGPoint=dd}{CGSize=dd}}");
    if (!strcmp(k, "transform")) return val_t3(l->transform);
    if (!strcmp(k, "backgroundColor")) return l->bg;
    if (!strcmp(k, "borderColor")) return l->borderColor;
    if (!strcmp(k, "contents")) return l->contents;
    if (!strcmp(k, "transform.rotation.z") || !strcmp(k, "transform.rotation")) return ns_num(atan2(l->transform.m12, l->transform.m11));
    if (!strcmp(k, "transform.scale") || !strcmp(k, "transform.scale.x")) return ns_num(hypot(l->transform.m11, l->transform.m12));
    if (!strcmp(k, "transform.scale.y")) return ns_num(hypot(l->transform.m21, l->transform.m22));
    if (!strcmp(k, "position.x")) return ns_num(l->position.x);
    if (!strcmp(k, "position.y")) return ns_num(l->position.y);
    if (rt_is_kind_of(self, RT_CLASS(CAShapeLayer))) {
        if (!strcmp(k, "strokeEnd")) return ns_num(SH(self)->strokeEnd);
        if (!strcmp(k, "strokeStart")) return ns_num(SH(self)->strokeStart);
        if (!strcmp(k, "path")) return SH(self)->path;
        if (!strcmp(k, "lineWidth")) return ns_num(SH(self)->lw);
    }
    return l->values ? rt_dict_get(l->values, key) : NULL;
}
static void ly_setValueForKey(id self, SEL s, id v, id key) {
    struct calayer *l = L(self); const char *k = key ? rt_str_utf8(key) : "";
    if (!strcmp(k, "opacity")) { l->opacity = num(v); dirty(); return; }
    if (!strcmp(k, "cornerRadius")) { l->corner = num(v); dirty(); return; }
    if (!strcmp(k, "borderWidth")) { l->borderWidth = num(v); dirty(); return; }
    if (!strcmp(k, "zPosition")) { l->zpos = num(v); dirty(); return; }
    if (!strcmp(k, "hidden")) { l->hidden = num(v) != 0; dirty(); return; }
    if (!strcmp(k, "masksToBounds")) { l->masks = num(v) != 0; dirty(); return; }
    if (!strcmp(k, "position")) { l->position = pt_of(v); dirty(); return; }
    if (!strcmp(k, "anchorPoint")) { l->anchor = pt_of(v); dirty(); return; }
    if (!strcmp(k, "bounds")) { set_bounds(self, rect_of(v)); return; }
    if (!strcmp(k, "transform")) { l->transform = t3_of(v); dirty(); return; }
    if (!strcmp(k, "backgroundColor")) { ly_setBg(self, s, v); return; }
    if (!strcmp(k, "borderColor")) { ly_setBc(self, s, v); return; }
    if (!strcmp(k, "contents")) { ly_setContents(self, s, v); return; }
    if (!strcmp(k, "transform.rotation.z") || !strcmp(k, "transform.rotation")) { CATransform3D t = iosrt_CATransform3DMakeRotation(num(v), 0, 0, 1); double sc = hypot(l->transform.m11, l->transform.m12); l->transform = t3_mul(iosrt_CATransform3DMakeScale(sc, sc, 1), t); dirty(); return; }
    if (!strcmp(k, "transform.scale")) { double a = atan2(l->transform.m12, l->transform.m11), sc = num(v); l->transform = t3_mul(iosrt_CATransform3DMakeScale(sc, sc, 1), iosrt_CATransform3DMakeRotation(a, 0, 0, 1)); dirty(); return; }
    if (!strcmp(k, "position.x")) { l->position.x = num(v); dirty(); return; }
    if (!strcmp(k, "position.y")) { l->position.y = num(v); dirty(); return; }
    if (rt_is_kind_of(self, RT_CLASS(CAShapeLayer))) {
        if (!strcmp(k, "strokeEnd")) { SH(self)->strokeEnd = num(v); dirty(); return; }
        if (!strcmp(k, "strokeStart")) { SH(self)->strokeStart = num(v); dirty(); return; }
        if (!strcmp(k, "path")) { sh_setPath(self, s, v); return; }
        if (!strcmp(k, "lineWidth")) { SH(self)->lw = num(v); dirty(); return; }
    }
    if (!l->values) l->values = RT_SEND0(id, RT_SEND0(id, (id)rt_class_named("NSMutableDictionary"), "alloc"), "init");
    if (v) RT_SEND2(void, l->values, "setObject:forKey:", id, v, id, key); else RT_SEND1(void, l->values, "removeObjectForKey:", id, key);
}

/* ============================================================== rendering */
static void rounded_path(struct cgctx *c, CGRect r, double rad) {
    if (rad <= 0) { iosrt_CGContextAddRect(c, r); return; }
    id p = iosrt_CGPathCreateWithRoundedRect(r, rad, rad, NULL);
    iosrt_CGContextAddPath(c, p); objc_release(p);
}
static int gravity_rect(id gravity, CGRect b, CGSize content, CGRect *out) {
    const char *g = gravity ? rt_str_utf8(gravity) : "resize";
    double bw = b.size.width, bh = b.size.height, cw = content.width, ch = content.height;
    if (!strcmp(g, "resize") || cw <= 0 || ch <= 0) { *out = b; return 1; }
    if (!strcmp(g, "resizeAspect") || !strcmp(g, "resizeAspectFill")) {
        double k = !strcmp(g, "resizeAspect") ? fmin(bw / cw, bh / ch) : fmax(bw / cw, bh / ch);
        *out = (CGRect){ { b.origin.x + (bw - cw * k) / 2, b.origin.y + (bh - ch * k) / 2 }, { cw * k, ch * k } }; return 1;
    }
    double x = b.origin.x + (bw - cw) / 2, y = b.origin.y + (bh - ch) / 2;
    if (strstr(g, "left") || strstr(g, "Left")) x = b.origin.x;
    if (strstr(g, "right") || strstr(g, "Right")) x = b.origin.x + bw - cw;
    if (!strncmp(g, "top", 3)) y = b.origin.y;   /* iOS: "top" is the min-y edge in the flipped layer space */
    if (!strncmp(g, "bottom", 6)) y = b.origin.y + bh - ch;
    *out = (CGRect){ { x, y }, content }; return 1;
}
static void draw_contents(id layer, struct cgctx *c) {
    struct calayer *l = L(layer);
    id cg = l->contents;
    if (cg && !rt_cgimage_gfx(cg)) cg = rt_uiimage_cgimage(cg);   /* someone set a UIImage */
    struct gfx_image *g = rt_cgimage_gfx(cg);
    if (!g) return;
    double sc = l->contentsScale > 0 ? l->contentsScale : 1;
    CGRect cr = l->contentsRect;
    id sub = NULL;
    if (cr.origin.x != 0 || cr.origin.y != 0 || cr.size.width != 1 || cr.size.height != 1) {
        extern id iosrt_CGImageCreateWithImageInRect(id, CGRect);
        sub = iosrt_CGImageCreateWithImageInRect(cg, (CGRect){ { cr.origin.x * g->w, cr.origin.y * g->h }, { cr.size.width * g->w, cr.size.height * g->h } });
        if (sub) { cg = sub; g = rt_cgimage_gfx(sub); }
    }
    CGRect dst; gravity_rect(l->gravity, l->bounds, (CGSize){ g->w / sc, g->h / sc }, &dst);
    rt_cg_draw_image_uikit(c, cg, dst, 1, rt_cg_blend(c), NULL);
    objc_release(sub);
}
static void draw_shape(id layer, struct cgctx *c, const struct pres *pr) {
    struct cashape *s = SH(layer);
    if (!s->path) return;
    if (s->fill && pr->strokeStart <= 0 && pr->strokeEnd >= 1) {
        iosrt_CGContextBeginPath(c); iosrt_CGContextAddPath(c, s->path);
        iosrt_CGContextSetFillColorWithColor(c, s->fill);
        if (s->rule && !strcmp(rt_str_utf8(s->rule), "even-odd")) iosrt_CGContextEOFillPath(c); else iosrt_CGContextFillPath(c);
    } else if (s->fill) {
        iosrt_CGContextBeginPath(c); iosrt_CGContextAddPath(c, s->path);
        iosrt_CGContextSetFillColorWithColor(c, s->fill); iosrt_CGContextFillPath(c);
    }
    if (s->stroke && s->lw > 0 && pr->strokeEnd > pr->strokeStart) {
        iosrt_CGContextSetStrokeColorWithColor(c, s->stroke); iosrt_CGContextSetLineWidth(c, s->lw);
        const char *cap = s->cap ? rt_str_utf8(s->cap) : "butt", *join = s->join ? rt_str_utf8(s->join) : "miter";
        iosrt_CGContextSetLineCap(c, !strcmp(cap, "round") ? 1 : !strcmp(cap, "square") ? 2 : 0);
        iosrt_CGContextSetLineJoin(c, !strcmp(join, "round") ? 1 : !strcmp(join, "bevel") ? 2 : 0);
        iosrt_CGContextSetMiterLimit(c, s->miter);
        double dash[16]; size_t nd = 0;
        for (size_t i = 0; s->dash && i < rt_array_count(s->dash) && i < 16; i++) dash[nd++] = num(rt_array_at(s->dash, i));
        iosrt_CGContextSetLineDash(c, s->dashPhase, nd ? dash : NULL, nd);
        iosrt_CGContextBeginPath(c);
        if (pr->strokeStart > 0 || pr->strokeEnd < 1) {
            /* partial stroke: one dash covering [start, end] of the path length */
            extern double rt_cg_path_length(id path);
            double total = rt_cg_path_length(s->path);
            double a = fmax(0, pr->strokeStart) * total, b = fmin(1, pr->strokeEnd) * total;
            double d2[2] = { b - a, total * 2 + 1 };
            iosrt_CGContextSetLineDash(c, d2[0] + d2[1] - a, d2, 2);
            iosrt_CGContextAddPath(c, s->path);
        } else iosrt_CGContextAddPath(c, s->path);
        iosrt_CGContextStrokePath(c);
    }
}
static void draw_gradient(id layer, struct cgctx *c) {
    struct cagrad *g = GR(layer);
    size_t n = g->colors ? rt_array_count(g->colors) : 0;
    if (!n) return;
    double locs[GFX_MAX_STOPS];
    for (size_t i = 0; i < n && i < GFX_MAX_STOPS; i++) locs[i] = g->locations && i < rt_array_count(g->locations) ? num(rt_array_at(g->locations, i)) : (n > 1 ? (double)i / (n - 1) : 0);
    extern id iosrt_CGGradientCreateWithColors(id, id, const double *);
    extern void iosrt_CGContextDrawLinearGradient(struct cgctx *, id, CGPoint, CGPoint, unsigned);
    extern void iosrt_CGContextDrawRadialGradient(struct cgctx *, id, CGPoint, double, CGPoint, double, unsigned);
    id grad = iosrt_CGGradientCreateWithColors(NULL, g->colors, locs);
    CGRect b = L(layer)->bounds;
    iosrt_CGContextSaveGState(c); iosrt_CGContextClipToRect(c, b);
    CGPoint p0 = { b.origin.x + g->start.x * b.size.width, b.origin.y + g->start.y * b.size.height };
    CGPoint p1 = { b.origin.x + g->end.x * b.size.width, b.origin.y + g->end.y * b.size.height };
    if (g->type && !strcmp(rt_str_utf8(g->type), "radial")) iosrt_CGContextDrawRadialGradient(c, grad, p0, 0, p0, hypot(p1.x - p0.x, p1.y - p0.y), 3);
    else iosrt_CGContextDrawLinearGradient(c, grad, p0, p1, 3);
    iosrt_CGContextRestoreGState(c);
    objc_release(grad);
}
static void draw_textlayer(id layer, struct cgctx *c) {
    struct catext *t = TX(layer);
    if (!t->string) return;
    id str = t->string; id color = t->fg;
    if (!rt_is_kind_of(str, (Class)rt_class_named("NSString"))) {
        id attrs = RT_SEND0(unsigned long, str, "length") ? RT_SEND2(id, str, "attributesAtIndex:effectiveRange:", unsigned long, 0, void *, NULL) : NULL;
        if (rt_attrs_color(attrs)) color = rt_attrs_color(attrs);
        str = RT_SEND0(id, str, "string");
    }
    id font = t->font && rt_is_kind_of(t->font, (Class)rt_class_named("UIFont")) ? RT_SEND1(id, t->font, "fontWithSize:", double, t->fontSize) : rt_font_make(t->fontSize, 0, NULL);
    const char *al = t->align ? rt_str_utf8(t->align) : "natural";
    long align = !strcmp(al, "center") ? 1 : !strcmp(al, "right") ? 2 : 0;
    float col[4]; rt_color_rgba(color, col);
    rt_text_draw(c, rt_str_utf8(str), font, col, L(layer)->bounds, align, t->wraps ? 0 : 1, 0);
}
static int cmp_z(const void *a, const void *b) { double za = L(*(id *)a)->zpos, zb = L(*(id *)b)->zpos; return za < zb ? -1 : za > zb; }
static void apply_mask(id layer, struct cgctx *c) {
    id m = L(layer)->mask;
    if (!m) return;
    /* the mask layer lives in this layer's coordinate space */
    iosrt_CGContextSaveGState(c);
    gfx_mat mm = local_mat(m);
    iosrt_CGContextConcatCTM(c, mm);
    if (rt_is_kind_of(m, RT_CLASS(CAShapeLayer)) && SH(m)->path) {
        gfx_mat ctm = rt_cg_ctm(c);
        iosrt_CGContextRestoreGState(c);
        /* clip with the path mapped by the mask transform (the clip must survive the restore above) */
        gfx_mat rel = gfx_mat_mul(ctm, gfx_mat_invert(rt_cg_ctm(c)));
        id p = iosrt_CGPathCreateCopyByTransformingPath(SH(m)->path, &rel);
        iosrt_CGContextBeginPath(c); iosrt_CGContextAddPath(c, p); objc_release(p);
        iosrt_CGContextClip(c);
        return;
    }
    if (L(m)->contents && rt_cgimage_gfx(L(m)->contents)) {
        CGRect b = L(m)->bounds; gfx_mat ctm = rt_cg_ctm(c);
        iosrt_CGContextRestoreGState(c);
        struct gfx_image *g = rt_cgimage_gfx(L(m)->contents);
        gfx_mat im = { b.size.width / g->w, 0, 0, b.size.height / g->h, b.origin.x, b.origin.y };
        gfx_clip_image_mask(rt_cg_surface(c), g, gfx_mat_mul(im, ctm), 0);
        return;
    }
    if (L(m)->bg) {   /* plain layer mask: its bounds */
        CGRect b = L(m)->bounds; gfx_mat ctm = rt_cg_ctm(c);
        iosrt_CGContextRestoreGState(c);
        gfx_mat rel = gfx_mat_mul(ctm, gfx_mat_invert(rt_cg_ctm(c)));
        id p = iosrt_CGPathCreateWithRoundedRect(b, L(m)->corner, L(m)->corner, &rel);
        iosrt_CGContextBeginPath(c); iosrt_CGContextAddPath(c, p); objc_release(p); iosrt_CGContextClip(c);
        return;
    }
    iosrt_CGContextRestoreGState(c);
}
void rt_layer_render(id layer, struct cgctx *c) {
    struct calayer *l = L(layer);
    struct pres pr; presentation(layer, &pr);
    if (l->hidden || pr.opacity <= 0.001) return;
    iosrt_CGContextSaveGState(c);
    iosrt_CGContextConcatCTM(c, local_mat_with(l, pr.transform, pr.position));
    iosrt_CGContextSetAlpha(c, rt_cg_alpha(c) * pr.opacity);
    apply_mask(layer, c);
    CGRect b = l->bounds;
    if (l->masks) { iosrt_CGContextBeginPath(c); rounded_path(c, b, l->corner); iosrt_CGContextClip(c); }
    if (l->bg) {
        float col[4]; rt_color_rgba(l->bg, col);
        if (col[3] > 0 || ((struct uicolor *)l->bg)->pattern) {
            iosrt_CGContextSetFillColorWithColor(c, l->bg);
            iosrt_CGContextBeginPath(c); rounded_path(c, b, l->corner); iosrt_CGContextFillPath(c);
        }
    }
    if (l->contents) draw_contents(layer, c);
    if (l->delegate && rt_is_kind_of(l->delegate, RT_CLASS(UIView))) rt_view_draw(l->delegate, c, b);
    else {
        int found; IMP imp = rt_lookup(layer->isa, rt_sel("drawInContext:"), &found);
        if (found && imp != (IMP)ly_drawInContext) { iosrt_CGContextSaveGState(c); ((void (*)(id, SEL, struct cgctx *))imp)(layer, rt_sel("drawInContext:"), c); iosrt_CGContextRestoreGState(c); }
        else if (l->delegate && rt_responds(l->delegate, rt_sel("drawLayer:inContext:"))) { iosrt_CGContextSaveGState(c); ly_drawInContext(layer, NULL, c); iosrt_CGContextRestoreGState(c); }
    }
    if (rt_is_kind_of(layer, RT_CLASS(CAShapeLayer))) { iosrt_CGContextSaveGState(c); draw_shape(layer, c, &pr); iosrt_CGContextRestoreGState(c); }
    else if (rt_is_kind_of(layer, RT_CLASS(CAGradientLayer))) draw_gradient(layer, c);
    else if (rt_is_kind_of(layer, RT_CLASS(CATextLayer))) draw_textlayer(layer, c);
    size_t n = l->sublayers ? rt_array_count(l->sublayers) : 0;
    if (n) {
        id *subs = malloc(sizeof(id) * n);
        for (size_t i = 0; i < n; i++) subs[i] = objc_retain(rt_array_at(l->sublayers, i));
        /* stable sort by zPosition (insertion sort keeps equal elements in order) */
        for (size_t i = 1; i < n; i++) { id x = subs[i]; size_t j = i; while (j > 0 && cmp_z(&subs[j - 1], &x) > 0) { subs[j] = subs[j - 1]; j--; } subs[j] = x; }
        if (!iosrt_CATransform3DIsIdentity(l->sublayerTransform)) iosrt_CGContextConcatCTM(c, iosrt_CATransform3DGetAffineTransform(l->sublayerTransform));
        for (size_t i = 0; i < n; i++) { rt_layer_render(subs[i], c); objc_release(subs[i]); }
        free(subs);
    }
    if (l->borderWidth > 0 && l->borderColor) {
        iosrt_CGContextSetStrokeColorWithColor(c, l->borderColor); iosrt_CGContextSetLineWidth(c, l->borderWidth);
        double h = l->borderWidth / 2;
        iosrt_CGContextBeginPath(c);
        rounded_path(c, (CGRect){ { b.origin.x + h, b.origin.y + h }, { b.size.width - 2 * h, b.size.height - 2 * h } }, l->corner > h ? l->corner - h : 0);
        iosrt_CGContextStrokePath(c);
    }
    l->needsDisplay = 0;
    iosrt_CGContextRestoreGState(c);
}
void rt_layers_begin_frame(void) { g_animating = 0; }

/* ============================================================ CATransaction */
struct txn { void *completion; double max; };
static struct txn txns[32]; static int ntxn;
static double txn_duration = 0.25; static long txn_disable;
static void run_block(void *b) { ((void (*)(void *))((struct rt_block *)b)->invoke)(b); _Block_release(b); }
static void tx_begin(Class c, SEL s) { (void)c; (void)s; if (ntxn < 32) { txns[ntxn].completion = NULL; txns[ntxn].max = g_txn_max; ntxn++; g_txn_max = 0; } }
static void tx_commit(Class c, SEL s) {
    (void)c; (void)s; if (!ntxn) return;
    struct txn *t = &txns[--ntxn];
    double d = g_txn_max;
    if (t->completion) iosrt_dispatch_after_f(iosrt_dispatch_time(0, (int64_t)(d * 1e9)), iosrt_dispatch_main_q, t->completion, run_block);
    g_txn_max = fmax(t->max, d);
}
static void tx_setCompletion(Class c, SEL s, void *blk) {
    (void)c; (void)s;
    void *copy = blk ? _Block_copy(blk) : NULL;
    if (ntxn) { if (txns[ntxn - 1].completion) _Block_release(txns[ntxn - 1].completion); txns[ntxn - 1].completion = copy; }
    else if (copy) iosrt_dispatch_after_f(iosrt_dispatch_time(0, 0), iosrt_dispatch_main_q, copy, run_block);   /* implicit transaction */
}
static void *tx_completion(Class c, SEL s) { (void)c; (void)s; return ntxn ? txns[ntxn - 1].completion : NULL; }
static void tx_setDuration(Class c, SEL s, double d) { (void)c; (void)s; txn_duration = d; }
static double tx_duration(Class c, SEL s) { (void)c; (void)s; return txn_duration; }
static void tx_setDisable(Class c, SEL s, long v) { (void)c; (void)s; txn_disable = v & 0xff; }
static rt_bool tx_disable(Class c, SEL s) { (void)c; (void)s; return txn_disable != 0; }
static void tx_noop(Class c, SEL s) { (void)c; (void)s; }
static void tx_noop1(Class c, SEL s, id v) { (void)c; (void)s; (void)v; }
static id tx_nil(Class c, SEL s, id k) { (void)c; (void)s; (void)k; return NULL; }
static void tx_setValue(Class c, SEL s, id v, id k) { (void)c; (void)s; (void)v; (void)k; }

/* ============================================================ CADisplayLink */
struct cadl { Class isa; id target; SEL sel; long paused, valid, interval, fps; double ts, last; unsigned long frames; long scheduled; };
static id links[64]; static int nlinks;
static id dl_make(Class c, SEL s, id target, SEL sel) {
    (void)s; id d = rt_alloc_instance(c, 0); struct cadl *x = (struct cadl *)d;
    x->target = objc_retain(target); x->sel = sel; x->valid = 1; x->interval = 1;
    return objc_autorelease(d);
}
static void dl_add(id self, SEL s, id rl, id mode) {
    (void)s; (void)rl; (void)mode; struct cadl *x = (struct cadl *)self;
    if (x->scheduled || !x->valid || nlinks >= 64) return;
    x->scheduled = 1; links[nlinks++] = objc_retain(self);
}
static void dl_remove(id self, SEL s, id rl, id mode) {
    (void)s; (void)rl; (void)mode;
    for (int i = 0; i < nlinks; i++) if (links[i] == self) { links[i] = links[--nlinks]; ((struct cadl *)self)->scheduled = 0; objc_release(self); break; }
}
static void dl_invalidate(id self, SEL s) { struct cadl *x = (struct cadl *)self; x->valid = 0; objc_release(x->target); x->target = NULL; dl_remove(self, s, NULL, NULL); }
static rt_bool dl_paused(id self, SEL s) { (void)s; return ((struct cadl *)self)->paused != 0; }
static void dl_setPaused(id self, SEL s, long v) { (void)s; ((struct cadl *)self)->paused = v & 0xff; }
static double dl_ts(id self, SEL s) { (void)s; return ((struct cadl *)self)->ts; }
static double dl_duration(id self, SEL s) { (void)self; (void)s; return 1.0 / 60; }
static double dl_target(id self, SEL s) { (void)s; return ((struct cadl *)self)->ts + 1.0 / 60; }
static long dl_interval(id self, SEL s) { (void)s; return ((struct cadl *)self)->interval; }
static void dl_setInterval(id self, SEL s, long v) { (void)s; ((struct cadl *)self)->interval = v > 0 ? v : 1; }
static long dl_fps(id self, SEL s) { (void)s; return ((struct cadl *)self)->fps; }
static void dl_setFps(id self, SEL s, long v) { (void)s; struct cadl *x = (struct cadl *)self; x->fps = v; x->interval = v > 0 && v < 60 ? 60 / v : 1; }
int rt_display_links_active(void) { for (int i = 0; i < nlinks; i++) if (!((struct cadl *)links[i])->paused) return 1; return g_animating; }
void rt_display_links_tick(void) {
    double now = iosrt_CACurrentMediaTime();
    int n = nlinks; id snap[64];
    for (int i = 0; i < n; i++) snap[i] = objc_retain(links[i]);
    for (int i = 0; i < n; i++) {
        struct cadl *x = (struct cadl *)snap[i];
        if (x->valid && !x->paused && x->target && (++x->frames % (unsigned long)x->interval) == 0) {
            x->ts = now;
            ((void (*)(id, SEL, id))objc_msgSend)(x->target, x->sel, snap[i]);
        }
        objc_release(snap[i]);
    }
}

/* NSValue CATransform3D */
static id v_withT3(Class c, SEL s, CATransform3D t) { (void)c; (void)s; return val_t3(t); }
static CATransform3D v_t3(id self, SEL s) { (void)s; return t3_of(self); }

id iosrt_kCAFillModeForwards, iosrt_kCAFillModeBackwards, iosrt_kCAFillModeBoth, iosrt_kCAFillModeRemoved,
   iosrt_kCAMediaTimingFunctionLinear, iosrt_kCAMediaTimingFunctionEaseIn, iosrt_kCAMediaTimingFunctionEaseOut,
   iosrt_kCAMediaTimingFunctionEaseInEaseOut, iosrt_kCAMediaTimingFunctionDefault,
   iosrt_kCATransitionFade, iosrt_kCATransitionMoveIn, iosrt_kCATransitionPush, iosrt_kCATransitionReveal,
   iosrt_kCATransitionFromRight, iosrt_kCATransitionFromLeft, iosrt_kCATransitionFromTop, iosrt_kCATransitionFromBottom,
   iosrt_kCAGravityCenter, iosrt_kCAGravityTop, iosrt_kCAGravityBottom, iosrt_kCAGravityLeft, iosrt_kCAGravityRight,
   iosrt_kCAGravityTopLeft, iosrt_kCAGravityTopRight, iosrt_kCAGravityBottomLeft, iosrt_kCAGravityBottomRight,
   iosrt_kCAGravityResize, iosrt_kCAGravityResizeAspect, iosrt_kCAGravityResizeAspectFill,
   iosrt_kCALineCapButt, iosrt_kCALineCapRound, iosrt_kCALineCapSquare, iosrt_kCALineJoinMiter, iosrt_kCALineJoinRound, iosrt_kCALineJoinBevel,
   iosrt_kCAFillRuleNonZero, iosrt_kCAFillRuleEvenOdd, iosrt_kCAAlignmentNatural, iosrt_kCAAlignmentLeft, iosrt_kCAAlignmentRight,
   iosrt_kCAAlignmentCenter, iosrt_kCAAlignmentJustified, iosrt_kCAOnOrderIn, iosrt_kCAOnOrderOut, iosrt_kCATransition,
   iosrt_kCAAnimationLinear, iosrt_kCAAnimationDiscrete, iosrt_kCAAnimationPaced, iosrt_kCAAnimationCubic, iosrt_kCAAnimationCubicPaced,
   iosrt_kCAGradientLayerAxial, iosrt_kCAGradientLayerRadial, iosrt_kCATruncationEnd, iosrt_kCATruncationNone, iosrt_kCATruncationMiddle, iosrt_kCATruncationStart,
   iosrt_kCAFilterLinear, iosrt_kCAFilterNearest, iosrt_kCAFilterTrilinear;

void rt_layers_init(void) {
    Class o = RT_CLASS(NSObject);
    NEWCLASS(CALayer, o, sizeof(struct calayer) - sizeof(void *));
    Class c = RT_CLASS(CALayer);
    CM(c, "layer", ly_layer); IM(c, "init", ly_init); IM(c, "dealloc", ly_dealloc);
    IM(c, "bounds", ly_bounds); IM(c, "setBounds:", ly_setBounds); IM(c, "position", ly_position); IM(c, "setPosition:", ly_setPosition);
    IM(c, "anchorPoint", ly_anchor); IM(c, "setAnchorPoint:", ly_setAnchor); IM(c, "zPosition", ly_zpos); IM(c, "setZPosition:", ly_setZpos);
    IM(c, "frame", ly_frame); IM(c, "setFrame:", ly_setFrame); IM(c, "transform", ly_transform); IM(c, "setTransform:", ly_setTransform);
    IM(c, "affineTransform", ly_affine); IM(c, "setAffineTransform:", ly_setAffine); IM(c, "sublayerTransform", ly_subT); IM(c, "setSublayerTransform:", ly_setSubT);
    IM(c, "sublayers", ly_sublayers); IM(c, "setSublayers:", ly_setSublayers); IM(c, "superlayer", ly_superlayer);
    IM(c, "addSublayer:", ly_addSublayer); IM(c, "insertSublayer:atIndex:", ly_insertAt); IM(c, "insertSublayer:below:", ly_insertBelow);
    IM(c, "insertSublayer:above:", ly_insertAbove); IM(c, "replaceSublayer:with:", ly_replace); IM(c, "removeFromSuperlayer", ly_removeFromSuperlayer);
    IM(c, "delegate", ly_delegate); IM(c, "setDelegate:", ly_setDelegate);
    IM(c, "backgroundColor", ly_bg); IM(c, "setBackgroundColor:", ly_setBg); IM(c, "cornerRadius", ly_corner); IM(c, "setCornerRadius:", ly_setCorner);
    IM(c, "borderWidth", ly_bw); IM(c, "setBorderWidth:", ly_setBw); IM(c, "borderColor", ly_bc); IM(c, "setBorderColor:", ly_setBc);
    IM(c, "opacity", ly_opacity); IM(c, "setOpacity:", ly_setOpacity); IM(c, "isHidden", ly_hidden); IM(c, "setHidden:", ly_setHidden);
    IM(c, "masksToBounds", ly_masks); IM(c, "setMasksToBounds:", ly_setMasks); IM(c, "contents", ly_contents); IM(c, "setContents:", ly_setContents);
    IM(c, "contentsRect", ly_contentsRect); IM(c, "setContentsRect:", ly_setContentsRect); IM(c, "contentsCenter", ly_contentsCenter); IM(c, "setContentsCenter:", ly_setContentsCenter);
    IM(c, "contentsGravity", ly_gravity); IM(c, "setContentsGravity:", ly_setGravity); IM(c, "contentsScale", ly_cscale); IM(c, "setContentsScale:", ly_setCscale);
    IM(c, "mask", ly_mask); IM(c, "setMask:", ly_setMask); IM(c, "name", ly_name); IM(c, "setName:", ly_setName);
    IM(c, "shadowColor", ly_shadowColor); IM(c, "setShadowColor:", ly_setShadowColor); IM(c, "shadowOpacity", ly_shadowOpacity); IM(c, "setShadowOpacity:", ly_setShadowOpacity);
    IM(c, "shadowRadius", ly_shadowRadius); IM(c, "setShadowRadius:", ly_setShadowRadius); IM(c, "shadowOffset", ly_shadowOffset); IM(c, "setShadowOffset:", ly_setShadowOffset);
    IM(c, "shadowPath", ly_shadowPath); IM(c, "setShadowPath:", ly_setShadowPath); IM(c, "speed", ly_speed); IM(c, "setSpeed:", ly_setSpeed);
    IM(c, "actions", ly_actions); IM(c, "setActions:", ly_setActions);
    IM(c, "needsDisplay", ly_needsDisplay); IM(c, "setNeedsDisplay", ly_setNeedsDisplay); IM(c, "setNeedsDisplayInRect:", ly_setNeedsDisplayInRect);
    IM(c, "displayIfNeeded", ly_noop); IM(c, "display", ly_display); IM(c, "drawInContext:", ly_drawInContext); IM(c, "renderInContext:", ly_renderInContext);
    IM(c, "setNeedsLayout", ly_setNeedsLayout); IM(c, "needsLayout", ly_needsLayout); IM(c, "layoutIfNeeded", ly_layoutIfNeeded); IM(c, "layoutSublayers", ly_layoutSublayers);
    IM(c, "preferredFrameSize", ly_preferredFrameSize);
    IM(c, "convertPoint:toLayer:", ly_convertPointTo); IM(c, "convertPoint:fromLayer:", ly_convertPointFrom);
    IM(c, "convertRect:toLayer:", ly_convertRectTo); IM(c, "convertRect:fromLayer:", ly_convertRectFrom);
    IM(c, "hitTest:", ly_hitTest); IM(c, "containsPoint:", ly_contains);
    IM(c, "addAnimation:forKey:", ly_addAnimation); IM(c, "removeAnimationForKey:", ly_removeAnimation); IM(c, "removeAllAnimations", ly_removeAll);
    IM(c, "animationForKey:", ly_animationForKey); IM(c, "animationKeys", ly_animationKeys);
    IM(c, "presentationLayer", ly_presentationLayer); IM(c, "modelLayer", ly_self);
    IM(c, "valueForKey:", ly_valueForKey); IM(c, "setValue:forKey:", ly_setValueForKey);
    IM(c, "valueForKeyPath:", ly_valueForKey); IM(c, "setValue:forKeyPath:", ly_setValueForKey);
    IM(c, "setShouldRasterize:", ly_noopL); IM(c, "shouldRasterize", ly_bool0); IM(c, "setRasterizationScale:", ly_noopD); IM(c, "rasterizationScale", ly_one);
    IM(c, "setOpaque:", ly_noopL); IM(c, "isOpaque", ly_bool0); IM(c, "setGeometryFlipped:", ly_noopL); IM(c, "isGeometryFlipped", ly_bool0);
    IM(c, "setDoubleSided:", ly_noopL); IM(c, "setAllowsEdgeAntialiasing:", ly_noopL); IM(c, "setAllowsGroupOpacity:", ly_noopL);
    IM(c, "setEdgeAntialiasingMask:", ly_noopL); IM(c, "setNeedsDisplayOnBoundsChange:", ly_noopL); IM(c, "setDrawsAsynchronously:", ly_noopL);
    IM(c, "setMinificationFilter:", ly_noop1); IM(c, "setMagnificationFilter:", ly_noop1); IM(c, "setStyle:", ly_noop1); IM(c, "setCompositingFilter:", ly_noop1);
    IM(c, "setFilters:", ly_noop1); IM(c, "setBackgroundFilters:", ly_noop1); IM(c, "actionForKey:", ly_nil1); IM(c, "contentsAreFlipped", ly_bool0);
    IM(c, "setBeginTime:", ly_noopD); IM(c, "setTimeOffset:", ly_noopD); IM(c, "beginTime", ly_nil); IM(c, "convertTime:fromLayer:", ly_nil1);

    NEWCLASS(CAShapeLayer, c, sizeof(struct cashape) - sizeof(struct calayer));
    Class sh = RT_CLASS(CAShapeLayer);
    IM(sh, "path", sh_path); IM(sh, "setPath:", sh_setPath); IM(sh, "fillColor", sh_fill); IM(sh, "setFillColor:", sh_setFill);
    IM(sh, "strokeColor", sh_stroke); IM(sh, "setStrokeColor:", sh_setStroke); IM(sh, "lineWidth", sh_lw); IM(sh, "setLineWidth:", sh_setLw);
    IM(sh, "miterLimit", sh_miter); IM(sh, "setMiterLimit:", sh_setMiter); IM(sh, "lineCap", sh_cap); IM(sh, "setLineCap:", sh_setCap);
    IM(sh, "lineJoin", sh_join); IM(sh, "setLineJoin:", sh_setJoin); IM(sh, "lineDashPattern", sh_dash); IM(sh, "setLineDashPattern:", sh_setDash);
    IM(sh, "lineDashPhase", sh_dashPhase); IM(sh, "setLineDashPhase:", sh_setDashPhase); IM(sh, "strokeStart", sh_strokeStart); IM(sh, "setStrokeStart:", sh_setStrokeStart);
    IM(sh, "strokeEnd", sh_strokeEnd); IM(sh, "setStrokeEnd:", sh_setStrokeEnd); IM(sh, "fillRule", sh_rule); IM(sh, "setFillRule:", sh_setRule);
    NEWCLASS(CAGradientLayer, c, sizeof(struct cagrad) - sizeof(struct calayer));
    Class gr = RT_CLASS(CAGradientLayer);
    IM(gr, "colors", gr_colors); IM(gr, "setColors:", gr_setColors); IM(gr, "locations", gr_locations); IM(gr, "setLocations:", gr_setLocations);
    IM(gr, "startPoint", gr_start); IM(gr, "setStartPoint:", gr_setStart); IM(gr, "endPoint", gr_end); IM(gr, "setEndPoint:", gr_setEnd);
    IM(gr, "type", gr_type); IM(gr, "setType:", gr_setType);
    NEWCLASS(CATextLayer, c, sizeof(struct catext) - sizeof(struct calayer));
    Class tx = RT_CLASS(CATextLayer);
    IM(tx, "string", tx_string); IM(tx, "setString:", tx_setString); IM(tx, "font", tx_font); IM(tx, "setFont:", tx_setFont);
    IM(tx, "fontSize", tx_fontSize); IM(tx, "setFontSize:", tx_setFontSize); IM(tx, "foregroundColor", tx_fg); IM(tx, "setForegroundColor:", tx_setFg);
    IM(tx, "alignmentMode", tx_align); IM(tx, "setAlignmentMode:", tx_setAlign); IM(tx, "isWrapped", tx_wraps); IM(tx, "setWrapped:", tx_setWraps);
    IM(tx, "setTruncationMode:", tx_setTrunc);
    NEWCLASS(CAScrollLayer, c, 0); NEWCLASS(CAReplicatorLayer, c, 0); NEWCLASS(CAEAGLLayer, c, 0); NEWCLASS(CATransformLayer, c, 0);
    IM(RT_CLASS(CAEAGLLayer), "setDrawableProperties:", ly_noop1);

    NEWCLASS(CAAnimation, o, sizeof(struct caanim) - sizeof(void *));
    Class an = RT_CLASS(CAAnimation);
    CM(an, "animation", an_animation); IM(an, "init", an_init); IM(an, "dealloc", an_dealloc); IM(an, "copyWithZone:", an_copy); IM(an, "copy", an_copy);
    IM(an, "duration", an_duration); IM(an, "setDuration:", an_setDuration); IM(an, "beginTime", an_beginTime); IM(an, "setBeginTime:", an_setBeginTime);
    IM(an, "timeOffset", an_timeOffset); IM(an, "setTimeOffset:", an_setTimeOffset); IM(an, "repeatCount", an_repeatCount); IM(an, "setRepeatCount:", an_setRepeatCount);
    IM(an, "repeatDuration", an_repeatDuration); IM(an, "setRepeatDuration:", an_setRepeatDuration); IM(an, "speed", an_speed); IM(an, "setSpeed:", an_setSpeed);
    IM(an, "autoreverses", an_autoreverses); IM(an, "setAutoreverses:", an_setAutoreverses); IM(an, "isRemovedOnCompletion", an_removed);
    IM(an, "setRemovedOnCompletion:", an_setRemoved); IM(an, "fillMode", an_fillMode); IM(an, "setFillMode:", an_setFillMode);
    IM(an, "timingFunction", an_timing); IM(an, "setTimingFunction:", an_setTiming); IM(an, "delegate", an_delegate); IM(an, "setDelegate:", an_setDelegate);
    NEWCLASS(CAPropertyAnimation, an, 0);
    Class pa = RT_CLASS(CAPropertyAnimation);
    CM(pa, "animationWithKeyPath:", an_withKeyPath); IM(pa, "keyPath", an_keyPath); IM(pa, "setKeyPath:", an_setKeyPath);
    IM(pa, "isAdditive", an_additive); IM(pa, "setAdditive:", an_setAdditive); IM(pa, "isCumulative", an_cumulative); IM(pa, "setCumulative:", an_setCumulative);
    NEWCLASS(CABasicAnimation, pa, 0);
    Class ba = RT_CLASS(CABasicAnimation);
    IM(ba, "fromValue", an_from); IM(ba, "setFromValue:", an_setFrom); IM(ba, "toValue", an_to); IM(ba, "setToValue:", an_setTo);
    IM(ba, "byValue", an_by); IM(ba, "setByValue:", an_setBy);
    NEWCLASS(CASpringAnimation, ba, 0);
    IM(RT_CLASS(CASpringAnimation), "setDamping:", ly_noopD); IM(RT_CLASS(CASpringAnimation), "setStiffness:", ly_noopD);
    IM(RT_CLASS(CASpringAnimation), "setMass:", ly_noopD); IM(RT_CLASS(CASpringAnimation), "setInitialVelocity:", ly_noopD);
    NEWCLASS(CAKeyframeAnimation, pa, 0);
    Class ka = RT_CLASS(CAKeyframeAnimation);
    IM(ka, "values", an_values); IM(ka, "setValues:", an_setValues); IM(ka, "keyTimes", an_keyTimes); IM(ka, "setKeyTimes:", an_setKeyTimes);
    IM(ka, "path", an_path); IM(ka, "setPath:", an_setPath); IM(ka, "calculationMode", an_calc); IM(ka, "setCalculationMode:", an_setCalc);
    IM(ka, "setTimingFunctions:", ly_noop1); IM(ka, "setRotationMode:", ly_noop1);
    NEWCLASS(CAAnimationGroup, an, 0);
    IM(RT_CLASS(CAAnimationGroup), "animations", an_animations); IM(RT_CLASS(CAAnimationGroup), "setAnimations:", an_setAnimations);
    NEWCLASS(CATransition, an, 0);
    Class tr = RT_CLASS(CATransition);
    IM(tr, "type", an_type); IM(tr, "setType:", an_setType); IM(tr, "subtype", an_subtype); IM(tr, "setSubtype:", an_setSubtype);
    IM(tr, "setStartProgress:", ly_noopD); IM(tr, "setEndProgress:", ly_noopD);

    NEWCLASS(CAMediaTimingFunction, o, sizeof(struct catf) - sizeof(void *));
    Class tf = RT_CLASS(CAMediaTimingFunction);
    CM(tf, "functionWithName:", tf_withName); CM(tf, "functionWithControlPoints::::", tf_withPoints);
    IM(tf, "initWithControlPoints::::", tf_initPoints); IM(tf, "getControlPointAtIndex:values:", tf_getPoint);

    NEWCLASS(CATransaction, o, 0);
    Class t = RT_CLASS(CATransaction);
    CM(t, "begin", tx_begin); CM(t, "commit", tx_commit); CM(t, "flush", tx_noop); CM(t, "lock", tx_noop); CM(t, "unlock", tx_noop);
    CM(t, "setCompletionBlock:", tx_setCompletion); CM(t, "completionBlock", tx_completion);
    CM(t, "setAnimationDuration:", tx_setDuration); CM(t, "animationDuration", tx_duration);
    CM(t, "setDisableActions:", tx_setDisable); CM(t, "disableActions", tx_disable);
    CM(t, "setAnimationTimingFunction:", tx_noop1); CM(t, "valueForKey:", tx_nil); CM(t, "setValue:forKey:", tx_setValue);

    NEWCLASS(CADisplayLink, o, sizeof(struct cadl) - sizeof(void *));
    Class dl = RT_CLASS(CADisplayLink);
    CM(dl, "displayLinkWithTarget:selector:", dl_make);
    IM(dl, "addToRunLoop:forMode:", dl_add); IM(dl, "removeFromRunLoop:forMode:", dl_remove); IM(dl, "invalidate", dl_invalidate);
    IM(dl, "isPaused", dl_paused); IM(dl, "setPaused:", dl_setPaused); IM(dl, "timestamp", dl_ts); IM(dl, "duration", dl_duration);
    IM(dl, "targetTimestamp", dl_target); IM(dl, "frameInterval", dl_interval); IM(dl, "setFrameInterval:", dl_setInterval);
    IM(dl, "preferredFramesPerSecond", dl_fps); IM(dl, "setPreferredFramesPerSecond:", dl_setFps);

    CM(RT_CLASS(NSValue), "valueWithCATransform3D:", v_withT3); IM(RT_CLASS(NSValue), "CATransform3DValue", v_t3);

#define K(var, str) iosrt_##var = rt_str_new(str);
    K(kCAFillModeForwards, "forwards") K(kCAFillModeBackwards, "backwards") K(kCAFillModeBoth, "both") K(kCAFillModeRemoved, "removed")
    K(kCAMediaTimingFunctionLinear, "linear") K(kCAMediaTimingFunctionEaseIn, "easeIn") K(kCAMediaTimingFunctionEaseOut, "easeOut")
    K(kCAMediaTimingFunctionEaseInEaseOut, "easeInEaseOut") K(kCAMediaTimingFunctionDefault, "default")
    K(kCATransitionFade, "fade") K(kCATransitionMoveIn, "moveIn") K(kCATransitionPush, "push") K(kCATransitionReveal, "reveal")
    K(kCATransitionFromRight, "fromRight") K(kCATransitionFromLeft, "fromLeft") K(kCATransitionFromTop, "fromTop") K(kCATransitionFromBottom, "fromBottom")
    K(kCAGravityCenter, "center") K(kCAGravityTop, "top") K(kCAGravityBottom, "bottom") K(kCAGravityLeft, "left") K(kCAGravityRight, "right")
    K(kCAGravityTopLeft, "topLeft") K(kCAGravityTopRight, "topRight") K(kCAGravityBottomLeft, "bottomLeft") K(kCAGravityBottomRight, "bottomRight")
    K(kCAGravityResize, "resize") K(kCAGravityResizeAspect, "resizeAspect") K(kCAGravityResizeAspectFill, "resizeAspectFill")
    K(kCALineCapButt, "butt") K(kCALineCapRound, "round") K(kCALineCapSquare, "square") K(kCALineJoinMiter, "miter") K(kCALineJoinRound, "round") K(kCALineJoinBevel, "bevel")
    K(kCAFillRuleNonZero, "non-zero") K(kCAFillRuleEvenOdd, "even-odd") K(kCAAlignmentNatural, "natural") K(kCAAlignmentLeft, "left")
    K(kCAAlignmentRight, "right") K(kCAAlignmentCenter, "center") K(kCAAlignmentJustified, "justified") K(kCAOnOrderIn, "onOrderIn")
    K(kCAOnOrderOut, "onOrderOut") K(kCATransition, "transition") K(kCAAnimationLinear, "linear") K(kCAAnimationDiscrete, "discrete")
    K(kCAAnimationPaced, "paced") K(kCAAnimationCubic, "cubic") K(kCAAnimationCubicPaced, "cubicPaced") K(kCAGradientLayerAxial, "axial")
    K(kCAGradientLayerRadial, "radial") K(kCATruncationEnd, "end") K(kCATruncationNone, "none") K(kCATruncationMiddle, "middle") K(kCATruncationStart, "start")
    K(kCAFilterLinear, "linear") K(kCAFilterNearest, "nearest") K(kCAFilterTrilinear, "trilinear")
#undef K
}
gfx_mat rt_layer_local(id layer) { return local_mat(layer); }
