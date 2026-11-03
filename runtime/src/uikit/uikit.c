/*
 * UIKit core: UIColor, UIResponder, layer-backed UIView (geometry, hierarchy,
 * layout, autoresizing, block animations), UIWindow, UIScreen, UIDevice,
 * UIApplication, UITouch/UIEvent, gesture recognizers and the touch
 * dispatcher, and the app main loop that lays out, renders every window's
 * layer tree with the software rasterizer and presents the frame.
 *
 * Headless mode (tests): IOSRT_HEADLESS_FRAMES=n runs n frames, IOSRT_SCRIPT
 * injects touches, the last frame's draw trace is printed (golden output),
 * IOSRT_DUMP_FRAMES prints every changed frame, IOSRT_PNG_OUT=file.png saves
 * the last presented frame.
 */
#include "uikit_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

RT_DECLARE_CLASS(NSObject);
RT_DECLARE_CLASS(NSMutableArray);
RT_DECLARE_CLASS(NSSet);
RT_DECLARE_CLASS(CALayer);
RT_DEFINE_CLASS(UIResponder);
RT_DEFINE_CLASS(UIApplication);
RT_DEFINE_CLASS(UIScreen);
RT_DEFINE_CLASS(UIDevice);
RT_DEFINE_CLASS(UIView);
RT_DEFINE_CLASS(UIWindow);
RT_DEFINE_CLASS(UIColor);
RT_DEFINE_CLASS(UITouch);
RT_DEFINE_CLASS(UIEvent);
RT_DEFINE_CLASS(UIUserNotificationSettings);
RT_DEFINE_CLASS(UIGestureRecognizer);
RT_DEFINE_CLASS(UITapGestureRecognizer);
RT_DEFINE_CLASS(UILongPressGestureRecognizer);
RT_DEFINE_CLASS(UIPanGestureRecognizer);
RT_DEFINE_CLASS(UIScreenEdgePanGestureRecognizer);
RT_DEFINE_CLASS(UISwipeGestureRecognizer);
RT_DEFINE_CLASS(UIPinchGestureRecognizer);
RT_DEFINE_CLASS(UIRotationGestureRecognizer);
RT_DECLARE_CLASS(UIControl);
RT_DECLARE_CLASS(UIScrollView);
RT_DECLARE_CLASS(UILabel);

id rt_set_new(id *objs, size_t n);
int rt_runloop_run_once(int max_wait_ms);
void iosrt_dispatch_after_f(uint64_t when, void *q, void *ctx, void (*fn)(void *));
uint64_t iosrt_dispatch_time(uint64_t when, int64_t delta);
extern char iosrt_dispatch_main_q[];
double iosrt_CACurrentMediaTime(void);
void rt_cg_init(void);
void rt_uiimage_init(void);
void rt_text_init(void);
volatile int rt_quit_requested;

/* ------------------------------------------------------------ screen */
float rt_scr_w = 375, rt_scr_h = 667, rt_scr_scale = 2;
static pthread_mutex_t input_lock = PTHREAD_MUTEX_INITIALIZER;
struct touch_ev { int phase; float x, y; int tid; };
static struct touch_ev touchq[512]; static int tq_head, tq_tail;
void iosrt_set_screen(float w, float h, float scale) { if (w > 0) rt_scr_w = w; if (h > 0) rt_scr_h = h; if (scale > 0) rt_scr_scale = scale; }
void iosrt_push_touch_id(int tid, int phase, float x, float y) {
    pthread_mutex_lock(&input_lock);
    int n = (tq_tail + 1) % 512;
    if (n != tq_head) { touchq[tq_tail] = (struct touch_ev){ phase, x, y, tid }; tq_tail = n; }
    pthread_mutex_unlock(&input_lock);
}
void iosrt_push_touch(int phase, float x, float y) { iosrt_push_touch_id(0, phase, x, y); }
/* touches given in pixels of the presented frame */
void rt_push_touch_px(int tid, int phase, float x, float y) { iosrt_push_touch_id(tid, phase, x / rt_scr_scale, y / rt_scr_scale); }
void iosrt_request_quit(void) { rt_quit_requested = 1; }
static volatile int dirty = 1;
void rt_ui_mark_dirty(void) { dirty = 1; }
double rt_now(void) { return iosrt_CACurrentMediaTime(); }
void rt_after(double delay, void (*fn)(void *), void *ctx) { iosrt_dispatch_after_f(iosrt_dispatch_time(0, (int64_t)(delay * 1e9)), iosrt_dispatch_main_q, ctx, fn); }
static id mkarray(void) { return RT_SEND0(id, RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "alloc"), "init"); }

/* ============================================================== UIColor */
id rt_color_make(double r, double g, double b, double a) {
    id c = rt_alloc_instance(RT_CLASS(UIColor), 0); struct uicolor *u = (struct uicolor *)c;
    u->r = r; u->g = g; u->b = b; u->a = a; return objc_autorelease(c);
}
void rt_color_rgba(id color, float out[4]) {
    struct uicolor *u = (struct uicolor *)color;
    if (!u) { out[0] = out[1] = out[2] = out[3] = 0; return; }
    out[0] = (float)u->r; out[1] = (float)u->g; out[2] = (float)u->b; out[3] = (float)u->a;
}
static void c_dealloc(id self, SEL s) { (void)s; objc_release(((struct uicolor *)self)->pattern); rt_dealloc_instance(self); }
static id c_rgba(Class c, SEL s, double r, double g, double b, double a) { (void)c; (void)s; return rt_color_make(r, g, b, a); }
static id c_white(Class c, SEL s, double w, double a) { (void)c; (void)s; return rt_color_make(w, w, w, a); }
static void hsb_to_rgb(double h, double s, double v, double *r, double *g, double *b) {
    h = fmod(h, 1.0) * 6; if (h < 0) h += 6;
    int i = (int)floor(h); double f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    switch (i) { case 0: *r = v; *g = t; *b = p; break; case 1: *r = q; *g = v; *b = p; break; case 2: *r = p; *g = v; *b = t; break;
                 case 3: *r = p; *g = q; *b = v; break; case 4: *r = t; *g = p; *b = v; break; default: *r = v; *g = p; *b = q; }
}
static id c_hsb(Class c, SEL s, double h, double sa, double br, double a) { (void)c; (void)s; double r, g, b; hsb_to_rgb(h, sa, br, &r, &g, &b); return rt_color_make(r, g, b, a); }
static id c_init_rgba(id self, SEL s, double r, double g, double b, double a) { (void)s; struct uicolor *u = (struct uicolor *)self; u->r = r; u->g = g; u->b = b; u->a = a; return self; }
static id c_init_white(id self, SEL s, double w, double a) { return c_init_rgba(self, s, w, w, w, a); }
static id c_init_hsb(id self, SEL s, double h, double sa, double br, double a) { double r, g, b; hsb_to_rgb(h, sa, br, &r, &g, &b); return c_init_rgba(self, s, r, g, b, a); }
static id c_withCG(Class c, SEL s, id cg) { (void)c; (void)s; return cg; }   /* CGColorRef is a UIColor */
static id c_initWithCG(id self, SEL s, id cg) { (void)s; float v[4]; rt_color_rgba(cg, v); return c_init_rgba(self, NULL, v[0], v[1], v[2], v[3]); }
static id c_pattern(Class c, SEL s, id img) { (void)c; (void)s; id r = rt_color_make(0, 0, 0, 1); ((struct uicolor *)r)->pattern = objc_retain(img); return r; }
static id c_initPattern(id self, SEL s, id img) { (void)s; struct uicolor *u = (struct uicolor *)self; u->a = 1; u->pattern = objc_retain(img); return self; }
#define NAMED_COLOR(fn, r, g, b, a) static id fn(Class c, SEL s) { (void)c; (void)s; return rt_color_make(r, g, b, a); }
NAMED_COLOR(c_whiteColor, 1, 1, 1, 1) NAMED_COLOR(c_blackColor, 0, 0, 0, 1) NAMED_COLOR(c_redColor, 1, 0, 0, 1)
NAMED_COLOR(c_greenColor, 0, 1, 0, 1) NAMED_COLOR(c_blueColor, 0, 0, 1, 1) NAMED_COLOR(c_yellowColor, 1, 1, 0, 1)
NAMED_COLOR(c_orangeColor, 1, 0.5, 0, 1) NAMED_COLOR(c_purpleColor, 0.5, 0, 0.5, 1) NAMED_COLOR(c_grayColor, 0.5, 0.5, 0.5, 1)
NAMED_COLOR(c_darkGrayColor, 1.0 / 3, 1.0 / 3, 1.0 / 3, 1) NAMED_COLOR(c_lightGrayColor, 2.0 / 3, 2.0 / 3, 2.0 / 3, 1)
NAMED_COLOR(c_clearColor, 0, 0, 0, 0) NAMED_COLOR(c_cyanColor, 0, 1, 1, 1) NAMED_COLOR(c_magentaColor, 1, 0, 1, 1)
NAMED_COLOR(c_brownColor, 0.6, 0.4, 0.2, 1) NAMED_COLOR(c_systemBlue, 0, 0.478, 1, 1) NAMED_COLOR(c_systemRed, 1, 0.231, 0.188, 1)
NAMED_COLOR(c_systemGreen, 0.204, 0.78, 0.349, 1) NAMED_COLOR(c_groupTable, 0.937, 0.937, 0.957, 1) NAMED_COLOR(c_lightText, 1, 1, 1, 0.6)
NAMED_COLOR(c_darkText, 0, 0, 0, 1) NAMED_COLOR(c_label, 0, 0, 0, 1) NAMED_COLOR(c_secondaryLabel, 0.235, 0.235, 0.263, 0.6)
NAMED_COLOR(c_systemGray, 0.557, 0.557, 0.576, 1) NAMED_COLOR(c_separator, 0.235, 0.235, 0.263, 0.29)
static id c_withAlpha(id self, SEL s, double a) { (void)s; struct uicolor *u = (struct uicolor *)self; id r = rt_color_make(u->r, u->g, u->b, a); ((struct uicolor *)r)->pattern = objc_retain(u->pattern); return r; }
static rt_bool c_getRGBA(id self, SEL s, double *r, double *g, double *b, double *a) {
    (void)s; struct uicolor *u = (struct uicolor *)self;
    if (r) *r = u->r; if (g) *g = u->g; if (b) *b = u->b; if (a) *a = u->a; return 1;
}
static rt_bool c_getWhite(id self, SEL s, double *w, double *a) { (void)s; struct uicolor *u = (struct uicolor *)self; if (w) *w = (u->r + u->g + u->b) / 3; if (a) *a = u->a; return 1; }
static rt_bool c_getHSB(id self, SEL s, double *h, double *sa, double *br, double *a) {
    (void)s; struct uicolor *u = (struct uicolor *)self;
    double mx = fmax(u->r, fmax(u->g, u->b)), mn = fmin(u->r, fmin(u->g, u->b)), d = mx - mn, hh = 0;
    if (d > 0) { if (mx == u->r) hh = fmod((u->g - u->b) / d, 6); else if (mx == u->g) hh = (u->b - u->r) / d + 2; else hh = (u->r - u->g) / d + 4; hh /= 6; if (hh < 0) hh += 1; }
    if (h) *h = hh; if (sa) *sa = mx > 0 ? d / mx : 0; if (br) *br = mx; if (a) *a = u->a; return 1;
}
static id c_CGColor(id self, SEL s) { (void)s; return self; }
static id c_self(id self, SEL s) { (void)s; return self; }
static id c_description(id self, SEL s) { (void)s; struct uicolor *u = (struct uicolor *)self; char b[128]; snprintf(b, sizeof b, "UIExtendedSRGBColorSpace %g %g %g %g", u->r, u->g, u->b, u->a); return rt_str(b); }
static rt_bool c_isEqual(id self, SEL s, id o) {
    (void)s; if (self == o) return 1;
    if (!o || !rt_is_kind_of(o, RT_CLASS(UIColor))) return 0;
    struct uicolor *a = (struct uicolor *)self, *b = (struct uicolor *)o;
    return fabs(a->r - b->r) < 1e-6 && fabs(a->g - b->g) < 1e-6 && fabs(a->b - b->b) < 1e-6 && fabs(a->a - b->a) < 1e-6 && a->pattern == b->pattern;
}
static unsigned long c_hash(id self, SEL s) { (void)s; struct uicolor *u = (struct uicolor *)self; return (unsigned long)(u->r * 255) << 24 ^ (unsigned long)(u->g * 255) << 16 ^ (unsigned long)(u->b * 255) << 8 ^ (unsigned long)(u->a * 255); }
static id c_copy(id self, SEL s, void *z) { (void)s; (void)z; return objc_retain(self); }
static void c_setFill(id self, SEL s) { (void)s; iosrt_CGContextSetFillColorWithColor(rt_cg_top(), self); }
static void c_setStroke(id self, SEL s) { (void)s; iosrt_CGContextSetStrokeColorWithColor(rt_cg_top(), self); }
static void c_set(id self, SEL s) { c_setFill(self, s); c_setStroke(self, s); }

/* ============================================================ UIResponder */
static void r_touches(id self, SEL s, id touches, id ev) {
    id next = RT_SEND0(id, self, "nextResponder");
    if (next) ((void (*)(id, SEL, id, id))objc_msgSend)(next, s, touches, ev);
}
static id r_nil(id self, SEL s) { (void)self; (void)s; return NULL; }
static rt_bool r_yes(id self, SEL s) { (void)self; (void)s; return 1; }
static rt_bool r_no(id self, SEL s) { (void)self; (void)s; return 0; }
static rt_bool r_canPerform(id self, SEL s, SEL a, id sender) { (void)s; (void)sender; return rt_responds(self, a); }
static id r_targetForAction(id self, SEL s, SEL a, id sender) {
    (void)s; (void)sender;
    for (id r = self; r; r = RT_SEND0(id, r, "nextResponder")) if (rt_responds(r, a)) return r;
    return NULL;
}
static void r_noop1(id self, SEL s, id a) { (void)self; (void)s; (void)a; }
static void r_noop2(id self, SEL s, id a, id b) { (void)self; (void)s; (void)a; (void)b; }

/* =============================================================== UIView */
#define V(x) ((struct uiview *)(x))
static id layer_of(id v) { return V(v)->layer; }
static Class view_layer_class(Class c) { return (Class)RT_SEND0(id, (id)c, "layerClass"); }
static Class v_layerClass(Class c, SEL s) { (void)c; (void)s; return RT_CLASS(CALayer); }
static void view_setup(id self) {
    struct uiview *v = V(self);
    if (v->layer) return;
    Class lc = view_layer_class(self->isa); if (!lc) lc = RT_CLASS(CALayer);
    v->layer = RT_SEND0(id, RT_SEND0(id, (id)lc, "alloc"), "init");
    RT_SEND1(void, v->layer, "setDelegate:", id, self);
    v->subviews = mkarray(); v->interaction = 1; v->autoresizesSubviews = 1; v->needsLayout = 1;
    RT_SEND1(void, v->layer, "setContentsScale:", double, rt_scr_scale);
}
static id v_initWithFrame(id self, SEL s, CGRect fr) {
    (void)s; view_setup(self);
    rt_view_set_frame(self, fr);
    rt_ui_mark_dirty();
    return self;
}
static id v_init(id self, SEL s) { (void)s; return ((id (*)(id, SEL, CGRect))objc_msgSend)(self, rt_sel("initWithFrame:"), (CGRect){ { 0, 0 }, { 0, 0 } }); }
id rt_view_new(Class c, CGRect frame) { return ((id (*)(id, SEL, CGRect))objc_msgSend)(rt_alloc_instance(c, 0), rt_sel("initWithFrame:"), frame); }
static void v_dealloc(id self, SEL s) {
    (void)s; struct uiview *v = V(self);
    for (size_t i = 0; v->subviews && i < rt_array_count(v->subviews); i++) V(rt_array_at(v->subviews, i))->superview = NULL;
    if (v->layer) { RT_SEND1(void, v->layer, "setDelegate:", id, NULL); RT_SEND0(void, v->layer, "removeFromSuperlayer"); }
    objc_release(v->layer); objc_release(v->subviews); objc_release(v->gestures); objc_release(v->bg); objc_release(v->tint);
    objc_release(v->accessibilityLabel); objc_release(v->accessibilityIdentifier); objc_release(v->restorationIdentifier);
    rt_dealloc_instance(self);
}
static id v_layer(id self, SEL s) { (void)s; view_setup(self); return layer_of(self); }
CGRect rt_view_frame(id v) { view_setup(v); return RT_SEND0(CGRect, layer_of(v), "frame"); }
CGRect rt_view_bounds(id v) { view_setup(v); return RT_SEND0(CGRect, layer_of(v), "bounds"); }
void rt_view_set_frame(id v, CGRect f) { view_setup(v); RT_SEND1(void, layer_of(v), "setFrame:", CGRect, f); }
static CGRect v_frame(id self, SEL s) { (void)s; return rt_view_frame(self); }
void rt_print_backtrace(void);
static void v_setFrame(id self, SEL s, CGRect r) {
    (void)s;
    if (getenv("IOSRT_TRACE_FRAMES")) { char b[256]; int n = snprintf(b, sizeof b, "[frames] %s %p setFrame (%g,%g %gx%g)\n", rt_class_name(self->isa), (void *)self, r.origin.x, r.origin.y, r.size.width, r.size.height); rt_out(2, b, (size_t)n); rt_print_backtrace(); }
    rt_view_set_frame(self, r);
}
static CGRect v_bounds(id self, SEL s) { (void)s; return rt_view_bounds(self); }
static void v_setBounds(id self, SEL s, CGRect r) { (void)s; view_setup(self); RT_SEND1(void, layer_of(self), "setBounds:", CGRect, r); }
static CGPoint v_center(id self, SEL s) { (void)s; view_setup(self); return RT_SEND0(CGPoint, layer_of(self), "position"); }
static void v_setCenter(id self, SEL s, CGPoint p) { (void)s; view_setup(self); RT_SEND1(void, layer_of(self), "setPosition:", CGPoint, p); }
static gfx_mat v_transform(id self, SEL s) { (void)s; view_setup(self); return RT_SEND0(gfx_mat, layer_of(self), "affineTransform"); }
static void v_setTransform(id self, SEL s, gfx_mat t) { (void)s; view_setup(self); RT_SEND1(void, layer_of(self), "setAffineTransform:", gfx_mat, t); }
static id v_bg(id self, SEL s) { (void)s; return V(self)->bg; }
static void v_setBg(id self, SEL s, id c) {
    (void)s; struct uiview *v = V(self); view_setup(self);
    objc_retain(c); objc_release(v->bg); v->bg = c;
    RT_SEND1(void, v->layer, "setBackgroundColor:", id, c);
}
static double v_alpha(id self, SEL s) { (void)s; view_setup(self); return RT_SEND0(float, layer_of(self), "opacity"); }
static void v_setAlpha(id self, SEL s, double a) { (void)s; view_setup(self); RT_SEND1(void, layer_of(self), "setOpacity:", float, (float)a); }
static rt_bool v_hidden(id self, SEL s) { (void)s; view_setup(self); return RT_SEND0(long, layer_of(self), "isHidden") & 0xff; }
static void v_setHidden(id self, SEL s, long h) { (void)s; view_setup(self); RT_SEND1(void, layer_of(self), "setHidden:", long, h & 0xff); }
static rt_bool v_clips(id self, SEL s) { (void)s; view_setup(self); return RT_SEND0(long, layer_of(self), "masksToBounds") & 0xff; }
static void v_setClips(id self, SEL s, long h) { (void)s; view_setup(self); RT_SEND1(void, layer_of(self), "setMasksToBounds:", long, h & 0xff); }
static id v_subviews(id self, SEL s) { (void)s; view_setup(self); return objc_autorelease(RT_SEND0(id, V(self)->subviews, "copy")); }
static id v_superview(id self, SEL s) { (void)s; return V(self)->superview; }
id rt_view_window(id v) { while (v && !rt_is_kind_of(v, RT_CLASS(UIWindow))) v = V(v)->superview; return v; }
static id v_window(id self, SEL s) { (void)s; return rt_view_window(self); }
static void notify_window_change_views(id v, id win) {
    if (rt_responds(v, rt_sel("willMoveToWindow:"))) RT_SEND1(void, v, "willMoveToWindow:", id, win);
    if (rt_responds(v, rt_sel("didMoveToWindow"))) RT_SEND0(void, v, "didMoveToWindow");
    id subs = V(v)->subviews;
    for (size_t i = 0; subs && i < rt_array_count(subs); i++) notify_window_change_views(rt_array_at(subs, i), win);
}
/* appearance forwarding (iOS 5+): a child controller whose view enters the window while
   its parent is on screen gets viewWillAppear:/viewDidAppear:, and the reverse on leaving */
static void appearance_for_window(id v, id win) {
    struct uiview *uv = V(v);
    if (uv->vc && ((struct uivc *)uv->vc)->view == v) {
        id vc = uv->vc, parent = ((struct uivc *)vc)->parent;
        if (win && parent && ((struct uivc *)parent)->appeared && !((struct uivc *)vc)->appeared) { rt_vc_appear(vc, 1); return; }
        if (!win && ((struct uivc *)vc)->appeared) { rt_vc_appear(vc, 0); return; }
    }
    id subs = uv->subviews;
    for (size_t i = 0; subs && i < rt_array_count(subs); i++) appearance_for_window(rt_array_at(subs, i), win);
}
static void notify_window_change(id v, id win) {
    notify_window_change_views(v, win);
    appearance_for_window(v, win);
}
void rt_view_remove(id self) {
    struct uiview *v = V(self);
    if (!v->superview) return;
    id sup = v->superview, oldwin = rt_view_window(self);
    if (rt_responds(sup, rt_sel("willRemoveSubview:"))) RT_SEND1(void, sup, "willRemoveSubview:", id, self);
    if (rt_responds(self, rt_sel("willMoveToSuperview:"))) RT_SEND1(void, self, "willMoveToSuperview:", id, NULL);
    objc_retain(self);
    v->superview = NULL;
    RT_SEND1(void, V(sup)->subviews, "removeObjectIdenticalTo:", id, self);
    RT_SEND0(void, v->layer, "removeFromSuperlayer");
    if (rt_responds(self, rt_sel("didMoveToSuperview"))) RT_SEND0(void, self, "didMoveToSuperview");
    if (oldwin) notify_window_change(self, NULL);
    objc_autorelease(self);
    rt_ui_mark_dirty();
}
static void v_removeFromSuperview(id self, SEL s) { (void)s; rt_view_remove(self); }
static void place_layer(id parent, id sub) {
    /* keep the sublayer order consistent with the subview order */
    struct uiview *p = V(parent); size_t n = rt_array_count(p->subviews), i;
    for (i = 0; i < n && rt_array_at(p->subviews, i) != sub; i++) {}
    id next = i + 1 < n ? layer_of(rt_array_at(p->subviews, i + 1)) : NULL;
    id prev = i > 0 ? layer_of(rt_array_at(p->subviews, i - 1)) : NULL;
    if (next) RT_SEND2(void, p->layer, "insertSublayer:below:", id, V(sub)->layer, id, next);
    else if (prev) RT_SEND2(void, p->layer, "insertSublayer:above:", id, V(sub)->layer, id, prev);
    else RT_SEND1(void, p->layer, "addSublayer:", id, V(sub)->layer);
}
static void insert_subview(id self, id sub, long idx) {
    if (!sub || sub == self) return;
    view_setup(self); view_setup(sub);
    struct uiview *c = V(sub);
    id oldwin = rt_view_window(sub);
    objc_retain(sub);
    if (c->superview == self) { RT_SEND1(void, V(self)->subviews, "removeObjectIdenticalTo:", id, sub); }
    else {
        if (c->superview) rt_view_remove(sub);
        if (rt_responds(sub, rt_sel("willMoveToSuperview:"))) RT_SEND1(void, sub, "willMoveToSuperview:", id, self);
    }
    size_t n = rt_array_count(V(self)->subviews);
    if (idx < 0 || (size_t)idx > n) idx = (long)n;
    RT_SEND2(void, V(self)->subviews, "insertObject:atIndex:", id, sub, unsigned long, (unsigned long)idx);
    c->superview = self;
    place_layer(self, sub);
    if (rt_responds(sub, rt_sel("didMoveToSuperview"))) RT_SEND0(void, sub, "didMoveToSuperview");
    if (rt_responds(self, rt_sel("didAddSubview:"))) RT_SEND1(void, self, "didAddSubview:", id, sub);
    id win = rt_view_window(self);
    if (win != oldwin) notify_window_change(sub, win);
    objc_release(sub);
    rt_ui_mark_dirty();
}
void rt_view_add_subview(id parent, id sub) { insert_subview(parent, sub, -1); }
static void v_addSubview(id self, SEL s, id sub) { (void)s; insert_subview(self, sub, -1); }
static void v_insertSubviewAt(id self, SEL s, id sub, long idx) { (void)s; insert_subview(self, sub, idx); }
static long sub_index(id self, id sub) { id a = V(self)->subviews; for (size_t i = 0; i < rt_array_count(a); i++) if (rt_array_at(a, i) == sub) return (long)i; return -1; }
static void v_insertBelow(id self, SEL s, id sub, id sib) {
    (void)s; if (V(sub)->superview == self) { objc_retain(sub); RT_SEND1(void, V(self)->subviews, "removeObjectIdenticalTo:", id, sub); V(sub)->superview = NULL; RT_SEND0(void, V(sub)->layer, "removeFromSuperlayer"); }
    long i = sub_index(self, sib); insert_subview(self, sub, i < 0 ? 0 : i);
}
static void v_insertAbove(id self, SEL s, id sub, id sib) {
    (void)s; if (V(sub)->superview == self) { RT_SEND1(void, V(self)->subviews, "removeObjectIdenticalTo:", id, sub); V(sub)->superview = NULL; RT_SEND0(void, V(sub)->layer, "removeFromSuperlayer"); }
    long i = sub_index(self, sib); insert_subview(self, sub, i < 0 ? -1 : i + 1);
}
static void v_bringToFront(id self, SEL s, id sub) { (void)s; if (V(sub)->superview == self) insert_subview(self, sub, -1); }
static void v_sendToBack(id self, SEL s, id sub) { (void)s; if (V(sub)->superview == self) insert_subview(self, sub, 0); }
static void v_exchange(id self, SEL s, long a, long b) {
    (void)s; id arr = V(self)->subviews; long n = (long)rt_array_count(arr);
    if (a < 0 || b < 0 || a >= n || b >= n || a == b) return;
    RT_SEND2(void, arr, "exchangeObjectAtIndex:withObjectAtIndex:", unsigned long, (unsigned long)a, unsigned long, (unsigned long)b);
    for (long i = 0; i < n; i++) { id v = rt_array_at(arr, i); RT_SEND0(void, V(v)->layer, "removeFromSuperlayer"); }
    for (long i = 0; i < n; i++) RT_SEND1(void, V(self)->layer, "addSublayer:", id, V(rt_array_at(arr, i))->layer);
}
static rt_bool v_isDescendant(id self, SEL s, id other) { (void)s; for (id v = self; v; v = V(v)->superview) if (v == other) return 1; return 0; }
static long v_tag(id self, SEL s) { (void)s; return V(self)->tag; }
static void v_setTag(id self, SEL s, long t) { (void)s; V(self)->tag = t; }
static id v_viewWithTag(id self, SEL s, long t) {
    struct uiview *v = V(self);
    if (v->tag == t) return self;
    for (size_t i = 0; v->subviews && i < rt_array_count(v->subviews); i++) { id r = v_viewWithTag(rt_array_at(v->subviews, i), s, t); if (r) return r; }
    return NULL;
}
static rt_bool v_interaction(id self, SEL s) { (void)s; return V(self)->interaction != 0; }
static void v_setInteraction(id self, SEL s, long e) { (void)s; V(self)->interaction = e & 0xff; }
static rt_bool v_multitouch(id self, SEL s) { (void)s; return V(self)->multitouch != 0; }
static void v_setMultitouch(id self, SEL s, long e) { (void)s; V(self)->multitouch = e & 0xff; }
static rt_bool v_exclusive(id self, SEL s) { (void)s; return V(self)->exclusive != 0; }
static void v_setExclusive(id self, SEL s, long e) { (void)s; V(self)->exclusive = e & 0xff; }
static long v_autoresizing(id self, SEL s) { (void)s; return V(self)->autoresizing; }
static void v_setAutoresizing(id self, SEL s, long m) { (void)s; V(self)->autoresizing = m; }
static rt_bool v_autoresizesSubviews(id self, SEL s) { (void)s; return V(self)->autoresizesSubviews != 0; }
static void v_setAutoresizesSubviews(id self, SEL s, long m) { (void)s; V(self)->autoresizesSubviews = m & 0xff; }
static long v_contentMode(id self, SEL s) { (void)s; return V(self)->contentMode; }
static void v_setContentMode(id self, SEL s, long m) { (void)s; V(self)->contentMode = m; rt_ui_mark_dirty(); }
id rt_view_tint(id v) {
    for (id x = v; x; x = V(x)->superview) if (V(x)->tint) return V(x)->tint;
    return rt_color_make(0, 0.478, 1, 1);
}
static id v_tint(id self, SEL s) { (void)s; return rt_view_tint(self); }
static void notify_tint(id v) {
    if (rt_responds(v, rt_sel("tintColorDidChange"))) RT_SEND0(void, v, "tintColorDidChange");
    for (size_t i = 0; V(v)->subviews && i < rt_array_count(V(v)->subviews); i++) notify_tint(rt_array_at(V(v)->subviews, i));
}
static void v_setTint(id self, SEL s, id c) { (void)s; struct uiview *v = V(self); objc_retain(c); objc_release(v->tint); v->tint = c; notify_tint(self); rt_ui_mark_dirty(); }
static long v_tintMode(id self, SEL s) { (void)s; return V(self)->tintAdjust; }
static void v_setTintMode(id self, SEL s, long m) { (void)s; V(self)->tintAdjust = m; }
static void v_setNeedsDisplay(id self, SEL s) { (void)s; view_setup(self); RT_SEND0(void, layer_of(self), "setNeedsDisplay"); }
static void v_setNeedsDisplayInRect(id self, SEL s, CGRect r) { (void)r; v_setNeedsDisplay(self, s); }
static void v_setNeedsLayout(id self, SEL s) { (void)s; V(self)->needsLayout = 1; rt_ui_mark_dirty(); }
static void layout_tree(id v);
static void v_layoutIfNeeded(id self, SEL s) { (void)s; layout_tree(self); }
static void v_noop(id self, SEL s) { (void)self; (void)s; }
static void v_noop1(id self, SEL s, id a) { (void)self; (void)s; (void)a; }
static void v_noopL(id self, SEL s, long a) { (void)self; (void)s; (void)a; }
static rt_bool v_isOpaque(id self, SEL s) { (void)self; (void)s; return 1; }
static void v_noopD(id self, SEL s, double a) { (void)self; (void)s; (void)a; }
static void v_noopFL(id self, SEL s, float a, long b) { (void)self; (void)s; (void)a; (void)b; }
static id v_nil(id self, SEL s) { (void)self; (void)s; return NULL; }
static double v_contentScale(id self, SEL s) { (void)self; (void)s; return rt_scr_scale; }
static CGSize v_sizeThatFits(id self, SEL s, CGSize sz) { (void)s; (void)sz; return rt_view_bounds(self).size; }
static void v_sizeToFit(id self, SEL s) {
    (void)s; CGRect f = rt_view_frame(self);
    CGSize sz = ((CGSize (*)(id, SEL, CGSize))objc_msgSend)(self, rt_sel("sizeThatFits:"), f.size);
    f.size = sz; rt_view_set_frame(self, f);
}
static CGSize v_intrinsic(id self, SEL s) { (void)self; (void)s; return (CGSize){ -1, -1 }; }
static void v_drawRect(id self, SEL s, CGRect r) { (void)self; (void)s; (void)r; }
static id v_nextResponder(id self, SEL s) { (void)s; struct uiview *v = V(self); if (v->vc) return v->vc; if (v->superview) return v->superview; return NULL; }
static id v_gestures(id self, SEL s) { (void)s; return V(self)->gestures; }
static void v_addGesture(id self, SEL s, id g);
static void v_removeGesture(id self, SEL s, id g);
static void v_setGestures(id self, SEL s, id arr) {
    struct uiview *v = V(self);
    while (v->gestures && rt_array_count(v->gestures)) v_removeGesture(self, s, rt_array_at(v->gestures, 0));
    for (size_t i = 0; arr && i < rt_array_count(arr); i++) v_addGesture(self, s, rt_array_at(arr, i));
}
static rt_bool v_endEditing(id self, SEL s, long force) { (void)self; (void)s; (void)force; return 1; }
static UIEdgeInsets v_layoutMargins(id self, SEL s) { (void)s; UIEdgeInsets e = V(self)->layoutMargins; if (!e.top && !e.left && !e.bottom && !e.right) e = (UIEdgeInsets){ 8, 8, 8, 8 }; return e; }
static void v_setLayoutMargins(id self, SEL s, UIEdgeInsets e) { (void)s; V(self)->layoutMargins = e; }
static id v_accLabel(id self, SEL s) { (void)s; return V(self)->accessibilityLabel; }
static void v_setAccLabel(id self, SEL s, id v) { (void)s; objc_retain(v); objc_release(V(self)->accessibilityLabel); V(self)->accessibilityLabel = v; }
static id v_accId(id self, SEL s) { (void)s; return V(self)->accessibilityIdentifier; }
static void v_setAccId(id self, SEL s, id v) { (void)s; objc_retain(v); objc_release(V(self)->accessibilityIdentifier); V(self)->accessibilityIdentifier = v; }
static id v_description(id self, SEL s) {
    (void)s; CGRect f = rt_view_frame(self); char b[256];
    snprintf(b, sizeof b, "<%s: %p; frame = (%g %g; %g %g)>", rt_class_name(self->isa), (void *)self, f.origin.x, f.origin.y, f.size.width, f.size.height);
    return rt_str(b);
}

/* --- coordinates (through the layer tree, so transforms and bounds origins count) */
gfx_mat rt_view_to_window(id v) { view_setup(v); return rt_layer_to_root(layer_of(v)); }
CGPoint rt_view_convert(id from, CGPoint p, id to) {
    gfx_mat m = from ? rt_view_to_window(from) : gfx_mat_identity();
    if (to) m = gfx_mat_mul(m, gfx_mat_invert(rt_view_to_window(to)));
    double x, y; gfx_mat_apply(&m, p.x, p.y, &x, &y); return (CGPoint){ x, y };
}
static CGPoint v_convertPointToView(id self, SEL s, CGPoint p, id to) { (void)s; return rt_view_convert(self, p, to); }
static CGPoint v_convertPointFromView(id self, SEL s, CGPoint p, id from) { (void)s; return rt_view_convert(from, p, self); }
static CGRect v_convertRectToView(id self, SEL s, CGRect r, id to) {
    (void)s; gfx_mat m = rt_view_to_window(self);
    if (to) m = gfx_mat_mul(m, gfx_mat_invert(rt_view_to_window(to)));
    return iosrt_CGRectApplyAffineTransform(r, m);
}
static CGRect v_convertRectFromView(id self, SEL s, CGRect r, id from) {
    (void)s; gfx_mat m = from ? rt_view_to_window(from) : gfx_mat_identity();
    m = gfx_mat_mul(m, gfx_mat_invert(rt_view_to_window(self)));
    return iosrt_CGRectApplyAffineTransform(r, m);
}
static rt_bool v_pointInside(id self, SEL s, CGPoint p, id ev) { (void)s; (void)ev; return iosrt_CGRectContainsPoint(rt_view_bounds(self), p); }
static id v_hitTest(id self, SEL s, CGPoint p, id ev) {
    struct uiview *v = V(self);
    if (!v->interaction || v_hidden(self, NULL) || v_alpha(self, NULL) < 0.01) return NULL;
    if (!(((long (*)(id, SEL, CGPoint, id))objc_msgSend)(self, rt_sel("pointInside:withEvent:"), p, ev) & 0xff)) return NULL;
    for (size_t i = rt_array_count(v->subviews); i-- > 0;) {
        id sub = rt_array_at(v->subviews, i);
        gfx_mat inv = gfx_mat_invert(rt_layer_local(V(sub)->layer));
        double x, y; gfx_mat_apply(&inv, p.x, p.y, &x, &y);
        id h = ((id (*)(id, SEL, CGPoint, id))objc_msgSend)(sub, s, (CGPoint){ x, y }, ev);
        if (h) return h;
    }
    return self;
}

/* --- drawing (called by the layer renderer for view-backed layers) */
void rt_view_draw(id view, struct cgctx *c, CGRect b) {
    int found; IMP imp = rt_lookup(view->isa, rt_sel("drawRect:"), &found);
    if (found && imp != (IMP)v_drawRect) {
        iosrt_CGContextSaveGState(c);
        iosrt_CGContextClipToRect(c, b);
        rt_cg_push(c);
        ((void (*)(id, SEL, CGRect))imp)(view, rt_sel("drawRect:"), b);
        rt_cg_pop();
        iosrt_CGContextRestoreGState(c);
    }
    rt_control_draw(view, c, b);
}

/* --- layout */
static void autoresize(id sub, CGSize old, CGSize nw) {
    struct uiview *v = V(sub); long m = v->autoresizing;
    if (!m) return;
    CGRect f = rt_view_frame(sub);
    double dw = nw.width - old.width, dh = nw.height - old.height;
    if (dw != 0) {
        double l = f.origin.x, w = f.size.width, r = old.width - l - w;
        double fl = (m & 1) ? l : 0, fw = (m & 2) ? w : 0, fr = (m & 4) ? r : 0, tot = fl + fw + fr;
        int nflex = !!(m & 1) + !!(m & 2) + !!(m & 4);
        if (nflex) {
            if (tot <= 0) { fl = (m & 1) ? 1 : 0; fw = (m & 2) ? 1 : 0; fr = (m & 4) ? 1 : 0; tot = fl + fw + fr; }
            f.origin.x += dw * fl / tot; f.size.width += dw * fw / tot;
        }
    }
    if (dh != 0) {
        double t = f.origin.y, h = f.size.height, bt = old.height - t - h;
        double ft = (m & 8) ? t : 0, fh = (m & 16) ? h : 0, fb = (m & 32) ? bt : 0, tot = ft + fh + fb;
        int nflex = !!(m & 8) + !!(m & 16) + !!(m & 32);
        if (nflex) {
            if (tot <= 0) { ft = (m & 8) ? 1 : 0; fh = (m & 16) ? 1 : 0; fb = (m & 32) ? 1 : 0; tot = ft + fh + fb; }
            f.origin.y += dh * ft / tot; f.size.height += dh * fh / tot;
        }
    }
    if (f.size.width < 0) f.size.width = 0;
    if (f.size.height < 0) f.size.height = 0;
    rt_view_set_frame(sub, f);
}
void rt_view_layer_resized(id view, CGSize old) {
    struct uiview *v = V(view);
    v->needsLayout = 1; rt_ui_mark_dirty();
    if (!v->autoresizesSubviews || !v->subviews) return;
    CGSize nw = rt_view_bounds(view).size;
    if (old.width == 0 && old.height == 0) return;   /* first sizing: NIB/code frames are already final */
    for (size_t i = 0; i < rt_array_count(v->subviews); i++) autoresize(rt_array_at(v->subviews, i), old, nw);
}
static void layout_tree(id view) {
    struct uiview *v = V(view);
    if (v->needsLayout) {
        v->needsLayout = 0;
        id vc = v->vc;
        if (vc && rt_responds(vc, rt_sel("viewWillLayoutSubviews"))) RT_SEND0(void, vc, "viewWillLayoutSubviews");
        RT_SEND0(void, view, "layoutSubviews");
        if (vc && rt_responds(vc, rt_sel("viewDidLayoutSubviews"))) RT_SEND0(void, vc, "viewDidLayoutSubviews");
    }
    id subs = v->subviews;
    for (size_t i = 0; subs && i < rt_array_count(subs); i++) layout_tree(rt_array_at(subs, i));
}

/* --- block animations: applied immediately, completion after the duration */
struct anim_cb { void *block; id target; SEL sel; id animID; void *ctx; };
static void anim_completion(void *p) {
    struct anim_cb *a = p;
    if (a->block) { ((void (*)(void *, long))((struct rt_block *)a->block)->invoke)(a->block, 1); _Block_release(a->block); }
    if (a->target && a->sel) ((void (*)(id, SEL, id, id, void *))objc_msgSend)(a->target, a->sel, a->animID, RT_SEND1(id, (id)rt_class_named("NSNumber"), "numberWithBool:", long, 1), a->ctx);
    objc_release(a->target); objc_release(a->animID);
    free(a);
    rt_ui_mark_dirty();
}
static int anims_enabled = 1;
static void run_animation(double duration, double delay, void *anims, void *completion) {
    if (anims) ((void (*)(void *))((struct rt_block *)anims)->invoke)(anims);
    rt_ui_mark_dirty();
    if (completion) {
        struct anim_cb *a = calloc(1, sizeof *a); a->block = _Block_copy(completion);
        rt_after(anims_enabled ? duration + delay : 0, anim_completion, a);
    }
}
static void va_anim(Class c, SEL s, double d, void *anims) { (void)c; (void)s; run_animation(d, 0, anims, NULL); }
static void va_animC(Class c, SEL s, double d, void *anims, void *comp) { (void)c; (void)s; run_animation(d, 0, anims, comp); }
static void va_animDO(Class c, SEL s, double d, double delay, unsigned long o, void *anims, void *comp) { (void)c; (void)s; (void)o; run_animation(d, delay, anims, comp); }
static void va_animSpring(Class c, SEL s, double d, double delay, double damp, double vel, unsigned long o, void *anims, void *comp) { (void)c; (void)s; (void)damp; (void)vel; (void)o; run_animation(d, delay, anims, comp); }
static void va_transition(Class c, SEL s, id view, double d, unsigned long o, void *anims, void *comp) { (void)c; (void)s; (void)view; (void)o; run_animation(d, 0, anims, comp); }
static void va_transitionFrom(Class c, SEL s, id from, id to, double d, unsigned long o, void *comp) {
    (void)c; (void)s; (void)o;
    if (!(o & (1u << 1)) && from && V(from)->superview) { id sup = V(from)->superview; long i = sub_index(sup, from); rt_view_remove(from); insert_subview(sup, to, i); }
    run_animation(d, 0, NULL, comp);
}
static void va_keyframes(Class c, SEL s, double d, double delay, unsigned long o, void *anims, void *comp) { (void)c; (void)s; (void)o; run_animation(d, delay, anims, comp); }
static void va_addKeyframe(Class c, SEL s, double st, double rd, void *anims) { (void)c; (void)s; (void)st; (void)rd; if (anims) ((void (*)(void *))((struct rt_block *)anims)->invoke)(anims); }
static void va_withoutAnimation(Class c, SEL s, void *blk) { (void)c; (void)s; if (blk) ((void (*)(void *))((struct rt_block *)blk)->invoke)(blk); rt_ui_mark_dirty(); }
static void va_setEnabled(Class c, SEL s, long e) { (void)c; (void)s; anims_enabled = e & 0xff; }
static rt_bool va_enabled(Class c, SEL s) { (void)c; (void)s; return anims_enabled; }
/* begin/commitAnimations */
static struct { id animID; void *ctx; double duration, delay; id delegate; SEL stop; int depth; } legacy;
static void va_begin(Class c, SEL s, id animID, void *ctx) { (void)c; (void)s; if (!legacy.depth++) { legacy.animID = objc_retain(animID); legacy.ctx = ctx; legacy.duration = 0.2; legacy.delay = 0; legacy.delegate = NULL; legacy.stop = NULL; } }
static void va_commit(Class c, SEL s) {
    (void)c; (void)s; if (!legacy.depth || --legacy.depth) return;
    rt_ui_mark_dirty();
    if (legacy.delegate && legacy.stop) {
        struct anim_cb *a = calloc(1, sizeof *a); a->target = objc_retain(legacy.delegate); a->sel = legacy.stop; a->animID = legacy.animID; a->ctx = legacy.ctx;
        rt_after(legacy.duration + legacy.delay, anim_completion, a);
    } else objc_release(legacy.animID);
    legacy.animID = NULL;
}
static void va_setDuration(Class c, SEL s, double d) { (void)c; (void)s; legacy.duration = d; }
static void va_setDelay(Class c, SEL s, double d) { (void)c; (void)s; legacy.delay = d; }
static void va_setDelegate(Class c, SEL s, id d) { (void)c; (void)s; legacy.delegate = d; }
static void va_setStop(Class c, SEL s, SEL sel) { (void)c; (void)s; legacy.stop = sel; }
static void va_noopL(Class c, SEL s, long v) { (void)c; (void)s; (void)v; }
static void va_noop2(Class c, SEL s, long v, id w) { (void)c; (void)s; (void)v; (void)w; }

/* ============================================================== UIWindow */
static id g_app;
#define W(x) ((struct uiwindow *)(x))
static void app_add_window(id w);
static id w_initWithFrame(id self, SEL s, CGRect fr) { v_initWithFrame(self, s, fr); app_add_window(self); return self; }
static void w_dealloc(id self, SEL s) { objc_release(W(self)->root); v_dealloc(self, s); }
static void w_makeKey(id self, SEL s) {
    (void)s; struct uiapp *a = (struct uiapp *)g_app;
    if (a && a->keyWindow != self) {
        id old = a->keyWindow; a->keyWindow = objc_retain(self);
        if (old && rt_responds(old, rt_sel("resignKeyWindow"))) RT_SEND0(void, old, "resignKeyWindow");
        objc_release(old);
        if (rt_responds(self, rt_sel("becomeKeyWindow"))) RT_SEND0(void, self, "becomeKeyWindow");
    }
    rt_ui_mark_dirty();
}
/* put the root controller's view into the window (once) */
static void w_install_root(id self) {
    id vc = W(self)->root;
    if (!vc) return;
    id view = RT_SEND0(id, vc, "view");
    if (view && V(view)->superview != self) {
        rt_view_set_frame(view, rt_view_bounds(self));
        V(view)->autoresizing = 2 | 16;
        insert_subview(self, view, -1);
    }
}
static void w_makeKeyAndVisible(id self, SEL s) {
    struct uiwindow *w = W(self);
    w->hiddenWin = 0; v_setHidden(self, NULL, 0);
    w_install_root(self);
    w_makeKey(self, s);
    if (w->root && !((struct uivc *)w->root)->appeared) rt_vc_appear(w->root, 1);
}
static id w_root(id self, SEL s) { (void)s; return W(self)->root; }
static void w_setRoot(id self, SEL s, id vc) {
    (void)s; struct uiwindow *w = W(self);
    if (w->root == vc) return;
    id old = w->root;
    if (old) { id ov = ((struct uivc *)old)->view; if (ov && V(ov)->superview == self) rt_view_remove(ov); }
    w->root = objc_retain(vc);
    if (vc) {
        w_install_root(self);
        struct uiapp *a = (struct uiapp *)g_app;
        if (a && a->keyWindow == self) rt_vc_appear(vc, 1);
    }
    objc_release(old);
}
static id w_screen(id self, SEL s) { (void)self; (void)s; return RT_SEND0(id, (id)RT_CLASS(UIScreen), "mainScreen"); }
static double w_level(id self, SEL s) { (void)s; return W(self)->level; }
static void w_setLevel(id self, SEL s, double l) { (void)s; W(self)->level = l; rt_ui_mark_dirty(); }
static rt_bool w_isKey(id self, SEL s) { (void)s; return g_app && ((struct uiapp *)g_app)->keyWindow == self; }
static CGPoint w_convertPointToWindow(id self, SEL s, CGPoint p, id w) { (void)s; return rt_view_convert(self, p, w); }
static CGPoint w_convertPointFromWindow(id self, SEL s, CGPoint p, id w) { (void)s; return rt_view_convert(w, p, self); }
static void w_sendEvent(id self, SEL s, id ev) { (void)self; (void)s; (void)ev; }
static id w_nextResponder(id self, SEL s) { (void)self; (void)s; return g_app; }

/* ======================================================= UIScreen / UIDevice */
static id g_screen, g_device;
static id sc_main(Class c, SEL s) { (void)s; if (!g_screen) g_screen = rt_alloc_instance(c, 0); return g_screen; }
static id sc_screens(Class c, SEL s) { id m = sc_main(c, s); return objc_autorelease(rt_array_new(&m, 1)); }
static CGRect sc_bounds(id self, SEL s) { (void)self; (void)s; return (CGRect){ { 0, 0 }, { rt_scr_w, rt_scr_h } }; }
static CGRect sc_nativeBounds(id self, SEL s) { (void)self; (void)s; return (CGRect){ { 0, 0 }, { rt_scr_w * rt_scr_scale, rt_scr_h * rt_scr_scale } }; }
static double sc_scale(id self, SEL s) { (void)self; (void)s; return rt_scr_scale; }
static double sc_brightness(id self, SEL s) { (void)self; (void)s; return 0.5; }
static long sc_fps(id self, SEL s) { (void)self; (void)s; return 60; }
static id sc_displayLink(id self, SEL s, id target, SEL sel) { (void)self; (void)s; return RT_SEND2(id, (id)rt_class_named("CADisplayLink"), "displayLinkWithTarget:selector:", id, target, SEL, sel); }
static id dev_current(Class c, SEL s) { (void)s; if (!g_device) g_device = rt_alloc_instance(c, 0); return g_device; }
static id dev_sysver(id self, SEL s) { (void)self; (void)s; return rt_str("12.0"); }
static id dev_sysname(id self, SEL s) { (void)self; (void)s; return rt_str("iOS"); }
static id dev_model(id self, SEL s) { (void)self; (void)s; return rt_str("iPhone"); }
static id dev_name(id self, SEL s) { (void)self; (void)s; return rt_str("iosrt device"); }
static long dev_idiom(id self, SEL s) { (void)self; (void)s; return 0; }
static long dev_orientation(id self, SEL s) { (void)self; (void)s; return 1; }
static float dev_battery(id self, SEL s) { (void)self; (void)s; return 1.0f; }
static long dev_batteryState(id self, SEL s) { (void)self; (void)s; return 3; }
static id dev_vendorId(id self, SEL s) { (void)self; (void)s; return RT_SEND0(id, RT_SEND0(id, (id)rt_class_named("NSUUID"), "alloc"), "init"); }
static rt_bool dev_multitask(id self, SEL s) { (void)self; (void)s; return 1; }

/* =========================================================== UIApplication */
#define APP ((struct uiapp *)g_app)
static id app_shared(Class c, SEL s) {
    (void)s;
    if (!g_app) { g_app = rt_alloc_instance(c, 0); APP->windows = mkarray(); }
    return g_app;
}
static void app_add_window(id w) {
    if (!g_app) app_shared(RT_CLASS(UIApplication), NULL);
    for (size_t i = 0; i < rt_array_count(APP->windows); i++) if (rt_array_at(APP->windows, i) == w) return;
    RT_SEND1(void, APP->windows, "addObject:", id, w);   /* app keeps windows alive like iOS's window list */
}
id rt_app_key_window(void) { return g_app ? APP->keyWindow : NULL; }
static id app_delegate(id self, SEL s) { (void)s; return ((struct uiapp *)self)->delegate; }
static void app_setDelegate(id self, SEL s, id d) { (void)s; ((struct uiapp *)self)->delegate = d; }
static id app_keyWindow(id self, SEL s) { (void)s; return ((struct uiapp *)self)->keyWindow; }
static id app_windows(id self, SEL s) { (void)s; return ((struct uiapp *)self)->windows; }
static long app_state(id self, SEL s) { (void)self; (void)s; return 0; }
static long app_orientation(id self, SEL s) { (void)self; (void)s; return 1; }
static rt_bool app_openURL(id self, SEL s, id url) { (void)self; (void)s; if (url) rt_log("openURL: %s (not supported)", rt_str_utf8(RT_SEND0(id, url, "absoluteString"))); return 0; }
static void app_openURLopts(id self, SEL s, id url, id opts, void *comp) { (void)opts; app_openURL(self, s, url); if (comp) ((void (*)(void *, long))((struct rt_block *)comp)->invoke)(comp, 0); }
static rt_bool app_canOpenURL(id self, SEL s, id url) { (void)self; (void)s; (void)url; return 0; }
static void app_setIdle(id self, SEL s, long v) { (void)s; ((struct uiapp *)self)->idle = v; }
static rt_bool app_idle(id self, SEL s) { (void)s; return ((struct uiapp *)self)->idle != 0; }
static long app_badge(id self, SEL s) { (void)s; return ((struct uiapp *)self)->badge; }
static void app_setBadge(id self, SEL s, long v) { (void)s; ((struct uiapp *)self)->badge = v; }
static rt_bool app_statusHidden(id self, SEL s) { (void)s; return 1; (void)self; }
static void app_setStatusHidden(id self, SEL s, long v) { (void)s; ((struct uiapp *)self)->statusBarHidden = v; }
static void app_setStatusHidden2(id self, SEL s, long v, long anim) { (void)anim; app_setStatusHidden(self, s, v); }
static long app_statusStyle(id self, SEL s) { (void)s; return ((struct uiapp *)self)->statusBarStyle; }
static void app_setStatusStyle(id self, SEL s, long v) { (void)s; ((struct uiapp *)self)->statusBarStyle = v; }
static void app_setStatusStyle2(id self, SEL s, long v, long a) { (void)a; app_setStatusStyle(self, s, v); }
static CGRect app_statusFrame(id self, SEL s) { (void)self; (void)s; return (CGRect){ { 0, 0 }, { rt_scr_w, 0 } }; }
static long app_zero(id self, SEL s) { (void)self; (void)s; return 0; }
static double app_bgTime(id self, SEL s) { (void)self; (void)s; return 1e9; }
static unsigned long app_beginBg(id self, SEL s, void *h) { (void)self; (void)s; (void)h; static unsigned long n; return ++n; }
static unsigned long app_beginBgNamed(id self, SEL s, id name, void *h) { (void)name; return app_beginBg(self, s, h); }
static void app_endBg(id self, SEL s, unsigned long t) { (void)self; (void)s; (void)t; }
static rt_bool app_ignoring(id self, SEL s) { (void)s; return ((struct uiapp *)self)->ignoring > 0; }
static void app_beginIgnore(id self, SEL s) { (void)s; ((struct uiapp *)self)->ignoring++; }
static void app_endIgnore(id self, SEL s) { (void)s; if (((struct uiapp *)self)->ignoring > 0) ((struct uiapp *)self)->ignoring--; }
void rt_send_action(SEL action, id target, id sender, id event) {
    if (!action) return;
    if (!target) {
        /* nil target: walk the responder chain from the first responder (approximated by the sender) */
        for (id r = sender; r; r = RT_SEND0(id, r, "nextResponder")) if (rt_responds(r, action)) { target = r; break; }
        if (!target) return;
    }
    ((void (*)(id, SEL, id, id))objc_msgSend)(target, action, sender, event);
}
static rt_bool app_sendAction(id self, SEL s, SEL action, id target, id sender, id event) { (void)self; (void)s; rt_send_action(action, target, sender, event); return 1; }
static id app_nextResponder(id self, SEL s) { (void)s; id d = ((struct uiapp *)self)->delegate; return d && rt_is_kind_of(d, RT_CLASS(UIResponder)) ? d : NULL; }
static id app_preferredLanguage(id self, SEL s) { (void)self; (void)s; return rt_str("en"); }

/* ============================================================ UITouch/UIEvent */
#define TO(x) ((struct uitouch *)(x))
static CGPoint t_locationInView(id self, SEL s, id view) { (void)s; return rt_view_convert(NULL, TO(self)->pt, view); }
static CGPoint t_prevLocationInView(id self, SEL s, id view) { (void)s; return rt_view_convert(NULL, TO(self)->prev, view); }
static id t_view(id self, SEL s) { (void)s; return TO(self)->view; }
static id t_window(id self, SEL s) { (void)s; return TO(self)->window; }
static long t_phase(id self, SEL s) { (void)s; return TO(self)->phase; }
static unsigned long t_tapCount(id self, SEL s) { (void)s; return (unsigned long)TO(self)->tapCount; }
static double t_timestamp(id self, SEL s) { (void)s; return TO(self)->ts; }
static id t_gestures(id self, SEL s) { (void)s; return TO(self)->gestures; }
static double t_majorRadius(id self, SEL s) { (void)self; (void)s; return 20; }
static double t_force(id self, SEL s) { (void)self; (void)s; return 0; }
static long t_type(id self, SEL s) { (void)self; (void)s; return 0; }
static void t_dealloc(id self, SEL s) { (void)s; objc_release(TO(self)->gestures); rt_dealloc_instance(self); }
struct uievent { Class isa; id touches; double ts; };
static id ev_all(id self, SEL s) { (void)s; return ((struct uievent *)self)->touches; }
static id ev_forView(id self, SEL s, id view) {
    (void)s; id all = ((struct uievent *)self)->touches, out = RT_SEND0(id, (id)rt_class_named("NSMutableSet"), "set");
    id arr = all ? RT_SEND0(id, all, "allObjects") : NULL;
    for (size_t i = 0; arr && i < rt_array_count(arr); i++) { id t = rt_array_at(arr, i); if (TO(t)->view == view) RT_SEND1(void, out, "addObject:", id, t); }
    return out;
}
static id ev_forWindow(id self, SEL s, id w) { (void)w; return ev_all(self, s); }
static id ev_forGesture(id self, SEL s, id g) { (void)g; return ev_all(self, s); }
static double ev_ts(id self, SEL s) { (void)s; return ((struct uievent *)self)->ts; }
static long ev_type(id self, SEL s) { (void)self; (void)s; return 0; }
static void ev_dealloc(id self, SEL s) { (void)s; objc_release(((struct uievent *)self)->touches); rt_dealloc_instance(self); }

static id uns_make(Class c, SEL s, unsigned long t, id cats) { (void)s; (void)t; (void)cats; return objc_autorelease(rt_alloc_instance(c, 0)); }

/* ===================================================== gesture recognizers */
enum { GS_POSSIBLE, GS_BEGAN, GS_CHANGED, GS_ENDED, GS_CANCELLED, GS_FAILED };
enum { GK_BASE, GK_TAP, GK_LONG, GK_PAN, GK_SWIPE, GK_PINCH, GK_ROT };
struct uigr {
    Class isa; id targets; id view; long state, enabled, cancels, delaysBegan, delaysEnded; id delegate; id requires;
    long kind, ntouch; CGPoint loc, start, last; double startT, lastT; CGPoint vel, transl;
    long tapsReq, touchesReq, taps; double minDur, allowMove; long direction; double scale, rotation, velocity;
    long gen, waiting, pending, minT, maxT, edges; id name; long active; long down;
};
#define G(x) ((struct uigr *)(x))
static id active_gestures;      /* recognizers involved in the current touch sequence */
static void g_process(void);
static id gr_init(id self, SEL s) {
    (void)s; struct uigr *g = G(self);
    g->targets = mkarray(); g->enabled = 1; g->cancels = 1; g->delaysEnded = 1; g->tapsReq = 1; g->touchesReq = 1;
    g->minDur = 0.5; g->allowMove = 10; g->direction = 1; g->scale = 1; g->minT = 1; g->maxT = 1 << 20;
    g->kind = rt_is_kind_of(self, RT_CLASS(UITapGestureRecognizer)) ? GK_TAP : rt_is_kind_of(self, RT_CLASS(UILongPressGestureRecognizer)) ? GK_LONG :
              rt_is_kind_of(self, RT_CLASS(UIPanGestureRecognizer)) ? GK_PAN : rt_is_kind_of(self, RT_CLASS(UISwipeGestureRecognizer)) ? GK_SWIPE :
              rt_is_kind_of(self, RT_CLASS(UIPinchGestureRecognizer)) ? GK_PINCH : rt_is_kind_of(self, RT_CLASS(UIRotationGestureRecognizer)) ? GK_ROT : GK_BASE;
    return self;
}
static void gr_addTarget(id self, SEL s, id target, SEL action) {
    (void)s; if (!action) return;
    id ta = rt_alloc_instance(RT_CLASS(NSObject), sizeof(struct target_action) - sizeof(void *));
    ((struct target_action *)ta)->target = target; ((struct target_action *)ta)->action = action;
    RT_SEND1(void, G(self)->targets, "addObject:", id, ta); objc_release(ta);
}
static id gr_initWithTarget(id self, SEL s, id target, SEL action) {
    self = RT_SEND0(id, self, "init");
    if (self && target && action) gr_addTarget(self, s, target, action);
    return self;
}
static void gr_removeTarget(id self, SEL s, id target, SEL action) {
    (void)s; id arr = G(self)->targets;
    for (size_t i = rt_array_count(arr); i-- > 0;) {
        struct target_action *t = (struct target_action *)rt_array_at(arr, i);
        if ((!target || t->target == target) && (!action || t->action == action)) RT_SEND1(void, arr, "removeObjectAtIndex:", unsigned long, (unsigned long)i);
    }
}
static void gr_dealloc(id self, SEL s) { (void)s; objc_release(G(self)->targets); objc_release(G(self)->requires); objc_release(G(self)->name); rt_dealloc_instance(self); }
static long gr_state(id self, SEL s) { (void)s; return G(self)->state; }
static void gr_setState(id self, SEL s, long st) {
    (void)s; struct uigr *g = G(self);
    if (g->state == st && st != GS_CHANGED) return;
    g->state = st;
    if (st == GS_BEGAN || st == GS_CHANGED || st == GS_ENDED || st == GS_CANCELLED) g->pending = 1;
}
static id gr_view(id self, SEL s) { (void)s; return G(self)->view; }
static rt_bool gr_enabled(id self, SEL s) { (void)s; return G(self)->enabled != 0; }
static void gr_setEnabled(id self, SEL s, long e) {
    (void)s; struct uigr *g = G(self); g->enabled = e & 0xff;
    if (!g->enabled && (g->state == GS_BEGAN || g->state == GS_CHANGED)) { g->state = GS_CANCELLED; g->pending = 1; g_process(); }
}
static id gr_delegate(id self, SEL s) { (void)s; return G(self)->delegate; }
static void gr_setDelegate(id self, SEL s, id d) { (void)s; G(self)->delegate = d; }
static rt_bool gr_cancels(id self, SEL s) { (void)s; return G(self)->cancels != 0; }
static void gr_setCancels(id self, SEL s, long v) { (void)s; G(self)->cancels = v & 0xff; }
static void gr_setDelaysBegan(id self, SEL s, long v) { (void)s; G(self)->delaysBegan = v & 0xff; }
static void gr_setDelaysEnded(id self, SEL s, long v) { (void)s; G(self)->delaysEnded = v & 0xff; }
static rt_bool gr_delaysBegan(id self, SEL s) { (void)s; return G(self)->delaysBegan != 0; }
static rt_bool gr_delaysEnded(id self, SEL s) { (void)s; return G(self)->delaysEnded != 0; }
static void gr_requireFail(id self, SEL s, id other) {
    (void)s; struct uigr *g = G(self);
    if (!g->requires) g->requires = mkarray();
    if (other) RT_SEND1(void, g->requires, "addObject:", id, other);
}
static unsigned long gr_numTouches(id self, SEL s) { (void)s; return G(self)->down ? (unsigned long)G(self)->ntouch : (G(self)->state == GS_POSSIBLE ? 0 : 1); }
static CGPoint gr_location(id self, SEL s, id view) { (void)s; return rt_view_convert(NULL, G(self)->loc, view); }
static CGPoint gr_locationOfTouch(id self, SEL s, unsigned long i, id view) { (void)i; return gr_location(self, s, view); }
static void gr_reset(id self, SEL s) { (void)self; (void)s; }
static void gr_ignoreTouch(id self, SEL s, id t, id ev) { (void)self; (void)s; (void)t; (void)ev; }
static id gr_name(id self, SEL s) { (void)s; return G(self)->name; }
static void gr_setName(id self, SEL s, id n) { (void)s; objc_retain(n); objc_release(G(self)->name); G(self)->name = n; }
/* tap */
static unsigned long gr_tapsReq(id self, SEL s) { (void)s; return (unsigned long)G(self)->tapsReq; }
static void gr_setTapsReq(id self, SEL s, unsigned long v) { (void)s; G(self)->tapsReq = v ? (long)v : 1; }
static unsigned long gr_touchesReq(id self, SEL s) { (void)s; return (unsigned long)G(self)->touchesReq; }
static void gr_setTouchesReq(id self, SEL s, unsigned long v) { (void)s; G(self)->touchesReq = v ? (long)v : 1; }
/* long press */
static double gr_minDur(id self, SEL s) { (void)s; return G(self)->minDur; }
static void gr_setMinDur(id self, SEL s, double v) { (void)s; G(self)->minDur = v; }
static double gr_allowMove(id self, SEL s) { (void)s; return G(self)->allowMove; }
static void gr_setAllowMove(id self, SEL s, double v) { (void)s; G(self)->allowMove = v; }
/* pan */
static CGPoint gr_translation(id self, SEL s, id view) {
    (void)s; struct uigr *g = G(self);
    CGPoint d = { g->loc.x - g->start.x - g->transl.x, g->loc.y - g->start.y - g->transl.y };
    if (view) { gfx_mat m = rt_view_to_window(view); m.tx = m.ty = 0; gfx_mat inv = gfx_mat_invert(m); double x, y; gfx_mat_apply(&inv, d.x, d.y, &x, &y); d = (CGPoint){ x, y }; }
    return d;
}
static void gr_setTranslation(id self, SEL s, CGPoint t, id view) {
    (void)s; (void)view; struct uigr *g = G(self);
    g->transl = (CGPoint){ g->loc.x - g->start.x - t.x, g->loc.y - g->start.y - t.y };
}
static CGPoint gr_velocity(id self, SEL s, id view) { (void)s; (void)view; return G(self)->vel; }
static unsigned long gr_minT(id self, SEL s) { (void)s; return (unsigned long)G(self)->minT; }
static void gr_setMinT(id self, SEL s, unsigned long v) { (void)s; G(self)->minT = (long)v; }
static unsigned long gr_maxT(id self, SEL s) { (void)s; return (unsigned long)G(self)->maxT; }
static void gr_setMaxT(id self, SEL s, unsigned long v) { (void)s; G(self)->maxT = (long)v; }
static unsigned long gr_edges(id self, SEL s) { (void)s; return (unsigned long)G(self)->edges; }
static void gr_setEdges(id self, SEL s, unsigned long v) { (void)s; G(self)->edges = (long)v; }
/* swipe */
static unsigned long gr_direction(id self, SEL s) { (void)s; return (unsigned long)G(self)->direction; }
static void gr_setDirection(id self, SEL s, unsigned long v) { (void)s; G(self)->direction = (long)v; }
/* pinch / rotation */
static double gr_scale(id self, SEL s) { (void)s; return G(self)->scale; }
static void gr_setScale(id self, SEL s, double v) { (void)s; G(self)->scale = v; }
static double gr_rotation(id self, SEL s) { (void)s; return G(self)->rotation; }
static void gr_setRotation(id self, SEL s, double v) { (void)s; G(self)->rotation = v; }
static double gr_velocityScalar(id self, SEL s) { (void)s; return G(self)->velocity; }

static CGPoint touch_pt(id touches) { id t = touches ? RT_SEND0(id, touches, "anyObject") : NULL; return t ? TO(t)->pt : (CGPoint){ 0, 0 }; }
struct tap_timer { id g; long gen; };
static void tap_timeout(void *p) {
    struct tap_timer *t = p; struct uigr *g = G(t->g);
    if (g->gen == t->gen && g->state == GS_POSSIBLE && !g->down && g->taps > 0) { g->state = GS_FAILED; g_process(); }
    objc_release(t->g); free(t);
}
static void long_timeout(void *p) {
    struct tap_timer *t = p; struct uigr *g = G(t->g);
    if (g->gen == t->gen && g->state == GS_POSSIBLE && g->down) { gr_setState(t->g, NULL, GS_BEGAN); g_process(); }
    objc_release(t->g); free(t);
}
/* built-in recognition logic (subclasses override these and call super) */
static void gr_touchesBegan(id self, SEL s, id touches, id ev) {
    (void)s; (void)ev; struct uigr *g = G(self);
    CGPoint p = touch_pt(touches); double now = rt_now();
    g->down = 1; g->ntouch = 1; g->loc = g->last = p; g->lastT = now; g->vel = (CGPoint){ 0, 0 };
    if (g->kind == GK_TAP) {
        if (g->taps == 0) g->start = p;
        else if (hypot(p.x - g->start.x, p.y - g->start.y) > 40) { gr_setState(self, NULL, GS_FAILED); return; }
        g->startT = now;
    } else { g->start = p; g->startT = now; g->transl = (CGPoint){ 0, 0 }; }
    if (g->kind == GK_LONG) { struct tap_timer *t = malloc(sizeof *t); t->g = objc_retain(self); t->gen = g->gen; rt_after(g->minDur, long_timeout, t); }
}
static void gr_touchesMoved(id self, SEL s, id touches, id ev) {
    (void)s; (void)ev; struct uigr *g = G(self);
    CGPoint p = touch_pt(touches); double now = rt_now(), dt = now - g->lastT;
    if (dt > 0.001) { g->vel = (CGPoint){ (p.x - g->last.x) / dt, (p.y - g->last.y) / dt }; }
    g->last = p; g->lastT = now; g->loc = p;
    double dist = hypot(p.x - g->start.x, p.y - g->start.y);
    switch (g->kind) {
    case GK_TAP: if (dist > 10) gr_setState(self, NULL, GS_FAILED); break;
    case GK_LONG:
        if (g->state == GS_POSSIBLE && dist > g->allowMove) gr_setState(self, NULL, GS_FAILED);
        else if (g->state == GS_BEGAN || g->state == GS_CHANGED) gr_setState(self, NULL, GS_CHANGED);
        break;
    case GK_PAN:
        if (g->state == GS_POSSIBLE && dist > 10) gr_setState(self, NULL, GS_BEGAN);
        else if (g->state == GS_BEGAN || g->state == GS_CHANGED) gr_setState(self, NULL, GS_CHANGED);
        break;
    case GK_SWIPE: {
        double dx = p.x - g->start.x, dy = p.y - g->start.y;
        if (now - g->startT > 0.6) { gr_setState(self, NULL, GS_FAILED); break; }
        int dir = fabs(dx) > fabs(dy) ? (dx > 0 ? 1 : 2) : (dy > 0 ? 8 : 4);
        if (hypot(dx, dy) > 50 && (g->direction & dir)) gr_setState(self, NULL, GS_ENDED);
        break;
    }
    default: break;
    }
}
static void gr_touchesEnded(id self, SEL s, id touches, id ev) {
    (void)s; (void)ev; struct uigr *g = G(self);
    g->loc = touch_pt(touches); g->down = 0;
    switch (g->kind) {
    case GK_TAP:
        if (g->state != GS_POSSIBLE) break;
        if (++g->taps >= g->tapsReq) gr_setState(self, NULL, GS_ENDED);
        else { struct tap_timer *t = malloc(sizeof *t); t->g = objc_retain(self); t->gen = g->gen; rt_after(0.35, tap_timeout, t); }
        break;
    case GK_LONG: case GK_PAN:
        if (g->state == GS_BEGAN || g->state == GS_CHANGED) gr_setState(self, NULL, GS_ENDED); else gr_setState(self, NULL, GS_FAILED);
        break;
    case GK_SWIPE: if (g->state == GS_POSSIBLE) gr_setState(self, NULL, GS_FAILED); break;
    case GK_PINCH: case GK_ROT: gr_setState(self, NULL, g->state == GS_POSSIBLE ? GS_FAILED : GS_ENDED); break;
    default: break;
    }
}
static void gr_touchesCancelled(id self, SEL s, id touches, id ev) {
    (void)s; (void)touches; (void)ev; struct uigr *g = G(self); g->down = 0;
    if (g->state == GS_BEGAN || g->state == GS_CHANGED) gr_setState(self, NULL, GS_CANCELLED); else if (g->state == GS_POSSIBLE) gr_setState(self, NULL, GS_FAILED);
}
static void v_addGesture(id self, SEL s, id g) {
    (void)s; struct uiview *v = V(self);
    if (!g) return;
    if (!v->gestures) v->gestures = mkarray();
    if (G(g)->view && G(g)->view != self) v_removeGesture(G(g)->view, NULL, g);
    for (size_t i = 0; i < rt_array_count(v->gestures); i++) if (rt_array_at(v->gestures, i) == g) return;
    RT_SEND1(void, v->gestures, "addObject:", id, g); G(g)->view = self;
}
static void v_removeGesture(id self, SEL s, id g) { (void)s; struct uiview *v = V(self); if (!v->gestures || !g) return; G(g)->view = NULL; RT_SEND1(void, v->gestures, "removeObjectIdenticalTo:", id, g); }

/* --- dispatcher (seq_view: hit view of the current touch sequence) */
static long seq_view_cancelled;
static id seq_touch, seq_event;
static void send_actions(id g) {
    id arr = objc_retain(RT_SEND0(id, G(g)->targets, "copy"));
    for (size_t i = 0; i < rt_array_count(arr); i++) {
        struct target_action *t = (struct target_action *)rt_array_at(arr, i);
        if (t->target) ((void (*)(id, SEL, id))objc_msgSend)(t->target, t->action, g);
    }
    objc_release(arr);
}
static int simultaneous(id a, id b) {
    id da = G(a)->delegate, db = G(b)->delegate;
    SEL q = rt_sel("gestureRecognizer:shouldRecognizeSimultaneouslyWithGestureRecognizer:");
    if (da && rt_responds(da, q) && (RT_SEND2(long, da, "gestureRecognizer:shouldRecognizeSimultaneouslyWithGestureRecognizer:", id, a, id, b) & 0xff)) return 1;
    if (db && rt_responds(db, q) && (RT_SEND2(long, db, "gestureRecognizer:shouldRecognizeSimultaneouslyWithGestureRecognizer:", id, b, id, a) & 0xff)) return 1;
    return 0;
}
static id seq_view;
static int should_begin(id g) {
    /* a scroll view's own pan only takes over content touches it may cancel
       (UIScrollView -touchesShouldCancelInContentView:, NO for controls) */
    id sv = G(g)->view;
    if (sv && rt_is_kind_of(sv, RT_CLASS(UIScrollView)) && ((struct uiscroll *)sv)->pan == g && seq_view && seq_view != sv) {
        if (!((struct uiscroll *)sv)->cancelsContent) return 0;
        if (!(RT_SEND1(long, sv, "touchesShouldCancelInContentView:", id, seq_view) & 0xff)) return 0;
    }
    id d = G(g)->delegate;
    if (d && rt_responds(d, rt_sel("gestureRecognizerShouldBegin:")) && !(RT_SEND1(long, d, "gestureRecognizerShouldBegin:", id, g) & 0xff)) return 0;
    id v = G(g)->view;
    if (v && rt_responds(v, rt_sel("gestureRecognizerShouldBegin:")) && !(RT_SEND1(long, v, "gestureRecognizerShouldBegin:", id, g) & 0xff)) return 0;
    return 1;
}
static int waits_for_other(id g) {
    id req = G(g)->requires;
    for (size_t i = 0; req && i < rt_array_count(req); i++) {
        id o = rt_array_at(req, i); long st = G(o)->state;
        if (st == GS_POSSIBLE && G(o)->active) return 1;
    }
    return 0;
}
static int blocked_by_other(id g) {
    id req = G(g)->requires;
    for (size_t i = 0; req && i < rt_array_count(req); i++) { long st = G(rt_array_at(req, i))->state; if (st == GS_BEGAN || st == GS_CHANGED || st == GS_ENDED) return 1; }
    return 0;
}
static void cancel_view_touches(void) {
    if (!seq_view || seq_view_cancelled) return;
    seq_view_cancelled = 1;
    id set = rt_set_new(&seq_touch, 1);
    ((void (*)(id, SEL, id, id))objc_msgSend)(seq_view, rt_sel("touchesCancelled:withEvent:"), set, seq_event);
    objc_release(set);
}
static void g_process(void) {
    if (!active_gestures) return;
    for (int round = 0; round < 8; round++) {
        int changed = 0;
        size_t n = rt_array_count(active_gestures);
        for (size_t i = 0; i < n; i++) {
            id g = rt_array_at(active_gestures, i); struct uigr *x = G(g);
            if (x->state == GS_BEGAN || x->state == GS_ENDED) {
                if (x->pending && x->state != GS_CHANGED && !x->waiting) {
                    /* transition out of Possible: delegate veto, failure requirements, exclusivity */
                    if (!should_begin(g) || blocked_by_other(g)) { x->state = GS_FAILED; x->pending = 0; changed = 1; continue; }
                    if (waits_for_other(g)) { x->waiting = x->state; x->state = GS_POSSIBLE; x->pending = 0; continue; }
                    for (size_t j = 0; j < n; j++) {
                        id o = rt_array_at(active_gestures, j);
                        if (o == g || simultaneous(g, o)) continue;
                        struct uigr *y = G(o);
                        if (y->state == GS_POSSIBLE && !y->waiting) { y->state = GS_FAILED; changed = 1; }
                        else if (y->state == GS_BEGAN || y->state == GS_CHANGED) { /* already recognized one wins */ }
                    }
                    if (x->cancels) cancel_view_touches();
                }
            }
            if (x->waiting && !waits_for_other(g)) {
                long target = x->waiting; x->waiting = 0;
                if (blocked_by_other(g)) x->state = GS_FAILED;
                else { x->state = target; x->pending = 1; if (x->cancels) cancel_view_touches(); }
                changed = 1;
            }
            if (x->pending) { x->pending = 0; send_actions(g); changed = 1; }
        }
        if (!changed) break;
    }
    /* recognizers in final states return to Possible once nothing is down */
    int any_down = 0;
    for (size_t i = 0; i < rt_array_count(active_gestures); i++) if (G(rt_array_at(active_gestures, i))->down) any_down = 1;
    if (!any_down) {
        int all_done = 1;
        for (size_t i = 0; i < rt_array_count(active_gestures); i++) {
            id g = rt_array_at(active_gestures, i); struct uigr *x = G(g);
            if (x->state == GS_ENDED || x->state == GS_CANCELLED || x->state == GS_FAILED) {
                x->state = GS_POSSIBLE; x->taps = 0; x->gen++; x->active = 0; x->waiting = 0;
                RT_SEND0(void, g, "reset");
            } else if (x->state == GS_POSSIBLE && (x->taps > 0 || x->waiting)) all_done = 0;
            else if (x->state == GS_POSSIBLE) { x->gen++; x->active = 0; }
            else all_done = 0;
        }
        if (all_done) RT_SEND0(void, active_gestures, "removeAllObjects");
    }
    rt_ui_mark_dirty();
}
static void collect_gestures(id view, id hit) {
    if (!active_gestures) active_gestures = mkarray();
    for (id v = view; v; v = V(v)->superview) {
        id gs = V(v)->gestures;
        for (size_t i = 0; gs && i < rt_array_count(gs); i++) {
            id g = rt_array_at(gs, i); struct uigr *x = G(g);
            if (!x->enabled) continue;
            /* controls keep their own taps: a tap recognizer above a UIControl doesn't fire */
            if (hit && v != hit && rt_is_kind_of(hit, RT_CLASS(UIControl)) && (x->kind == GK_TAP)) continue;
            id d = x->delegate;
            if (d && rt_responds(d, rt_sel("gestureRecognizer:shouldReceiveTouch:")) && !(RT_SEND2(long, d, "gestureRecognizer:shouldReceiveTouch:", id, g, id, seq_touch) & 0xff)) continue;
            int present = 0;
            for (size_t j = 0; j < rt_array_count(active_gestures); j++) if (rt_array_at(active_gestures, j) == g) present = 1;
            if (!present) { if (x->state != GS_POSSIBLE) { x->state = GS_POSSIBLE; x->taps = 0; } RT_SEND1(void, active_gestures, "addObject:", id, g); }
            x->active = 1;
        }
    }
}
/* Touch "slop": a resting finger on Android produces a stream of ACTION_MOVE events
   (same or jittering position). iOS reports a stationary finger without touchesMoved,
   and apps' own gesture logic (tap vs. drag vs. hold) depends on that. Moves are held
   back until the touch leaves a small radius around where it began; a touch that ends
   inside it ends where it began. Moves to the same position are never delivered. */
#define TOUCH_SLOP_PT 2.0
static CGPoint seq_start; static int seq_left_slop;
static void deliver_touch(struct touch_ev *e) {
    struct uiapp *a = (struct uiapp *)g_app;
    if (!a || a->ignoring) return;
    double now = rt_now();
    if (e->phase == 0) {
        /* topmost visible window that contains the point (by level, then order) */
        id win = NULL; double best = -1e18;
        for (size_t i = 0; i < rt_array_count(a->windows); i++) {
            id w = rt_array_at(a->windows, i);
            if (v_hidden(w, NULL) || W(w)->hiddenWin) continue;
            if (W(w)->level >= best) { best = W(w)->level; win = w; }
        }
        if (!win) win = a->keyWindow;
        if (!win) return;
        objc_release(seq_touch); objc_release(seq_event);
        seq_touch = rt_alloc_instance(RT_CLASS(UITouch), 0);
        struct uitouch *t = TO(seq_touch);
        t->pt = t->prev = (CGPoint){ e->x, e->y }; t->phase = 0; t->ts = now; t->tapCount = 1; t->window = win; t->tid = e->tid;
        seq_start = t->pt; seq_left_slop = 0;
        seq_event = rt_alloc_instance(RT_CLASS(UIEvent), 0);
        ((struct uievent *)seq_event)->touches = rt_set_new(&seq_touch, 1); ((struct uievent *)seq_event)->ts = now;
        CGPoint wp = rt_view_convert(NULL, t->pt, win);
        id hit = ((id (*)(id, SEL, CGPoint, id))objc_msgSend)(win, rt_sel("hitTest:withEvent:"), wp, seq_event);
        objc_release(seq_view); seq_view = objc_retain(hit); seq_view_cancelled = 0;
        t->view = hit;
        if (!hit) return;
        collect_gestures(hit, hit);
        t->gestures = objc_retain(RT_SEND0(id, active_gestures, "copy"));
        id set = rt_set_new(&seq_touch, 1);
        for (size_t i = 0; i < rt_array_count(active_gestures); i++) {
            id g = rt_array_at(active_gestures, i);
            if (G(g)->state == GS_POSSIBLE || G(g)->state == GS_BEGAN || G(g)->state == GS_CHANGED) ((void (*)(id, SEL, id, id))objc_msgSend)(g, rt_sel("touchesBegan:withEvent:"), set, seq_event);
        }
        g_process();
        if (!seq_view_cancelled) ((void (*)(id, SEL, id, id))objc_msgSend)(hit, rt_sel("touchesBegan:withEvent:"), set, seq_event);
        objc_release(set);
    } else {
        if (!seq_touch || !seq_view) return;
        struct uitouch *t = TO(seq_touch);
        CGPoint np = { e->x, e->y };
        if (!seq_left_slop) {
            if (hypot(np.x - seq_start.x, np.y - seq_start.y) < TOUCH_SLOP_PT) {
                if (e->phase == 1) return;   /* still resting */
                np = seq_start;              /* a tap ends where it began */
            } else seq_left_slop = 1;
        }
        if (e->phase == 1 && fabs(np.x - t->pt.x) < 0.01 && fabs(np.y - t->pt.y) < 0.01) return;
        t->prev = t->pt; t->pt = np; t->ts = now; t->phase = e->phase == 1 ? 1 : 3;
        ((struct uievent *)seq_event)->ts = now;
        id set = rt_set_new(&seq_touch, 1);
        const char *sel = e->phase == 1 ? "touchesMoved:withEvent:" : "touchesEnded:withEvent:";
        id gs = active_gestures ? RT_SEND0(id, active_gestures, "copy") : NULL;
        for (size_t i = 0; gs && i < rt_array_count(gs); i++) {
            id g = rt_array_at(gs, i); long st = G(g)->state;
            if (st == GS_POSSIBLE || st == GS_BEGAN || st == GS_CHANGED) ((void (*)(id, SEL, id, id))objc_msgSend)(g, rt_sel(sel), set, seq_event);
            if (e->phase != 1) G(g)->down = 0;
        }
        objc_release(gs);
        g_process();
        if (!seq_view_cancelled) ((void (*)(id, SEL, id, id))objc_msgSend)(seq_view, rt_sel(sel), set, seq_event);
        objc_release(set);
        if (e->phase != 1) { objc_release(seq_view); seq_view = NULL; g_process(); }
    }
    rt_ui_mark_dirty();
}

/* ========================================================= app main loop */
int rt_controls_continuous(void);
static Class find_delegate_class(void);
/* scripted input for headless tests: IOSRT_SCRIPT="5:tap:100,200;9:quit" (also down/move/up/drag:x,y,x2,y2, wait:ms) */
static void run_script(unsigned frame) {
    const char *s = getenv("IOSRT_SCRIPT");
    if (!s) return;
    char *dup = rt_xstrdup(s), *save = NULL;
    for (char *tok = strtok_r(dup, ";", &save); tok; tok = strtok_r(NULL, ";", &save)) {
        unsigned f; char cmd[16]; float x = 0, y = 0, x2 = 0, y2 = 0;
        int n = sscanf(tok, "%u:%15[a-z]:%f,%f,%f,%f", &f, cmd, &x, &y, &x2, &y2);
        if (n < 2 || f != frame) continue;
        if (!strcmp(cmd, "tap")) { iosrt_push_touch(0, x, y); iosrt_push_touch(2, x, y); }
        else if (!strcmp(cmd, "down")) iosrt_push_touch(0, x, y);
        else if (!strcmp(cmd, "move")) iosrt_push_touch(1, x, y);
        else if (!strcmp(cmd, "up")) iosrt_push_touch(2, x, y);
        else if (!strcmp(cmd, "drag")) { iosrt_push_touch(0, x, y); for (int i = 1; i <= 8; i++) iosrt_push_touch(1, x + (x2 - x) * i / 8, y + (y2 - y) * i / 8); iosrt_push_touch(2, x2, y2); }
        else if (!strcmp(cmd, "quit")) rt_quit_requested = 1;
        else if (!strcmp(cmd, "wait")) usleep((useconds_t)(x * 1000));   /* N:wait:ms - real time passes (long presses, timers) */
    }
    free(dup);
}
static void dump_view(id v, int depth) {
    CGRect f = rt_view_frame(v), b = rt_view_bounds(v);
    char line[512];
    int n = snprintf(line, sizeof line, "%*s%s frame=(%g,%g %gx%g) bounds.origin=(%g,%g) mask=%ld%s%s%s\n", depth * 2, "", rt_class_name(v->isa),
                     f.origin.x, f.origin.y, f.size.width, f.size.height, b.origin.x, b.origin.y, V(v)->autoresizing,
                     v_hidden(v, NULL) ? " hidden" : "", V(v)->vc ? " vc=" : "", V(v)->vc ? rt_class_name(V(v)->vc->isa) : "");
    rt_out(2, line, (size_t)n);
    for (size_t i = 0; V(v)->subviews && i < rt_array_count(V(v)->subviews); i++) dump_view(rt_array_at(V(v)->subviews, i), depth + 1);
}
static struct cgctx *g_screen_ctx;
static char *last_dump;
static int window_cmp(const void *a, const void *b) { double x = W(*(id *)a)->level, y = W(*(id *)b)->level; return x < y ? -1 : x > y; }
static void render_frame(unsigned frame, int want_trace) {
    int pw = (int)lroundf(rt_scr_w * rt_scr_scale), ph = (int)lroundf(rt_scr_h * rt_scr_scale);
    struct gfx_surface *surf = g_screen_ctx ? rt_cg_surface(g_screen_ctx) : NULL;
    if (!surf || surf->w != pw || surf->h != ph) { objc_release((id)g_screen_ctx); g_screen_ctx = rt_cg_bitmap_new(pw, ph, rt_scr_scale, 1); surf = rt_cg_surface(g_screen_ctx); }
    for (size_t i = 0; i < (size_t)pw * ph; i++) surf->px[i] = 0xff000000u;   /* opaque black behind everything */
    rt_sb sb; rt_sb_init(&sb);
    rt_cg_set_trace(g_screen_ctx, want_trace ? &sb : NULL, rt_scr_scale);
    if (want_trace) rt_sb_printf(&sb, "screen %gx%g\n", rt_scr_w, rt_scr_h);
    rt_layers_begin_frame();
    struct uiapp *a = (struct uiapp *)g_app;
    size_t n = rt_array_count(a->windows);
    id *ws = malloc(sizeof(id) * (n + 1)); size_t k = 0;
    for (size_t i = 0; i < n; i++) { id w = rt_array_at(a->windows, i); if (!W(w)->hiddenWin && !v_hidden(w, NULL)) ws[k++] = w; }
    qsort(ws, k, sizeof(id), window_cmp);
    rt_cg_push(g_screen_ctx);
    for (size_t i = 0; i < k; i++) rt_layer_render(V(ws[i])->layer, g_screen_ctx);
    rt_cg_pop();
    free(ws);
    rt_cg_set_trace(g_screen_ctx, NULL, rt_scr_scale);
    if (want_trace) {
        if (!last_dump || strcmp(last_dump, sb.buf ? sb.buf : "")) {
            if (getenv("IOSRT_DUMP_FRAMES")) { char h[64]; int m = snprintf(h, sizeof h, "--- frame %u ---\n", frame); rt_out(1, h, (size_t)m); rt_out(1, sb.buf, sb.len); }
            free(last_dump); last_dump = rt_sb_take(&sb);
        } else free(sb.buf);
    }
    if (rt_plat->present_frame) rt_plat->present_frame(surf->px, surf->w, surf->h, surf->stride);
}
static void finish_launch(id app) {
    struct uiapp *a = (struct uiapp *)app;
    /* iOS 13- apps without a storyboard must create their window; ensure the key window is shown */
    if (!a->keyWindow && rt_array_count(a->windows)) w_makeKeyAndVisible(rt_array_at(a->windows, 0), NULL);
    id win = a->keyWindow;
    if (win) {
        if (W(win)->root && !((struct uivc *)W(win)->root)->appeared) rt_vc_appear(W(win)->root, 1);
    }
}
/* iOS compatibility ("zoomed") mode: an app without a launch storyboard only
 * gets the largest iPhone screen it ships a launch image for (else 320x480);
 * the system scales that up to the display. */
static void choose_compat_screen(void) {
    if (getenv("IOSRT_NO_COMPAT_SCREEN")) return;
    id bundle = RT_SEND0(id, (id)rt_class_named("NSBundle"), "mainBundle");
    id info = bundle ? RT_SEND0(id, bundle, "infoDictionary") : NULL;
    if (!info || rt_dict_get(info, rt_str("UILaunchStoryboardName")) || rt_dict_get(info, rt_str("UILaunchStoryboardName~iphone"))) return;
    const char *bdir = rt_str_utf8(RT_SEND0(id, bundle, "bundlePath"));
    char plist[4096]; snprintf(plist, sizeof plist, "%s/Info.plist", bdir);
    if (access(plist, F_OK) != 0) return;          /* a bare Mach-O: no launch metadata to go by */
    double bw = 320, bh = 480;
    id imgs = rt_dict_get(info, rt_str("UILaunchImages"));
    for (size_t i = 0; imgs && i < rt_array_count(imgs); i++) {
        id e = rt_array_at(imgs, i);
        id sz = rt_dict_get(e, rt_str("UILaunchImageSize")), orient = rt_dict_get(e, rt_str("UILaunchImageOrientation"));
        if (!sz || (orient && strcmp(rt_str_utf8(orient), "Portrait"))) continue;
        double w = 0, h = 0; sscanf(rt_str_utf8(sz), "{%lf, %lf}", &w, &h);
        if (w >= 768) continue;                     /* iPad images */
        if (h > bh || (h == bh && w > bw)) { bw = w; bh = h; }
    }
    const char *dir = rt_str_utf8(RT_SEND0(id, bundle, "bundlePath"));
    char p[4096]; snprintf(p, sizeof p, "%s/Default-568h@2x.png", dir);
    if (bh < 568 && access(p, F_OK) == 0) { bw = 320; bh = 568; }
    if (bw >= rt_scr_w && bh >= rt_scr_h) return;
    rt_log("compatibility screen %gx%g pt (no launch storyboard), scaled to the display", bw, bh);
    rt_scr_w = (float)bw; rt_scr_h = (float)bh; rt_scr_scale = 2;
}
id g_main_nib_objects;   /* the main nib's top-level objects, kept for the app's lifetime */
int iosrt_UIApplicationMain(int argc, char **argv, id principal, id delegateName) {
    (void)argc; (void)argv;
    void *pool = objc_autoreleasePoolPush();
    choose_compat_screen();
    Class appc = principal ? rt_class_named(rt_str_utf8(principal)) : NULL;
    id app = app_shared(appc ? appc : RT_CLASS(UIApplication), NULL);
    Class dc = delegateName ? rt_class_named(rt_str_utf8(delegateName)) : find_delegate_class();
    id delegate = NULL;
    /* UIMainStoryboardFile (device-specific key first) */
    id info = RT_SEND0(id, RT_SEND0(id, (id)rt_class_named("NSBundle"), "mainBundle"), "infoDictionary");
    id sbName = info ? rt_dict_get(info, rt_str("UIMainStoryboardFile~iphone")) : NULL;
    if (!sbName && info) sbName = rt_dict_get(info, rt_str("UIMainStoryboardFile"));
    if (dc) delegate = RT_SEND0(id, RT_SEND0(id, (id)dc, "alloc"), "init");
    else if (sbName) delegate = RT_SEND0(id, RT_SEND0(id, (id)RT_CLASS(NSObject), "alloc"), "init");
    else {
        /* storyboard/NIB launch: NSMainNibFile loads the window and delegate (outlets wire it up) */
        id nibName = RT_SEND1(id, RT_SEND0(id, (id)rt_class_named("NSBundle"), "mainBundle"), "objectForInfoDictionaryKey:", id, rt_str("NSMainNibFile"));
        const char *path = nibName ? rt_nib_find(rt_str_utf8(nibName), NULL) : NULL;
        /* like UIKit, keep the main nib's top-level objects (the delegate among them) for the app's lifetime */
        if (path) g_main_nib_objects = objc_retain(rt_nib_instantiate(path, app, NULL));
        delegate = ((struct uiapp *)app)->delegate;
        if (!delegate) rt_guest_exit(75, "UIApplicationMain: no app delegate class (storyboards are not supported)");
    }
    app_setDelegate(app, NULL, delegate);
    rt_log("UIApplicationMain: delegate %s", rt_class_name(delegate->isa));
    /* storyboard launch, like UIKit: the initial controller goes into the delegate's window
       (created here if the delegate has none) before application:didFinishLaunching... */
    static id sb_window;
    if (sbName) {
        id sb = RT_SEND2(id, (id)rt_class_named("UIStoryboard"), "storyboardWithName:bundle:", id, sbName, id, NULL);
        id root = sb ? RT_SEND0(id, sb, "instantiateInitialViewController") : NULL;
        if (root) {
            id win = rt_responds(delegate, rt_sel("window")) ? RT_SEND0(id, delegate, "window") : NULL;
            if (!win) {
                win = objc_autorelease(RT_SEND1(id, RT_SEND0(id, (id)RT_CLASS(UIWindow), "alloc"), "initWithFrame:", CGRect, ((CGRect){ { 0, 0 }, { rt_scr_w, rt_scr_h } })));
                if (rt_responds(delegate, rt_sel("setWindow:"))) RT_SEND1(void, delegate, "setWindow:", id, win);
            }
            sb_window = objc_retain(win);
            /* like UIKit, the root's view loads when the window is made visible (after didFinishLaunching) */
            objc_retain(root); objc_release(W(win)->root); W(win)->root = root;
        }
    }
    if (rt_responds(delegate, rt_sel("application:willFinishLaunchingWithOptions:")))
        RT_SEND2(long, delegate, "application:willFinishLaunchingWithOptions:", id, app, id, NULL);
    if (rt_responds(delegate, rt_sel("application:didFinishLaunchingWithOptions:")))
        RT_SEND2(long, delegate, "application:didFinishLaunchingWithOptions:", id, app, id, NULL);
    else if (rt_responds(delegate, rt_sel("applicationDidFinishLaunching:")))
        RT_SEND1(void, delegate, "applicationDidFinishLaunching:", id, app);
    id nc = RT_SEND0(id, (id)rt_class_named("NSNotificationCenter"), "defaultCenter");
    extern id iosrt_UIApplicationDidFinishLaunchingNotification, iosrt_UIApplicationDidBecomeActiveNotification;
    RT_SEND2(void, nc, "postNotificationName:object:", id, iosrt_UIApplicationDidFinishLaunchingNotification, id, app);
    if (sb_window && !((struct uiapp *)app)->keyWindow) RT_SEND0(void, sb_window, "makeKeyAndVisible");
    finish_launch(app);
    if (rt_responds(delegate, rt_sel("applicationDidBecomeActive:")))
        RT_SEND1(void, delegate, "applicationDidBecomeActive:", id, app);
    RT_SEND2(void, nc, "postNotificationName:object:", id, iosrt_UIApplicationDidBecomeActiveNotification, id, app);
    objc_autoreleasePoolPop(pool);

    const char *hf = getenv("IOSRT_HEADLESS_FRAMES");
    unsigned max_frames = hf ? (unsigned)atoi(hf) : 0;
    int want_trace = max_frames || getenv("IOSRT_DUMP_FRAMES");
    int last_continuous = 0;
    for (unsigned frame = 1; !rt_quit_requested; frame++) {
        pool = objc_autoreleasePoolPush();
        run_script(frame);
        for (;;) {
            pthread_mutex_lock(&input_lock);
            int has = tq_head != tq_tail; struct touch_ev e = touchq[tq_head];
            if (has) tq_head = (tq_head + 1) % 512;
            pthread_mutex_unlock(&input_lock);
            if (!has) break;
            void *p2 = objc_autoreleasePoolPush();
            deliver_touch(&e);
            objc_autoreleasePoolPop(p2);
        }
        rt_display_links_tick();
        int busy = rt_display_links_active() || last_continuous;
        rt_runloop_run_once(max_frames || busy ? (max_frames ? 0 : 4) : 16);
        struct uiapp *a = (struct uiapp *)app;
        for (size_t i = 0; i < rt_array_count(a->windows); i++) layout_tree(rt_array_at(a->windows, i));
        if (dirty || busy) {
            dirty = 0;
            render_frame(frame, want_trace);
            last_continuous = rt_controls_continuous();
        }
        objc_autoreleasePoolPop(pool);
        if (max_frames && frame >= max_frames) break;
        if (max_frames) usleep(1000);
    }
    if (max_frames && last_dump && !getenv("IOSRT_DUMP_FRAMES")) rt_out(1, last_dump, strlen(last_dump));
    /* debugging aid: IOSRT_DUMP_CLASS=A,B lists the methods each class implements itself */
    if (getenv("IOSRT_DUMP_CLASS")) {
        extern void **class_copyMethodList(Class c, unsigned *out); extern SEL method_getName(void *m);
        char buf[512]; snprintf(buf, sizeof buf, "%s", getenv("IOSRT_DUMP_CLASS"));
        for (char *save = NULL, *nm = strtok_r(buf, ",", &save); nm; nm = strtok_r(NULL, ",", &save)) {
            Class c = rt_class_named(nm);
            for (int meta = 0; c && meta < 2; meta++) {
                unsigned n = 0; void **ms = class_copyMethodList(meta ? c->isa : c, &n);
                for (unsigned i = 0; i < n; i++) { char line[300]; int k = snprintf(line, sizeof line, "%c[%s %s]\n", meta ? '+' : '-', nm, (const char *)method_getName(ms[i])); rt_out(2, line, (size_t)k); }
                free(ms);
            }
        }
    }
    if (getenv("IOSRT_DUMP_VIEWS")) {
        struct uiapp *a = (struct uiapp *)app;
        for (size_t i = 0; i < rt_array_count(a->windows); i++) dump_view(rt_array_at(a->windows, i), 0);
    }
    const char *png = getenv("IOSRT_PNG_OUT");
    if (png && g_screen_ctx) {
        struct gfx_surface *s = rt_cg_surface(g_screen_ctx);
        struct gfx_image im = { s->w, s->h, s->px, 1 };
        gfx_image_write_png(&im, png);
    }
    struct uiapp *a = (struct uiapp *)app;
    if (a->delegate && rt_responds(a->delegate, rt_sel("applicationWillTerminate:")))
        RT_SEND1(void, a->delegate, "applicationWillTerminate:", id, app);
    rt_guest_exit(0, NULL);
}

/* find a class whose name ends in "AppDelegate" (storyboard-less heuristic) */
Class rt_find_class_with_suffix(const char *suffix);
static Class find_delegate_class(void) { return rt_find_class_with_suffix("AppDelegate"); }

id iosrt_UIApplicationDidFinishLaunchingNotification, iosrt_UIApplicationDidBecomeActiveNotification,
   iosrt_UIApplicationWillResignActiveNotification, iosrt_UIApplicationDidEnterBackgroundNotification,
   iosrt_UIApplicationWillEnterForegroundNotification, iosrt_UIApplicationWillTerminateNotification,
   iosrt_UIApplicationDidReceiveMemoryWarningNotification, iosrt_UIApplicationDidChangeStatusBarFrameNotification,
   iosrt_UIApplicationWillChangeStatusBarFrameNotification, iosrt_UIApplicationDidChangeStatusBarOrientationNotification,
   iosrt_UIApplicationStatusBarFrameUserInfoKey, iosrt_UIApplicationLaunchOptionsURLKey, iosrt_UIApplicationLaunchOptionsSourceApplicationKey,
   iosrt_UIApplicationLaunchOptionsAnnotationKey, iosrt_UIApplicationLaunchOptionsRemoteNotificationKey, iosrt_UIDeviceOrientationDidChangeNotification,
   iosrt_UIKeyboardWillShowNotification, iosrt_UIKeyboardDidShowNotification, iosrt_UIKeyboardWillHideNotification, iosrt_UIKeyboardDidHideNotification,
   iosrt_UIKeyboardFrameEndUserInfoKey, iosrt_UIKeyboardFrameBeginUserInfoKey, iosrt_UIKeyboardAnimationDurationUserInfoKey, iosrt_UIKeyboardAnimationCurveUserInfoKey,
   iosrt_UITextViewTextDidChangeNotification, iosrt_UITextFieldTextDidChangeNotification, iosrt_UIMenuControllerWillHideMenuNotification,
   iosrt_UIPasteboardNameGeneral, iosrt_UIPasteboardNameFind;
unsigned long iosrt_UIBackgroundTaskInvalid = 0;
double iosrt_UIWindowLevelNormal = 0, iosrt_UIWindowLevelAlert = 2000, iosrt_UIWindowLevelStatusBar = 1000;
double iosrt_UIViewNoIntrinsicMetric = -1;
UIEdgeInsets iosrt_UIEdgeInsetsZero;
double iosrt_UIFloatRangeZero[2];
CGSize iosrt_UILayoutFittingCompressedSize, iosrt_UILayoutFittingExpandedSize = { 1e5, 1e5 };
/* accessibility: notifications are uint32 constants */
uint32_t iosrt_UIAccessibilityAnnouncementNotification = 1008, iosrt_UIAccessibilityLayoutChangedNotification = 1001,
         iosrt_UIAccessibilityScreenChangedNotification = 1000, iosrt_UIAccessibilityPageScrolledNotification = 1009;
uint64_t iosrt_UIAccessibilityTraitButton = 1, iosrt_UIAccessibilityTraitLink = 2, iosrt_UIAccessibilityTraitImage = 4, iosrt_UIAccessibilityTraitSelected = 8,
         iosrt_UIAccessibilityTraitStaticText = 64, iosrt_UIAccessibilityTraitAdjustable = 4096, iosrt_UIAccessibilityTraitAllowsDirectInteraction = 8192,
         iosrt_UIAccessibilityTraitNone = 0, iosrt_UIAccessibilityTraitHeader = 65536;
void iosrt_UIAccessibilityPostNotification(uint32_t n, id arg) { (void)n; (void)arg; }
rt_bool iosrt_UIAccessibilityIsVoiceOverRunning(void) { return 0; }
rt_bool iosrt_UIAccessibilityIsReduceMotionEnabled(void) { return 0; }
id iosrt_UIActivityTypePostToFacebook, iosrt_UIActivityTypePostToTwitter, iosrt_UIActivityTypePostToVimeo, iosrt_UIActivityTypeSaveToCameraRoll,
   iosrt_UIActivityTypeMail, iosrt_UIActivityTypeMessage, iosrt_UIActivityTypeCopyToPasteboard, iosrt_UIActivityTypeAirDrop,
   iosrt_UIImagePickerControllerOriginalImage, iosrt_UIImagePickerControllerCropRect, iosrt_UIImagePickerControllerEditedImage, iosrt_UIImagePickerControllerMediaURL;

void rt_uikit_init(void) {
    Class o = RT_CLASS(NSObject);
    rt_cg_init();
    NEWCLASS(UIColor, o, sizeof(struct uicolor) - sizeof(void *));
    Class c = RT_CLASS(UIColor);
    CM(c, "colorWithRed:green:blue:alpha:", c_rgba); CM(c, "colorWithWhite:alpha:", c_white); CM(c, "colorWithHue:saturation:brightness:alpha:", c_hsb);
    CM(c, "colorWithDisplayP3Red:green:blue:alpha:", c_rgba); CM(c, "colorWithCGColor:", c_withCG); CM(c, "colorWithPatternImage:", c_pattern);
    IM(c, "initWithRed:green:blue:alpha:", c_init_rgba); IM(c, "initWithWhite:alpha:", c_init_white); IM(c, "initWithHue:saturation:brightness:alpha:", c_init_hsb);
    IM(c, "initWithCGColor:", c_initWithCG); IM(c, "initWithPatternImage:", c_initPattern); IM(c, "dealloc", c_dealloc);
    CM(c, "whiteColor", c_whiteColor); CM(c, "blackColor", c_blackColor); CM(c, "redColor", c_redColor);
    CM(c, "greenColor", c_greenColor); CM(c, "blueColor", c_blueColor); CM(c, "yellowColor", c_yellowColor);
    CM(c, "orangeColor", c_orangeColor); CM(c, "purpleColor", c_purpleColor); CM(c, "grayColor", c_grayColor);
    CM(c, "darkGrayColor", c_darkGrayColor); CM(c, "lightGrayColor", c_lightGrayColor); CM(c, "clearColor", c_clearColor);
    CM(c, "cyanColor", c_cyanColor); CM(c, "magentaColor", c_magentaColor); CM(c, "brownColor", c_brownColor);
    CM(c, "systemBlueColor", c_systemBlue); CM(c, "systemRedColor", c_systemRed); CM(c, "systemGreenColor", c_systemGreen);
    CM(c, "groupTableViewBackgroundColor", c_groupTable); CM(c, "lightTextColor", c_lightText); CM(c, "darkTextColor", c_darkText);
    CM(c, "labelColor", c_label); CM(c, "secondaryLabelColor", c_secondaryLabel); CM(c, "systemGrayColor", c_systemGray);
    CM(c, "separatorColor", c_separator); CM(c, "systemBackgroundColor", c_whiteColor);
    IM(c, "colorWithAlphaComponent:", c_withAlpha); IM(c, "getRed:green:blue:alpha:", c_getRGBA); IM(c, "getWhite:alpha:", c_getWhite);
    IM(c, "getHue:saturation:brightness:alpha:", c_getHSB); IM(c, "CGColor", c_CGColor); IM(c, "CIColor", c_self);
    IM(c, "set", c_set); IM(c, "setFill", c_setFill); IM(c, "setStroke", c_setStroke); IM(c, "description", c_description);
    IM(c, "isEqual:", c_isEqual); IM(c, "hash", c_hash); IM(c, "copyWithZone:", c_copy); IM(c, "copy", c_copy);
    rt_uiimage_init();
    rt_text_init();
    { void rt_coretext_init(void); rt_coretext_init(); }
    rt_layers_init();
    { void rt_eagl_init(void); rt_eagl_init(); }
    { void rt_motion_init(void); rt_motion_init(); }

    NEWCLASS(UIResponder, o, 0);
    Class r = RT_CLASS(UIResponder);
    IM(r, "touchesBegan:withEvent:", r_touches); IM(r, "touchesMoved:withEvent:", r_touches);
    IM(r, "touchesEnded:withEvent:", r_touches); IM(r, "touchesCancelled:withEvent:", r_touches);
    IM(r, "nextResponder", r_nil); IM(r, "becomeFirstResponder", r_yes); IM(r, "resignFirstResponder", r_yes);
    IM(r, "canBecomeFirstResponder", r_no); IM(r, "canResignFirstResponder", r_yes); IM(r, "isFirstResponder", r_no);
    IM(r, "canPerformAction:withSender:", r_canPerform); IM(r, "targetForAction:withSender:", r_targetForAction);
    IM(r, "motionBegan:withEvent:", r_noop2); IM(r, "motionEnded:withEvent:", r_noop2); IM(r, "remoteControlReceivedWithEvent:", r_noop1);
    IM(r, "inputView", r_nil); IM(r, "inputAccessoryView", r_nil); IM(r, "undoManager", r_nil); IM(r, "reloadInputViews", v_noop);
    IM(r, "pressesBegan:withEvent:", r_noop2); IM(r, "setInputView:", r_noop1); IM(r, "setInputAccessoryView:", r_noop1); IM(r, "pressesEnded:withEvent:", r_noop2);

    NEWCLASS(UIApplication, r, sizeof(struct uiapp) - sizeof(void *));
    Class ap = RT_CLASS(UIApplication);
    CM(ap, "sharedApplication", app_shared); IM(ap, "delegate", app_delegate); IM(ap, "setDelegate:", app_setDelegate);
    IM(ap, "keyWindow", app_keyWindow); IM(ap, "windows", app_windows); IM(ap, "applicationState", app_state);
    IM(ap, "statusBarOrientation", app_orientation); IM(ap, "openURL:", app_openURL); IM(ap, "openURL:options:completionHandler:", app_openURLopts);
    IM(ap, "canOpenURL:", app_canOpenURL); IM(ap, "setIdleTimerDisabled:", app_setIdle); IM(ap, "isIdleTimerDisabled", app_idle);
    IM(ap, "setNetworkActivityIndicatorVisible:", v_noopL); IM(ap, "isNetworkActivityIndicatorVisible", app_zero);
    IM(ap, "isStatusBarHidden", app_statusHidden); IM(ap, "setStatusBarHidden:", app_setStatusHidden); IM(ap, "setStatusBarHidden:withAnimation:", app_setStatusHidden2);
    IM(ap, "statusBarStyle", app_statusStyle); IM(ap, "setStatusBarStyle:", app_setStatusStyle); IM(ap, "setStatusBarStyle:animated:", app_setStatusStyle2);
    IM(ap, "statusBarFrame", app_statusFrame);
    IM(ap, "registerForRemoteNotifications", v_noop); IM(ap, "unregisterForRemoteNotifications", v_noop);
    IM(ap, "beginReceivingRemoteControlEvents", v_noop); IM(ap, "endReceivingRemoteControlEvents", v_noop);
    IM(ap, "applicationIconBadgeNumber", app_badge); IM(ap, "setApplicationIconBadgeNumber:", app_setBadge);
    IM(ap, "isIgnoringInteractionEvents", app_ignoring); IM(ap, "beginIgnoringInteractionEvents", app_beginIgnore); IM(ap, "endIgnoringInteractionEvents", app_endIgnore);
    IM(ap, "registerUserNotificationSettings:", v_noop1); IM(ap, "setMinimumBackgroundFetchInterval:", v_noopD);
    IM(ap, "isRegisteredForRemoteNotifications", app_zero); IM(ap, "backgroundTimeRemaining", app_bgTime);
    IM(ap, "beginBackgroundTaskWithExpirationHandler:", app_beginBg); IM(ap, "beginBackgroundTaskWithName:expirationHandler:", app_beginBgNamed);
    IM(ap, "endBackgroundTask:", app_endBg); IM(ap, "sendAction:to:from:forEvent:", app_sendAction); IM(ap, "nextResponder", app_nextResponder);
    IM(ap, "preferredContentSizeCategory", app_preferredLanguage); IM(ap, "cancelAllLocalNotifications", v_noop);
    IM(ap, "scheduleLocalNotification:", v_noop1); IM(ap, "presentLocalNotificationNow:", v_noop1); IM(ap, "setShortcutItems:", v_noop1);

    NEWCLASS(UIScreen, o, 0);
    Class sc = RT_CLASS(UIScreen);
    CM(sc, "mainScreen", sc_main); CM(sc, "screens", sc_screens); IM(sc, "bounds", sc_bounds);
    IM(sc, "applicationFrame", sc_bounds); IM(sc, "nativeBounds", sc_nativeBounds);
    IM(sc, "scale", sc_scale); IM(sc, "nativeScale", sc_scale); IM(sc, "brightness", sc_brightness);
    IM(sc, "maximumFramesPerSecond", sc_fps); IM(sc, "displayLinkWithTarget:selector:", sc_displayLink); IM(sc, "setBrightness:", v_noopD);
    NEWCLASS(UIDevice, o, 0);
    Class dv = RT_CLASS(UIDevice);
    CM(dv, "currentDevice", dev_current); IM(dv, "systemVersion", dev_sysver); IM(dv, "systemName", dev_sysname);
    IM(dv, "model", dev_model); IM(dv, "localizedModel", dev_model); IM(dv, "name", dev_name);
    IM(dv, "userInterfaceIdiom", dev_idiom); IM(dv, "orientation", dev_orientation); IM(dv, "batteryLevel", dev_battery);
    IM(dv, "batteryState", dev_batteryState); IM(dv, "identifierForVendor", dev_vendorId); IM(dv, "isMultitaskingSupported", dev_multitask);
    IM(dv, "beginGeneratingDeviceOrientationNotifications", v_noop); IM(dv, "endGeneratingDeviceOrientationNotifications", v_noop);
    IM(dv, "setBatteryMonitoringEnabled:", v_noopL); IM(dv, "setProximityMonitoringEnabled:", v_noopL); IM(dv, "playInputClick", v_noop);

    NEWCLASS(UIView, r, sizeof(struct uiview) - sizeof(void *));
    Class v = RT_CLASS(UIView);
    CM(v, "layerClass", v_layerClass);
    IM(v, "initWithFrame:", v_initWithFrame); IM(v, "init", v_init); IM(v, "dealloc", v_dealloc); IM(v, "layer", v_layer);
    IM(v, "frame", v_frame); IM(v, "setFrame:", v_setFrame); IM(v, "bounds", v_bounds); IM(v, "setBounds:", v_setBounds);
    IM(v, "center", v_center); IM(v, "setCenter:", v_setCenter); IM(v, "transform", v_transform); IM(v, "setTransform:", v_setTransform);
    IM(v, "backgroundColor", v_bg); IM(v, "setBackgroundColor:", v_setBg);
    IM(v, "subviews", v_subviews); IM(v, "superview", v_superview); IM(v, "window", v_window);
    IM(v, "addSubview:", v_addSubview); IM(v, "insertSubview:atIndex:", v_insertSubviewAt);
    IM(v, "insertSubview:belowSubview:", v_insertBelow); IM(v, "insertSubview:aboveSubview:", v_insertAbove);
    IM(v, "removeFromSuperview", v_removeFromSuperview); IM(v, "bringSubviewToFront:", v_bringToFront); IM(v, "sendSubviewToBack:", v_sendToBack);
    IM(v, "exchangeSubviewAtIndex:withSubviewAtIndex:", v_exchange); IM(v, "isDescendantOfView:", v_isDescendant);
    IM(v, "alpha", v_alpha); IM(v, "setAlpha:", v_setAlpha); IM(v, "isHidden", v_hidden); IM(v, "setHidden:", v_setHidden);
    IM(v, "clipsToBounds", v_clips); IM(v, "setClipsToBounds:", v_setClips);
    IM(v, "tag", v_tag); IM(v, "setTag:", v_setTag); IM(v, "viewWithTag:", v_viewWithTag);
    IM(v, "isUserInteractionEnabled", v_interaction); IM(v, "setUserInteractionEnabled:", v_setInteraction);
    IM(v, "isMultipleTouchEnabled", v_multitouch); IM(v, "setMultipleTouchEnabled:", v_setMultitouch);
    IM(v, "isExclusiveTouch", v_exclusive); IM(v, "setExclusiveTouch:", v_setExclusive);
    IM(v, "autoresizingMask", v_autoresizing); IM(v, "setAutoresizingMask:", v_setAutoresizing);
    IM(v, "autoresizesSubviews", v_autoresizesSubviews); IM(v, "setAutoresizesSubviews:", v_setAutoresizesSubviews);
    IM(v, "contentMode", v_contentMode); IM(v, "setContentMode:", v_setContentMode);
    IM(v, "tintColor", v_tint); IM(v, "setTintColor:", v_setTint); IM(v, "tintAdjustmentMode", v_tintMode); IM(v, "setTintAdjustmentMode:", v_setTintMode);
    IM(v, "tintColorDidChange", v_noop);
    IM(v, "setNeedsDisplay", v_setNeedsDisplay); IM(v, "setNeedsDisplayInRect:", v_setNeedsDisplayInRect);
    IM(v, "setNeedsLayout", v_setNeedsLayout); IM(v, "layoutIfNeeded", v_layoutIfNeeded);
    IM(v, "layoutSubviews", v_noop); IM(v, "drawRect:", v_drawRect); IM(v, "sizeToFit", v_sizeToFit); IM(v, "sizeThatFits:", v_sizeThatFits);
    IM(v, "intrinsicContentSize", v_intrinsic); IM(v, "invalidateIntrinsicContentSize", v_noop);
    IM(v, "hitTest:withEvent:", v_hitTest); IM(v, "pointInside:withEvent:", v_pointInside);
    IM(v, "convertPoint:fromView:", v_convertPointFromView); IM(v, "convertPoint:toView:", v_convertPointToView);
    IM(v, "convertRect:fromView:", v_convertRectFromView); IM(v, "convertRect:toView:", v_convertRectToView);
    IM(v, "nextResponder", v_nextResponder); IM(v, "didMoveToSuperview", v_noop); IM(v, "didMoveToWindow", v_noop);
    IM(v, "willMoveToSuperview:", v_noop1); IM(v, "willMoveToWindow:", v_noop1); IM(v, "didAddSubview:", v_noop1); IM(v, "willRemoveSubview:", v_noop1);
    IM(v, "gestureRecognizers", v_gestures); IM(v, "setGestureRecognizers:", v_setGestures);
    IM(v, "addGestureRecognizer:", v_addGesture); IM(v, "removeGestureRecognizer:", v_removeGesture);
    IM(v, "endEditing:", v_endEditing); IM(v, "contentScaleFactor", v_contentScale); IM(v, "setContentScaleFactor:", v_noopD);
    IM(v, "layoutMargins", v_layoutMargins); IM(v, "setLayoutMargins:", v_setLayoutMargins);
    IM(v, "accessibilityLabel", v_accLabel); IM(v, "setAccessibilityLabel:", v_setAccLabel);
    IM(v, "accessibilityIdentifier", v_accId); IM(v, "setAccessibilityIdentifier:", v_setAccId);
    IM(v, "setIsAccessibilityElement:", v_noopL); IM(v, "setAccessibilityTraits:", v_noopL); IM(v, "setAccessibilityHint:", v_noop1);
    IM(v, "setAccessibilityValue:", v_noop1); IM(v, "setAccessibilityViewIsModal:", v_noopL); IM(v, "setAccessibilityElementsHidden:", v_noopL);
    IM(v, "setTranslatesAutoresizingMaskIntoConstraints:", v_noopL); IM(v, "setShouldGroupAccessibilityChildren:", v_noopL);
    IM(v, "setContentHuggingPriority:forAxis:", v_noopFL); IM(v, "setContentCompressionResistancePriority:forAxis:", v_noopFL);
    IM(v, "setAccessibilityContainerType:", v_noopL); IM(v, "setAccessibilityElements:", v_noop1); IM(v, "addConstraint:", v_noop1); IM(v, "addConstraints:", v_noop1);
    IM(v, "removeConstraint:", v_noop1); IM(v, "removeConstraints:", v_noop1); IM(v, "constraints", v_nil); IM(v, "updateConstraintsIfNeeded", v_noop);
    IM(v, "setNeedsUpdateConstraints", v_noop); IM(v, "updateConstraints", v_noop);
    IM(v, "setOpaque:", v_noopL); IM(v, "isOpaque", v_isOpaque); IM(v, "setClearsContextBeforeDrawing:", v_noopL); IM(v, "setPreservesSuperviewLayoutMargins:", v_noopL);
    IM(v, "setSemanticContentAttribute:", v_noopL); IM(v, "setInsetsLayoutMarginsFromSafeArea:", v_noopL); IM(v, "setRestorationIdentifier:", v_noop1);
    IM(v, "description", v_description);
    CM(v, "animateWithDuration:animations:", va_anim); CM(v, "animateWithDuration:animations:completion:", va_animC);
    CM(v, "animateWithDuration:delay:options:animations:completion:", va_animDO);
    CM(v, "animateWithDuration:delay:usingSpringWithDamping:initialSpringVelocity:options:animations:completion:", va_animSpring);
    CM(v, "transitionWithView:duration:options:animations:completion:", va_transition);
    CM(v, "transitionFromView:toView:duration:options:completion:", va_transitionFrom);
    CM(v, "animateKeyframesWithDuration:delay:options:animations:completion:", va_keyframes);
    CM(v, "addKeyframeWithRelativeStartTime:relativeDuration:animations:", va_addKeyframe);
    CM(v, "performWithoutAnimation:", va_withoutAnimation); CM(v, "setAnimationsEnabled:", va_setEnabled); CM(v, "areAnimationsEnabled", va_enabled);
    CM(v, "beginAnimations:context:", va_begin); CM(v, "commitAnimations", va_commit); CM(v, "setAnimationDuration:", va_setDuration);
    CM(v, "setAnimationDelay:", va_setDelay); CM(v, "setAnimationDelegate:", va_setDelegate); CM(v, "setAnimationDidStopSelector:", va_setStop);
    CM(v, "setAnimationCurve:", va_noopL); CM(v, "setAnimationBeginsFromCurrentState:", va_noopL); CM(v, "setAnimationRepeatCount:", va_noopL);
    CM(v, "setAnimationRepeatAutoreverses:", va_noopL); CM(v, "setAnimationTransition:forView:cache:", va_noop2);
    CM(v, "setAnimationWillStartSelector:", va_noopL);

    NEWCLASS(UIWindow, v, sizeof(struct uiwindow) - sizeof(struct uiview));
    Class w = RT_CLASS(UIWindow);
    IM(w, "initWithFrame:", w_initWithFrame); IM(w, "dealloc", w_dealloc);
    IM(w, "makeKeyAndVisible", w_makeKeyAndVisible); IM(w, "makeKeyWindow", w_makeKey); IM(w, "isKeyWindow", w_isKey);
    IM(w, "rootViewController", w_root); IM(w, "setRootViewController:", w_setRoot); IM(w, "screen", w_screen); IM(w, "setScreen:", v_noop1);
    IM(w, "windowLevel", w_level); IM(w, "setWindowLevel:", w_setLevel); IM(w, "becomeKeyWindow", v_noop); IM(w, "resignKeyWindow", v_noop);
    IM(w, "convertPoint:toWindow:", w_convertPointToWindow); IM(w, "convertPoint:fromWindow:", w_convertPointFromWindow);
    IM(w, "sendEvent:", w_sendEvent); IM(w, "nextResponder", w_nextResponder);

    NEWCLASS(UITouch, o, sizeof(struct uitouch) - sizeof(void *));
    Class t = RT_CLASS(UITouch);
    IM(t, "locationInView:", t_locationInView); IM(t, "previousLocationInView:", t_prevLocationInView); IM(t, "view", t_view);
    IM(t, "window", t_window); IM(t, "phase", t_phase); IM(t, "tapCount", t_tapCount); IM(t, "timestamp", t_timestamp);
    IM(t, "gestureRecognizers", t_gestures); IM(t, "majorRadius", t_majorRadius); IM(t, "force", t_force); IM(t, "maximumPossibleForce", t_force);
    IM(t, "type", t_type); IM(t, "dealloc", t_dealloc);
    NEWCLASS(UIEvent, o, sizeof(struct uievent) - sizeof(void *));
    Class ev = RT_CLASS(UIEvent);
    IM(ev, "allTouches", ev_all); IM(ev, "touchesForView:", ev_forView); IM(ev, "touchesForWindow:", ev_forWindow);
    IM(ev, "touchesForGestureRecognizer:", ev_forGesture); IM(ev, "timestamp", ev_ts); IM(ev, "type", ev_type); IM(ev, "subtype", ev_type);
    IM(ev, "dealloc", ev_dealloc);
    NEWCLASS(UIUserNotificationSettings, o, 0);
    CM(RT_CLASS(UIUserNotificationSettings), "settingsForTypes:categories:", uns_make);

    NEWCLASS(UIGestureRecognizer, o, sizeof(struct uigr) - sizeof(void *));
    Class g = RT_CLASS(UIGestureRecognizer);
    IM(g, "init", gr_init); IM(g, "initWithTarget:action:", gr_initWithTarget); IM(g, "dealloc", gr_dealloc);
    IM(g, "addTarget:action:", gr_addTarget); IM(g, "removeTarget:action:", gr_removeTarget);
    IM(g, "state", gr_state); IM(g, "setState:", gr_setState); IM(g, "view", gr_view); IM(g, "isEnabled", gr_enabled); IM(g, "setEnabled:", gr_setEnabled);
    IM(g, "delegate", gr_delegate); IM(g, "setDelegate:", gr_setDelegate); IM(g, "cancelsTouchesInView", gr_cancels); IM(g, "setCancelsTouchesInView:", gr_setCancels);
    IM(g, "delaysTouchesBegan", gr_delaysBegan); IM(g, "setDelaysTouchesBegan:", gr_setDelaysBegan);
    IM(g, "delaysTouchesEnded", gr_delaysEnded); IM(g, "setDelaysTouchesEnded:", gr_setDelaysEnded);
    IM(g, "requireGestureRecognizerToFail:", gr_requireFail); IM(g, "numberOfTouches", gr_numTouches);
    IM(g, "locationInView:", gr_location); IM(g, "locationOfTouch:inView:", gr_locationOfTouch); IM(g, "reset", gr_reset);
    IM(g, "ignoreTouch:forEvent:", gr_ignoreTouch); IM(g, "name", gr_name); IM(g, "setName:", gr_setName);
    IM(g, "touchesBegan:withEvent:", gr_touchesBegan); IM(g, "touchesMoved:withEvent:", gr_touchesMoved);
    IM(g, "touchesEnded:withEvent:", gr_touchesEnded); IM(g, "touchesCancelled:withEvent:", gr_touchesCancelled);
    IM(g, "canPreventGestureRecognizer:", r_yes); IM(g, "canBePreventedByGestureRecognizer:", r_yes);
    IM(g, "shouldRequireFailureOfGestureRecognizer:", r_no); IM(g, "shouldBeRequiredToFailByGestureRecognizer:", r_no);
    NEWCLASS(UITapGestureRecognizer, g, 0);
    Class tg = RT_CLASS(UITapGestureRecognizer);
    IM(tg, "numberOfTapsRequired", gr_tapsReq); IM(tg, "setNumberOfTapsRequired:", gr_setTapsReq);
    IM(tg, "numberOfTouchesRequired", gr_touchesReq); IM(tg, "setNumberOfTouchesRequired:", gr_setTouchesReq);
    NEWCLASS(UILongPressGestureRecognizer, g, 0);
    Class lg = RT_CLASS(UILongPressGestureRecognizer);
    IM(lg, "minimumPressDuration", gr_minDur); IM(lg, "setMinimumPressDuration:", gr_setMinDur);
    IM(lg, "allowableMovement", gr_allowMove); IM(lg, "setAllowableMovement:", gr_setAllowMove);
    IM(lg, "numberOfTapsRequired", gr_tapsReq); IM(lg, "setNumberOfTapsRequired:", gr_setTapsReq);
    IM(lg, "numberOfTouchesRequired", gr_touchesReq); IM(lg, "setNumberOfTouchesRequired:", gr_setTouchesReq);
    NEWCLASS(UIPanGestureRecognizer, g, 0);
    Class pg = RT_CLASS(UIPanGestureRecognizer);
    IM(pg, "translationInView:", gr_translation); IM(pg, "setTranslation:inView:", gr_setTranslation); IM(pg, "velocityInView:", gr_velocity);
    IM(pg, "minimumNumberOfTouches", gr_minT); IM(pg, "setMinimumNumberOfTouches:", gr_setMinT);
    IM(pg, "maximumNumberOfTouches", gr_maxT); IM(pg, "setMaximumNumberOfTouches:", gr_setMaxT);
    NEWCLASS(UIScreenEdgePanGestureRecognizer, pg, 0);
    IM(RT_CLASS(UIScreenEdgePanGestureRecognizer), "edges", gr_edges); IM(RT_CLASS(UIScreenEdgePanGestureRecognizer), "setEdges:", gr_setEdges);
    NEWCLASS(UISwipeGestureRecognizer, g, 0);
    Class sg = RT_CLASS(UISwipeGestureRecognizer);
    IM(sg, "direction", gr_direction); IM(sg, "setDirection:", gr_setDirection);
    IM(sg, "numberOfTouchesRequired", gr_touchesReq); IM(sg, "setNumberOfTouchesRequired:", gr_setTouchesReq);
    NEWCLASS(UIPinchGestureRecognizer, g, 0);
    IM(RT_CLASS(UIPinchGestureRecognizer), "scale", gr_scale); IM(RT_CLASS(UIPinchGestureRecognizer), "setScale:", gr_setScale);
    IM(RT_CLASS(UIPinchGestureRecognizer), "velocity", gr_velocityScalar);
    NEWCLASS(UIRotationGestureRecognizer, g, 0);
    IM(RT_CLASS(UIRotationGestureRecognizer), "rotation", gr_rotation); IM(RT_CLASS(UIRotationGestureRecognizer), "setRotation:", gr_setRotation);
    IM(RT_CLASS(UIRotationGestureRecognizer), "velocity", gr_velocityScalar);

    rt_controls_init();
    rt_nib_init();

#define K(var, str) iosrt_##var = rt_str_new(str);
    K(UIApplicationDidFinishLaunchingNotification, "UIApplicationDidFinishLaunchingNotification")
    K(UIApplicationDidBecomeActiveNotification, "UIApplicationDidBecomeActiveNotification")
    K(UIApplicationWillResignActiveNotification, "UIApplicationWillResignActiveNotification")
    K(UIApplicationDidEnterBackgroundNotification, "UIApplicationDidEnterBackgroundNotification")
    K(UIApplicationWillEnterForegroundNotification, "UIApplicationWillEnterForegroundNotification")
    K(UIApplicationWillTerminateNotification, "UIApplicationWillTerminateNotification")
    K(UIApplicationDidReceiveMemoryWarningNotification, "UIApplicationDidReceiveMemoryWarningNotification")
    K(UIApplicationDidChangeStatusBarFrameNotification, "UIApplicationDidChangeStatusBarFrameNotification")
    K(UIApplicationWillChangeStatusBarFrameNotification, "UIApplicationWillChangeStatusBarFrameNotification")
    K(UIApplicationDidChangeStatusBarOrientationNotification, "UIApplicationDidChangeStatusBarOrientationNotification")
    K(UIApplicationStatusBarFrameUserInfoKey, "UIApplicationStatusBarFrameUserInfoKey")
    K(UIApplicationLaunchOptionsURLKey, "UIApplicationLaunchOptionsURLKey")
    K(UIApplicationLaunchOptionsSourceApplicationKey, "UIApplicationLaunchOptionsSourceApplicationKey")
    K(UIApplicationLaunchOptionsAnnotationKey, "UIApplicationLaunchOptionsAnnotationKey")
    K(UIApplicationLaunchOptionsRemoteNotificationKey, "UIApplicationLaunchOptionsRemoteNotificationKey")
    K(UIDeviceOrientationDidChangeNotification, "UIDeviceOrientationDidChangeNotification")
    K(UIKeyboardWillShowNotification, "UIKeyboardWillShowNotification") K(UIKeyboardDidShowNotification, "UIKeyboardDidShowNotification")
    K(UIKeyboardWillHideNotification, "UIKeyboardWillHideNotification") K(UIKeyboardDidHideNotification, "UIKeyboardDidHideNotification")
    K(UIKeyboardFrameEndUserInfoKey, "UIKeyboardFrameEndUserInfoKey") K(UIKeyboardFrameBeginUserInfoKey, "UIKeyboardFrameBeginUserInfoKey")
    K(UIKeyboardAnimationDurationUserInfoKey, "UIKeyboardAnimationDurationUserInfoKey") K(UIKeyboardAnimationCurveUserInfoKey, "UIKeyboardAnimationCurveUserInfoKey")
    K(UITextViewTextDidChangeNotification, "UITextViewTextDidChangeNotification") K(UITextFieldTextDidChangeNotification, "UITextFieldTextDidChangeNotification")
    K(UIMenuControllerWillHideMenuNotification, "UIMenuControllerWillHideMenuNotification")
    K(UIPasteboardNameGeneral, "com.apple.UIKit.pboard.general") K(UIPasteboardNameFind, "com.apple.UIKit.pboard.find")
    K(UIActivityTypePostToFacebook, "com.apple.UIKit.activity.PostToFacebook") K(UIActivityTypePostToTwitter, "com.apple.UIKit.activity.PostToTwitter")
    K(UIActivityTypePostToVimeo, "com.apple.UIKit.activity.PostToVimeo") K(UIActivityTypeSaveToCameraRoll, "com.apple.UIKit.activity.SaveToCameraRoll")
    K(UIActivityTypeMail, "com.apple.UIKit.activity.Mail") K(UIActivityTypeMessage, "com.apple.UIKit.activity.Message")
    K(UIActivityTypeCopyToPasteboard, "com.apple.UIKit.activity.CopyToPasteboard") K(UIActivityTypeAirDrop, "com.apple.UIKit.activity.AirDrop")
    K(UIImagePickerControllerOriginalImage, "UIImagePickerControllerOriginalImage") K(UIImagePickerControllerCropRect, "UIImagePickerControllerCropRect")
    K(UIImagePickerControllerEditedImage, "UIImagePickerControllerEditedImage") K(UIImagePickerControllerMediaURL, "UIImagePickerControllerMediaURL")
#undef K
}
