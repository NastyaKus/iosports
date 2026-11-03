/*
 * UIKit-lite + CoreGraphics subset.
 *
 * Model: the guest builds a UIView tree exactly as on iOS. Every frame the
 * runtime walks the key window, asks views to draw (backgroundColor, UILabel
 * text, UIButton title, custom -drawRect: through a recording CGContext) and
 * produces a platform-neutral display list, which the platform layer renders
 * (Android: Canvas in a custom View; host: text dump for tests).
 * Touches arrive from the platform, are hit-tested and delivered as
 * touchesBegan/Ended + UIControl target/action.
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#define IM(c, s, f) rt_native_method(c, s, (void *)(f), "")
#define CM(c, s, f) rt_native_cmethod(c, s, (void *)(f), "")

RT_DECLARE_CLASS(NSObject);
RT_DECLARE_CLASS(NSMutableArray);
RT_DECLARE_CLASS(NSSet);
RT_DEFINE_CLASS(UIResponder);
RT_DEFINE_CLASS(UIApplication);
RT_DEFINE_CLASS(UIScreen);
RT_DEFINE_CLASS(UIDevice);
RT_DEFINE_CLASS(UIView);
RT_DEFINE_CLASS(UIWindow);
RT_DEFINE_CLASS(UILabel);
RT_DEFINE_CLASS(UIControl);
RT_DEFINE_CLASS(UIButton);
RT_DEFINE_CLASS(UIViewController);
RT_DEFINE_CLASS(UIColor);
RT_DEFINE_CLASS(UIFont);
RT_DEFINE_CLASS(UITouch);
RT_DEFINE_CLASS(UIEvent);

id rt_set_new(id *objs, size_t n);
int rt_runloop_run_once(int max_wait_ms);
volatile int rt_quit_requested;

/* ------------------------------------------------------------ screen */
static float scr_w = 375, scr_h = 667, scr_scale = 2;
static pthread_mutex_t input_lock = PTHREAD_MUTEX_INITIALIZER;
struct touch_ev { int phase; float x, y; };
static struct touch_ev touchq[256]; static int tq_head, tq_tail;
void iosrt_set_screen(float w, float h, float scale) { if (w > 0) scr_w = w; if (h > 0) scr_h = h; if (scale > 0) scr_scale = scale; }
void iosrt_push_touch(int phase, float x, float y) {
    pthread_mutex_lock(&input_lock);
    int n = (tq_tail + 1) % 256;
    if (n != tq_head) { touchq[tq_tail] = (struct touch_ev){ phase, x, y }; tq_tail = n; }
    pthread_mutex_unlock(&input_lock);
}
void iosrt_request_quit(void) { rt_quit_requested = 1; }

/* ------------------------------------------------------------- objects */
struct uicolor { Class isa; double r, g, b, a; };
struct uifont { Class isa; double size; long bold; };
struct uiview {
    Class isa; CGRect frame; id bg; id subviews; id superview; double alpha;
    long hidden, tag, interaction, laidOut; id vc;
};
struct uilabel { struct uiview v; id text; id textColor; id font; long align; long lines; };
struct uicontrol { struct uiview v; id targets; long enabled, highlighted, selected; };
struct uibutton { struct uicontrol c; id title; id titleColor; id font; };
struct uiwindow { struct uiview v; id root; };
struct uivc { Class isa; id view; id title; long loaded; };
struct uiapp { Class isa; id delegate; id keyWindow; id windows; long idle; };
struct uitouch { Class isa; CGPoint pt; id view; long phase; double ts; };
struct target_action { Class isa; id target; SEL action; unsigned long events; };

static id color_make(double r, double g, double b, double a) {
    id c = rt_alloc_instance(RT_CLASS(UIColor), 0); struct uicolor *u = (struct uicolor *)c;
    u->r = r; u->g = g; u->b = b; u->a = a; return objc_autorelease(c);
}
static id c_rgba(Class c, SEL s, double r, double g, double b, double a) { (void)c; (void)s; return color_make(r, g, b, a); }
static id c_white(Class c, SEL s, double w, double a) { (void)c; (void)s; return color_make(w, w, w, a); }
static id c_init_rgba(id self, SEL s, double r, double g, double b, double a) { (void)s; struct uicolor *u = (struct uicolor *)self; u->r = r; u->g = g; u->b = b; u->a = a; return self; }
#define NAMED_COLOR(fn, r, g, b, a) static id fn(Class c, SEL s) { (void)c; (void)s; return color_make(r, g, b, a); }
NAMED_COLOR(c_whiteColor, 1, 1, 1, 1)
NAMED_COLOR(c_blackColor, 0, 0, 0, 1)
NAMED_COLOR(c_redColor, 1, 0, 0, 1)
NAMED_COLOR(c_greenColor, 0, 1, 0, 1)
NAMED_COLOR(c_blueColor, 0, 0, 1, 1)
NAMED_COLOR(c_yellowColor, 1, 1, 0, 1)
NAMED_COLOR(c_orangeColor, 1, 0.5, 0, 1)
NAMED_COLOR(c_purpleColor, 0.5, 0, 0.5, 1)
NAMED_COLOR(c_grayColor, 0.5, 0.5, 0.5, 1)
NAMED_COLOR(c_darkGrayColor, 1.0 / 3, 1.0 / 3, 1.0 / 3, 1)
NAMED_COLOR(c_lightGrayColor, 2.0 / 3, 2.0 / 3, 2.0 / 3, 1)
NAMED_COLOR(c_clearColor, 0, 0, 0, 0)
NAMED_COLOR(c_cyanColor, 0, 1, 1, 1)
NAMED_COLOR(c_magentaColor, 1, 0, 1, 1)
static id c_withAlpha(id self, SEL s, double a) { (void)s; struct uicolor *u = (struct uicolor *)self; return color_make(u->r, u->g, u->b, a); }
static rt_bool c_getRGBA(id self, SEL s, double *r, double *g, double *b, double *a) {
    (void)s; struct uicolor *u = (struct uicolor *)self;
    if (r) *r = u->r; if (g) *g = u->g; if (b) *b = u->b; if (a) *a = u->a; return 1;
}
static id c_CGColor(id self, SEL s) { (void)s; return self; }   /* CGColorRef == UIColor here */
static id c_description(id self, SEL s) { (void)s; struct uicolor *u = (struct uicolor *)self; char b[128]; snprintf(b, sizeof b, "UIExtendedSRGBColorSpace %g %g %g %g", u->r, u->g, u->b, u->a); return rt_str(b); }

static id f_system(Class c, SEL s, double size) { (void)s; id f = rt_alloc_instance(c, 0); ((struct uifont *)f)->size = size; return objc_autorelease(f); }
static id f_bold(Class c, SEL s, double size) { id f = f_system(c, s, size); ((struct uifont *)f)->bold = 1; return f; }
static id f_named(Class c, SEL s, id name, double size) { (void)name; return f_system(c, s, size); }
static double f_pointSize(id self, SEL s) { (void)s; return ((struct uifont *)self)->size; }

/* -------------------------------------------------------------- UIView */
static void mark_dirty(void);
static id v_initWithFrame(id self, SEL s, CGRect fr) {
    (void)s; struct uiview *v = (struct uiview *)self;
    v->frame = fr; v->alpha = 1; v->interaction = 1;
    v->subviews = RT_SEND0(id, RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "alloc"), "init");
    mark_dirty();
    return self;
}
static id v_init(id self, SEL s) { (void)s; return ((id (*)(id, SEL, CGRect))objc_msgSend)(self, rt_sel("initWithFrame:"), (CGRect){ { 0, 0 }, { 0, 0 } }); }
static void v_dealloc(id self, SEL s) {
    (void)s; struct uiview *v = (struct uiview *)self;
    objc_release(v->bg); objc_release(v->subviews);
    ((void (*)(void *, SEL))objc_msgSendSuper2)(&(struct { id r; Class c; }){ self, RT_CLASS(UIView) }, rt_sel("dealloc"));
}
static CGRect v_frame(id self, SEL s) { (void)s; return ((struct uiview *)self)->frame; }
static void v_setFrame(id self, SEL s, CGRect r) { (void)s; ((struct uiview *)self)->frame = r; ((struct uiview *)self)->laidOut = 0; mark_dirty(); }
static CGRect v_bounds(id self, SEL s) { (void)s; struct uiview *v = (struct uiview *)self; return (CGRect){ { 0, 0 }, v->frame.size }; }
static void v_setBounds(id self, SEL s, CGRect r) { (void)s; ((struct uiview *)self)->frame.size = r.size; mark_dirty(); }
static CGPoint v_center(id self, SEL s) { (void)s; CGRect f = ((struct uiview *)self)->frame; return (CGPoint){ f.origin.x + f.size.width / 2, f.origin.y + f.size.height / 2 }; }
static void v_setCenter(id self, SEL s, CGPoint p) { (void)s; struct uiview *v = (struct uiview *)self; v->frame.origin.x = p.x - v->frame.size.width / 2; v->frame.origin.y = p.y - v->frame.size.height / 2; mark_dirty(); }
static id v_bg(id self, SEL s) { (void)s; return ((struct uiview *)self)->bg; }
static void v_setBg(id self, SEL s, id c) { (void)s; struct uiview *v = (struct uiview *)self; objc_retain(c); objc_release(v->bg); v->bg = c; mark_dirty(); }
static id v_subviews(id self, SEL s) { (void)s; return ((struct uiview *)self)->subviews; }
static id v_superview(id self, SEL s) { (void)s; return ((struct uiview *)self)->superview; }
static id v_window(id self, SEL s) { (void)s; id v = self; while (v && !rt_is_kind_of(v, RT_CLASS(UIWindow))) v = ((struct uiview *)v)->superview; return v; }
static void v_removeFromSuperview(id self, SEL s) {
    (void)s; struct uiview *v = (struct uiview *)self;
    if (!v->superview) return;
    id sup = v->superview; v->superview = NULL;
    RT_SEND1(void, ((struct uiview *)sup)->subviews, "removeObject:", id, self);
    mark_dirty();
}
static void v_addSubview(id self, SEL s, id sub) {
    (void)s; if (!sub || sub == self) return;
    struct uiview *c = (struct uiview *)sub;
    objc_retain(sub);
    if (c->superview) v_removeFromSuperview(sub, NULL);
    c->superview = self;
    RT_SEND1(void, ((struct uiview *)self)->subviews, "addObject:", id, sub);
    objc_release(sub);
    if (rt_responds(sub, rt_sel("didMoveToSuperview"))) RT_SEND0(void, sub, "didMoveToSuperview");
    mark_dirty();
}
static void v_insertSubviewAt(id self, SEL s, id sub, long idx) {
    v_addSubview(self, s, sub);
    id arr = ((struct uiview *)self)->subviews; size_t n = rt_array_count(arr);
    if (idx >= 0 && (size_t)idx < n - 1) {
        objc_retain(sub);
        RT_SEND1(void, arr, "removeLastObject", long, 0);
        RT_SEND2(void, arr, "insertObject:atIndex:", id, sub, unsigned long, (unsigned long)idx);
        objc_release(sub);
    }
}
static void v_bringToFront(id self, SEL s, id sub) { (void)s; objc_retain(sub); id arr = ((struct uiview *)self)->subviews; RT_SEND1(void, arr, "removeObject:", id, sub); RT_SEND1(void, arr, "addObject:", id, sub); objc_release(sub); mark_dirty(); }
static double v_alpha(id self, SEL s) { (void)s; return ((struct uiview *)self)->alpha; }
static void v_setAlpha(id self, SEL s, double a) { (void)s; ((struct uiview *)self)->alpha = a; mark_dirty(); }
static rt_bool v_hidden(id self, SEL s) { (void)s; return ((struct uiview *)self)->hidden; }
static void v_setHidden(id self, SEL s, long h) { (void)s; ((struct uiview *)self)->hidden = h & 0xff; mark_dirty(); }
static long v_tag(id self, SEL s) { (void)s; return ((struct uiview *)self)->tag; }
static void v_setTag(id self, SEL s, long t) { (void)s; ((struct uiview *)self)->tag = t; }
static id v_viewWithTag(id self, SEL s, long t) {
    struct uiview *v = (struct uiview *)self;
    if (v->tag == t) return self;
    for (size_t i = 0; i < rt_array_count(v->subviews); i++) { id r = v_viewWithTag(rt_array_at(v->subviews, i), s, t); if (r) return r; }
    return NULL;
}
static rt_bool v_interaction(id self, SEL s) { (void)s; return ((struct uiview *)self)->interaction; }
static void v_setInteraction(id self, SEL s, long e) { (void)s; ((struct uiview *)self)->interaction = e & 0xff; }
static void v_setNeedsDisplay(id self, SEL s) { (void)self; (void)s; mark_dirty(); }
static void v_setNeedsLayout(id self, SEL s) { (void)s; ((struct uiview *)self)->laidOut = 0; mark_dirty(); }
static void v_layoutIfNeeded(id self, SEL s) { (void)self; (void)s; }
static void v_noop(id self, SEL s) { (void)self; (void)s; }
static void v_noop1(id self, SEL s, id a) { (void)self; (void)s; (void)a; }
static void v_noop2(id self, SEL s, id a, id b) { (void)self; (void)s; (void)a; (void)b; }
static void v_setNoopL(id self, SEL s, long a) { (void)self; (void)s; (void)a; }
static CGPoint v_convertPointFromView(id self, SEL s, CGPoint p, id from);
static CGPoint abs_origin(id v) {
    CGPoint o = { 0, 0 };
    for (; v; v = ((struct uiview *)v)->superview) { o.x += ((struct uiview *)v)->frame.origin.x; o.y += ((struct uiview *)v)->frame.origin.y; }
    return o;
}
static CGPoint v_convertPointFromView(id self, SEL s, CGPoint p, id from) {
    (void)s; CGPoint a = from ? abs_origin(from) : (CGPoint){ 0, 0 }, b = abs_origin(self);
    return (CGPoint){ p.x + a.x - b.x, p.y + a.y - b.y };
}
static CGPoint v_convertPointToView(id self, SEL s, CGPoint p, id to) {
    (void)s; CGPoint a = abs_origin(self), b = to ? abs_origin(to) : (CGPoint){ 0, 0 };
    return (CGPoint){ p.x + a.x - b.x, p.y + a.y - b.y };
}
static rt_bool v_pointInside(id self, SEL s, CGPoint p, id ev) { (void)s; (void)ev; CGRect f = ((struct uiview *)self)->frame; return p.x >= 0 && p.y >= 0 && p.x < f.size.width && p.y < f.size.height; }
static id v_hitTest(id self, SEL s, CGPoint p, id ev) {
    struct uiview *v = (struct uiview *)self;
    if (v->hidden || !v->interaction || v->alpha < 0.01) return NULL;
    if (!(((long (*)(id, SEL, CGPoint, id))objc_msgSend)(self, rt_sel("pointInside:withEvent:"), p, ev) & 0xff)) return NULL;
    for (size_t i = rt_array_count(v->subviews); i-- > 0;) {
        id sub = rt_array_at(v->subviews, i);
        CGRect f = ((struct uiview *)sub)->frame;
        id h = ((id (*)(id, SEL, CGPoint, id))objc_msgSend)(sub, s, (CGPoint){ p.x - f.origin.x, p.y - f.origin.y }, ev);
        if (h) return h;
    }
    return self;
}
static void v_sizeToFit(id self, SEL s);
static void v_drawRect(id self, SEL s, CGRect r) { (void)self; (void)s; (void)r; }
static id v_nextResponder(id self, SEL s) { (void)s; struct uiview *v = (struct uiview *)self; return v->vc ? v->vc : v->superview; }
/* responder chain default: forward touches to next responder */
static void r_touches(id self, SEL s, id touches, id ev) {
    id next = rt_responds(self, rt_sel("nextResponder")) ? RT_SEND0(id, self, "nextResponder") : NULL;
    if (next) ((void (*)(id, SEL, id, id))objc_msgSend)(next, s, touches, ev);
}
static id r_nextResponder(id self, SEL s) { (void)self; (void)s; return NULL; }
static rt_bool r_yes(id self, SEL s) { (void)self; (void)s; return 1; }
static rt_bool r_no(id self, SEL s) { (void)self; (void)s; return 0; }

/* ------------------------------------------------------------- UILabel */
static id l_initWithFrame(id self, SEL s, CGRect fr) {
    v_initWithFrame(self, s, fr);
    struct uilabel *l = (struct uilabel *)self;
    l->textColor = objc_retain(color_make(0, 0, 0, 1)); l->lines = 1;
    l->font = objc_retain(f_system(RT_CLASS(UIFont), NULL, 17));
    return self;
}
static id l_text(id self, SEL s) { (void)s; return ((struct uilabel *)self)->text; }
static void l_setText(id self, SEL s, id t) { (void)s; struct uilabel *l = (struct uilabel *)self; id c = t ? RT_SEND0(id, t, "copy") : NULL; objc_release(l->text); l->text = c; mark_dirty(); }
static id l_textColor(id self, SEL s) { (void)s; return ((struct uilabel *)self)->textColor; }
static void l_setTextColor(id self, SEL s, id c) { (void)s; struct uilabel *l = (struct uilabel *)self; objc_retain(c); objc_release(l->textColor); l->textColor = c; mark_dirty(); }
static id l_font(id self, SEL s) { (void)s; return ((struct uilabel *)self)->font; }
static void l_setFont(id self, SEL s, id f) { (void)s; struct uilabel *l = (struct uilabel *)self; objc_retain(f); objc_release(l->font); l->font = f; mark_dirty(); }
static long l_align(id self, SEL s) { (void)s; return ((struct uilabel *)self)->align; }
static void l_setAlign(id self, SEL s, long a) { (void)s; ((struct uilabel *)self)->align = a; mark_dirty(); }
static void l_setLines(id self, SEL s, long n) { (void)s; ((struct uilabel *)self)->lines = n; }
static double font_size(id f) { return f ? ((struct uifont *)f)->size : 17; }
static void v_sizeToFit(id self, SEL s) {
    (void)s;
    if (rt_is_kind_of(self, RT_CLASS(UILabel))) {
        struct uilabel *l = (struct uilabel *)self;
        size_t n = l->text ? strlen(rt_str_utf8(l->text)) : 0;
        l->v.frame.size.width = ceil(n * font_size(l->font) * 0.55);
        l->v.frame.size.height = ceil(font_size(l->font) * 1.2);
        mark_dirty();
    }
}

/* ------------------------------------------------------- UIControl/UIButton */
static id ctl_initWithFrame(id self, SEL s, CGRect fr) {
    v_initWithFrame(self, s, fr);
    struct uicontrol *c = (struct uicontrol *)self;
    c->targets = RT_SEND0(id, RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "alloc"), "init"); c->enabled = 1;
    return self;
}
static void ctl_addTarget(id self, SEL s, id target, SEL action, unsigned long events) {
    (void)s; struct uicontrol *c = (struct uicontrol *)self;
    if (!c->targets) c->targets = RT_SEND0(id, RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "alloc"), "init");
    id ta = rt_alloc_instance(RT_CLASS(NSObject), sizeof(struct target_action) - sizeof(void *));
    struct target_action *t = (struct target_action *)ta; t->target = target; t->action = action; t->events = events;
    RT_SEND1(void, c->targets, "addObject:", id, ta); objc_release(ta);
}
static void ctl_sendActions(id self, SEL s, unsigned long events) {
    (void)s; struct uicontrol *c = (struct uicontrol *)self;
    for (size_t i = 0; i < rt_array_count(c->targets); i++) {
        struct target_action *t = (struct target_action *)rt_array_at(c->targets, i);
        if (!(t->events & events)) continue;
        id target = t->target;
        if (!target) { rt_warn("nil-targeted actions (responder chain) are not supported"); continue; }
        ((void (*)(id, SEL, id, id))objc_msgSend)(target, t->action, self, NULL);
    }
}
static rt_bool ctl_enabled(id self, SEL s) { (void)s; return ((struct uicontrol *)self)->enabled; }
static void ctl_setEnabled(id self, SEL s, long e) { (void)s; ((struct uicontrol *)self)->enabled = e & 0xff; mark_dirty(); }
static rt_bool ctl_selected(id self, SEL s) { (void)s; return ((struct uicontrol *)self)->selected; }
static void ctl_setSelected(id self, SEL s, long e) { (void)s; ((struct uicontrol *)self)->selected = e & 0xff; mark_dirty(); }
static id b_buttonWithType(Class c, SEL s, long type) {
    (void)s; (void)type;
    id b = ((id (*)(id, SEL, CGRect))objc_msgSend)(rt_alloc_instance(c, 0), rt_sel("initWithFrame:"), (CGRect){ { 0, 0 }, { 0, 0 } });
    struct uibutton *bt = (struct uibutton *)b;
    bt->titleColor = objc_retain(color_make(0, 0.48, 1, 1));
    return objc_autorelease(b);
}
static void b_setTitle(id self, SEL s, id t, unsigned long state) { (void)s; if (state) return; struct uibutton *b = (struct uibutton *)self; id c = t ? RT_SEND0(id, t, "copy") : NULL; objc_release(b->title); b->title = c; mark_dirty(); }
static id b_titleForState(id self, SEL s, unsigned long st) { (void)s; (void)st; return ((struct uibutton *)self)->title; }
static void b_setTitleColor(id self, SEL s, id c, unsigned long state) { (void)s; if (state) return; struct uibutton *b = (struct uibutton *)self; objc_retain(c); objc_release(b->titleColor); b->titleColor = c; mark_dirty(); }
static id b_titleLabel(id self, SEL s) { (void)self; (void)s; return NULL; }

/* ---------------------------------------------------------- UIWindow */
static id g_app;
static void w_makeKeyAndVisible(id self, SEL s) {
    (void)s; struct uiapp *a = (struct uiapp *)g_app;
    if (a) { objc_retain(self); objc_release(a->keyWindow); a->keyWindow = self; }
    struct uiwindow *w = (struct uiwindow *)self;
    if (w->root && rt_responds(w->root, rt_sel("viewWillAppear:"))) RT_SEND1(void, w->root, "viewWillAppear:", long, 0);
    if (w->root && rt_responds(w->root, rt_sel("viewDidAppear:"))) RT_SEND1(void, w->root, "viewDidAppear:", long, 0);
    mark_dirty();
}
static id w_root(id self, SEL s) { (void)s; return ((struct uiwindow *)self)->root; }
static void w_setRoot(id self, SEL s, id vc) {
    (void)s; struct uiwindow *w = (struct uiwindow *)self;
    objc_retain(vc); objc_release(w->root); w->root = vc;
    if (vc) {
        id view = RT_SEND0(id, vc, "view");
        struct uiview *v = (struct uiview *)view;
        v->frame = (CGRect){ { 0, 0 }, w->v.frame.size };
        v_addSubview(self, NULL, view);
    }
}
static id w_screen(id self, SEL s) { (void)self; (void)s; return RT_SEND0(id, (id)RT_CLASS(UIScreen), "mainScreen"); }

/* ------------------------------------------------------- UIViewController */
static id vc_init(id self, SEL s) { (void)s; return self; }
static id vc_initWithNib(id self, SEL s, id nib, id bundle) { (void)s; (void)bundle; if (nib) rt_warn("NIB/storyboard loading is not supported (%s)", rt_str_utf8(nib)); return self; }
static void vc_loadView(id self, SEL s) {
    (void)s; struct uivc *c = (struct uivc *)self;
    id v = ((id (*)(id, SEL, CGRect))objc_msgSend)(RT_SEND0(id, (id)RT_CLASS(UIView), "alloc"), rt_sel("initWithFrame:"), (CGRect){ { 0, 0 }, { scr_w, scr_h } });
    RT_SEND1(void, v, "setBackgroundColor:", id, color_make(1, 1, 1, 1));
    c->view = v;
}
static id vc_view(id self, SEL s) {
    (void)s; struct uivc *c = (struct uivc *)self;
    if (!c->view && !c->loaded) {
        c->loaded = 1;
        RT_SEND0(void, self, "loadView");
        if (c->view) ((struct uiview *)c->view)->vc = self;
        RT_SEND0(void, self, "viewDidLoad");
    }
    return c->view;
}
static void vc_setView(id self, SEL s, id v) { (void)s; struct uivc *c = (struct uivc *)self; objc_retain(v); objc_release(c->view); c->view = v; c->loaded = 1; if (v) ((struct uiview *)v)->vc = self; }
static rt_bool vc_isViewLoaded(id self, SEL s) { (void)s; return ((struct uivc *)self)->view != NULL; }
static id vc_title(id self, SEL s) { (void)s; return ((struct uivc *)self)->title; }
static void vc_setTitle(id self, SEL s, id t) { (void)s; struct uivc *c = (struct uivc *)self; objc_retain(t); objc_release(c->title); c->title = t; }
static id vc_nextResponder(id self, SEL s) { (void)s; id v = ((struct uivc *)self)->view; return v ? ((struct uiview *)v)->superview : NULL; }
static long vc_supportedOrientations(id self, SEL s) { (void)self; (void)s; return 2; }
static void vc_presentViewController(id self, SEL s, id vc, long animated, void *completion) {
    (void)s; (void)animated;
    id view = RT_SEND0(id, vc, "view");
    id win = v_window(((struct uivc *)self)->view, NULL);
    if (win) { ((struct uiview *)view)->frame = ((struct uiview *)win)->frame; v_addSubview(win, NULL, view); }
    if (completion) ((void (*)(void *))((struct rt_block *)completion)->invoke)(completion);
}

/* ----------------------------------------------------------- UIScreen */
static id g_screen, g_device;
static id sc_main(Class c, SEL s) { (void)s; if (!g_screen) g_screen = rt_alloc_instance(c, 0); return g_screen; }
static CGRect sc_bounds(id self, SEL s) { (void)self; (void)s; return (CGRect){ { 0, 0 }, { scr_w, scr_h } }; }
static CGRect sc_nativeBounds(id self, SEL s) { (void)self; (void)s; return (CGRect){ { 0, 0 }, { scr_w * scr_scale, scr_h * scr_scale } }; }
static double sc_scale(id self, SEL s) { (void)self; (void)s; return scr_scale; }
static id dev_current(Class c, SEL s) { (void)s; if (!g_device) g_device = rt_alloc_instance(c, 0); return g_device; }
static id dev_sysver(id self, SEL s) { (void)self; (void)s; return rt_str("12.0"); }
static id dev_sysname(id self, SEL s) { (void)self; (void)s; return rt_str("iOS"); }
static id dev_model(id self, SEL s) { (void)self; (void)s; return rt_str("iPhone"); }
static id dev_name(id self, SEL s) { (void)self; (void)s; return rt_str("iosrt device"); }
static long dev_idiom(id self, SEL s) { (void)self; (void)s; return 0; }
static long dev_orientation(id self, SEL s) { (void)self; (void)s; return 1; }

/* ------------------------------------------------------- UIApplication */
static id app_shared(Class c, SEL s) {
    (void)s;
    if (!g_app) { g_app = rt_alloc_instance(c, 0); ((struct uiapp *)g_app)->windows = RT_SEND0(id, RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "alloc"), "init"); }
    return g_app;
}
static id app_delegate(id self, SEL s) { (void)s; return ((struct uiapp *)self)->delegate; }
static void app_setDelegate(id self, SEL s, id d) { (void)s; ((struct uiapp *)self)->delegate = d; }
static id app_keyWindow(id self, SEL s) { (void)s; return ((struct uiapp *)self)->keyWindow; }
static id app_windows(id self, SEL s) { (void)s; id k = ((struct uiapp *)self)->keyWindow; return objc_autorelease(rt_array_new(k ? &k : NULL, k ? 1 : 0)); }
static long app_state(id self, SEL s) { (void)self; (void)s; return 0; }
static long app_orientation(id self, SEL s) { (void)self; (void)s; return 1; }
static rt_bool app_openURL(id self, SEL s, id url) { (void)self; (void)s; (void)url; return 0; }
static void app_setIdle(id self, SEL s, long v) { (void)s; ((struct uiapp *)self)->idle = v; }
static void app_setNetworkIndicator(id self, SEL s, long v) { (void)self; (void)s; (void)v; }

/* ------------------------------------------------------------ UITouch */
static CGPoint t_locationInView(id self, SEL s, id view) {
    (void)s; struct uitouch *t = (struct uitouch *)self;
    CGPoint o = view ? abs_origin(view) : (CGPoint){ 0, 0 };
    return (CGPoint){ t->pt.x - o.x, t->pt.y - o.y };
}
static id t_view(id self, SEL s) { (void)s; return ((struct uitouch *)self)->view; }
static long t_phase(id self, SEL s) { (void)s; return ((struct uitouch *)self)->phase; }
static long t_tapCount(id self, SEL s) { (void)self; (void)s; return 1; }
static double t_timestamp(id self, SEL s) { (void)s; return ((struct uitouch *)self)->ts; }

/* ================================================================ CoreGraphics */
struct cgctx { float ox, oy; float fill[4], stroke[4]; float lw; float px, py; struct rt_display_list *dl; struct { float x0, y0, x1, y1; } seg[256]; int nseg; };
static struct cgctx *cur_ctx;
static struct cgctx ctx_storage;
void *iosrt_UIGraphicsGetCurrentContext(void) { return cur_ctx; }
static void put_rect(struct cgctx *c, int op, CGRect r, const float *col, float lw) {
    if (!c) return;
    struct rt_dl_cmd *d = rt_dl_push(c->dl, op);
    d->x = c->ox + (float)r.origin.x; d->y = c->oy + (float)r.origin.y; d->w = (float)r.size.width; d->h = (float)r.size.height;
    d->r = col[0]; d->g = col[1]; d->b = col[2]; d->a = col[3]; d->line = lw;
}
void iosrt_CGContextSetRGBFillColor(struct cgctx *c, double r, double g, double b, double a) { if (c) { c->fill[0] = (float)r; c->fill[1] = (float)g; c->fill[2] = (float)b; c->fill[3] = (float)a; } }
void iosrt_CGContextSetRGBStrokeColor(struct cgctx *c, double r, double g, double b, double a) { if (c) { c->stroke[0] = (float)r; c->stroke[1] = (float)g; c->stroke[2] = (float)b; c->stroke[3] = (float)a; } }
void iosrt_CGContextSetFillColorWithColor(struct cgctx *c, id col) { struct uicolor *u = (struct uicolor *)col; if (c && u) iosrt_CGContextSetRGBFillColor(c, u->r, u->g, u->b, u->a); }
void iosrt_CGContextSetStrokeColorWithColor(struct cgctx *c, id col) { struct uicolor *u = (struct uicolor *)col; if (c && u) iosrt_CGContextSetRGBStrokeColor(c, u->r, u->g, u->b, u->a); }
void iosrt_CGContextSetLineWidth(struct cgctx *c, double w) { if (c) c->lw = (float)w; }
void iosrt_CGContextFillRect(struct cgctx *c, CGRect r) { if (c) put_rect(c, 1, r, c->fill, 0); }
void iosrt_CGContextStrokeRect(struct cgctx *c, CGRect r) { if (c) put_rect(c, 6, r, c->stroke, c->lw); }
void iosrt_CGContextFillEllipseInRect(struct cgctx *c, CGRect r) { if (c) put_rect(c, 3, r, c->fill, 0); }
void iosrt_CGContextStrokeEllipseInRect(struct cgctx *c, CGRect r) { if (c) put_rect(c, 4, r, c->stroke, c->lw); }
void iosrt_CGContextMoveToPoint(struct cgctx *c, double x, double y) { if (c) { c->px = (float)x; c->py = (float)y; } }
void iosrt_CGContextAddLineToPoint(struct cgctx *c, double x, double y) {
    if (!c) return;
    if (c->nseg < 256) { c->seg[c->nseg].x0 = c->px; c->seg[c->nseg].y0 = c->py; c->seg[c->nseg].x1 = (float)x; c->seg[c->nseg].y1 = (float)y; c->nseg++; }
    c->px = (float)x; c->py = (float)y;
}
void iosrt_CGContextStrokePath(struct cgctx *c) {
    if (!c) return;
    for (int i = 0; i < c->nseg; i++) {
        struct rt_dl_cmd *d = rt_dl_push(c->dl, 5);
        d->x = c->ox + c->seg[i].x0; d->y = c->oy + c->seg[i].y0; d->w = c->seg[i].x1 - c->seg[i].x0; d->h = c->seg[i].y1 - c->seg[i].y0;
        memcpy(&d->r, c->stroke, sizeof(float) * 4); d->line = c->lw;
    }
    c->nseg = 0;
}
void iosrt_CGContextBeginPath(struct cgctx *c) { if (c) c->nseg = 0; }
void iosrt_CGContextSaveGState(struct cgctx *c) { (void)c; }
void iosrt_CGContextRestoreGState(struct cgctx *c) { (void)c; }
void iosrt_UIRectFill(CGRect r) { if (cur_ctx) put_rect(cur_ctx, 1, r, cur_ctx->fill, 0); }
static void c_setFill(id self, SEL s) { (void)s; iosrt_CGContextSetFillColorWithColor(cur_ctx, self); }
static void c_setStroke(id self, SEL s) { (void)s; iosrt_CGContextSetStrokeColorWithColor(cur_ctx, self); }
static void c_set(id self, SEL s) { c_setFill(self, s); c_setStroke(self, s); }
/* geometry functions that are real exports in CoreGraphics */
CGRect iosrt_CGRectZero, iosrt_CGRectNull = { { INFINITY, INFINITY }, { 0, 0 } }, iosrt_CGRectInfinite = { { -8.98846567431158e307, -8.98846567431158e307 }, { 1.7976931348623157e308, 1.7976931348623157e308 } };
CGPoint iosrt_CGPointZero; CGSize iosrt_CGSizeZero;
struct cgaffine { double a, b, c, d, tx, ty; } iosrt_CGAffineTransformIdentity = { 1, 0, 0, 1, 0, 0 };
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

/* ============================================================== rendering */
static volatile int dirty = 1;
static void mark_dirty(void) { dirty = 1; }
void rt_dl_clear(struct rt_display_list *dl) {
    for (size_t i = 0; i < dl->n; i++) free(dl->cmds[i].text);
    dl->n = 0;
}
struct rt_dl_cmd *rt_dl_push(struct rt_display_list *dl, int op) {
    if (dl->n == dl->cap) { dl->cap = dl->cap ? dl->cap * 2 : 64; dl->cmds = realloc(dl->cmds, dl->cap * sizeof *dl->cmds); }
    struct rt_dl_cmd *c = &dl->cmds[dl->n++];
    memset(c, 0, sizeof *c); c->op = op;
    return c;
}
void rt_dl_dump(const struct rt_display_list *dl, rt_sb *out) {
    static const char *names[] = { "?", "rect", "text", "oval", "ovalStroke", "line", "rectStroke" };
    rt_sb_printf(out, "screen %gx%g\n", dl->width, dl->height);
    for (size_t i = 0; i < dl->n; i++) {
        const struct rt_dl_cmd *c = &dl->cmds[i];
        rt_sb_printf(out, "%s %g,%g %gx%g rgba(%d,%d,%d,%.2f)", names[c->op > 6 ? 0 : c->op], c->x, c->y, c->w, c->h,
                     (int)lroundf(c->r * 255), (int)lroundf(c->g * 255), (int)lroundf(c->b * 255), c->a);
        if (c->op == 2) rt_sb_printf(out, " size=%g align=%d \"%s\"", c->size, c->align, c->text ? c->text : "");
        if (c->line > 0) rt_sb_printf(out, " lw=%g", c->line);
        rt_sb_putc(out, '\n');
    }
}
static void render_view(id view, float ox, float oy, float alpha, struct rt_display_list *dl) {
    struct uiview *v = (struct uiview *)view;
    if (v->hidden || v->alpha <= 0.001) return;
    alpha *= (float)v->alpha;
    float x = ox + (float)v->frame.origin.x, y = oy + (float)v->frame.origin.y;
    float w = (float)v->frame.size.width, h = (float)v->frame.size.height;
    if (!v->laidOut) {
        v->laidOut = 1;
        if (rt_responds(view, rt_sel("layoutSubviews"))) RT_SEND0(void, view, "layoutSubviews");
    }
    if (v->bg) {
        struct uicolor *c = (struct uicolor *)v->bg;
        if (c->a > 0) {
            struct rt_dl_cmd *d = rt_dl_push(dl, 1);
            d->x = x; d->y = y; d->w = w; d->h = h; d->r = (float)c->r; d->g = (float)c->g; d->b = (float)c->b; d->a = (float)c->a * alpha;
        }
    }
    /* custom drawing: only if a subclass overrides -drawRect: */
    int found; IMP imp = rt_lookup(view->isa, rt_sel("drawRect:"), &found);
    if (found && imp != (IMP)v_drawRect) {
        struct cgctx *saved = cur_ctx;
        memset(&ctx_storage, 0, sizeof ctx_storage);
        ctx_storage.ox = x; ctx_storage.oy = y; ctx_storage.dl = dl; ctx_storage.lw = 1;
        ctx_storage.fill[3] = 1; ctx_storage.stroke[3] = 1;
        cur_ctx = &ctx_storage;
        ((void (*)(id, SEL, CGRect))imp)(view, rt_sel("drawRect:"), (CGRect){ { 0, 0 }, v->frame.size });
        cur_ctx = saved;
    }
    id text = NULL, tcol = NULL, font = NULL; long align = 0;
    if (rt_is_kind_of(view, RT_CLASS(UILabel))) { struct uilabel *l = (struct uilabel *)view; text = l->text; tcol = l->textColor; font = l->font; align = l->align; }
    else if (rt_is_kind_of(view, RT_CLASS(UIButton))) { struct uibutton *b = (struct uibutton *)view; text = b->title; tcol = b->titleColor; font = b->font; align = 1; }
    if (text) {
        struct rt_dl_cmd *d = rt_dl_push(dl, 2);
        struct uicolor *c = (struct uicolor *)tcol;
        d->x = x; d->y = y; d->w = w; d->h = h; d->size = (float)font_size(font); d->align = (int)align;
        if (c) { d->r = (float)c->r; d->g = (float)c->g; d->b = (float)c->b; d->a = (float)c->a * alpha; } else d->a = alpha;
        d->text = rt_xstrdup(rt_str_utf8(text));
    }
    id subs = v->subviews;
    for (size_t i = 0; i < rt_array_count(subs); i++) render_view(rt_array_at(subs, i), x, y, alpha, dl);
}

/* --------------------------------------------------------------- touches */
static id touch_view;
static void deliver_touch(struct touch_ev *e) {
    struct uiapp *a = (struct uiapp *)g_app;
    if (!a || !a->keyWindow) return;
    id t = objc_autorelease(rt_alloc_instance(RT_CLASS(UITouch), 0));
    struct uitouch *tt = (struct uitouch *)t;
    tt->pt = (CGPoint){ e->x, e->y }; tt->phase = e->phase == 0 ? 0 : e->phase == 1 ? 1 : 3;
    id set = objc_autorelease(rt_set_new(&t, 1));
    if (e->phase == 0) {
        touch_view = ((id (*)(id, SEL, CGPoint, id))objc_msgSend)(a->keyWindow, rt_sel("hitTest:withEvent:"), tt->pt, NULL);
        if (!touch_view) return;
        tt->view = touch_view;
        if (rt_is_kind_of(touch_view, RT_CLASS(UIControl))) {
            struct uicontrol *c = (struct uicontrol *)touch_view;
            if (c->enabled) { c->highlighted = 1; ctl_sendActions(touch_view, NULL, 1); }
        }
        ((void (*)(id, SEL, id, id))objc_msgSend)(touch_view, rt_sel("touchesBegan:withEvent:"), set, NULL);
    } else if (touch_view) {
        tt->view = touch_view;
        if (e->phase == 1) ((void (*)(id, SEL, id, id))objc_msgSend)(touch_view, rt_sel("touchesMoved:withEvent:"), set, NULL);
        else {
            ((void (*)(id, SEL, id, id))objc_msgSend)(touch_view, rt_sel("touchesEnded:withEvent:"), set, NULL);
            if (rt_is_kind_of(touch_view, RT_CLASS(UIControl))) {
                struct uicontrol *c = (struct uicontrol *)touch_view;
                CGPoint p = t_locationInView(t, NULL, touch_view);
                c->highlighted = 0;
                if (c->enabled && p.x >= 0 && p.y >= 0 && p.x < c->v.frame.size.width && p.y < c->v.frame.size.height) ctl_sendActions(touch_view, NULL, 1 << 6);
            }
            touch_view = NULL;
        }
    }
    mark_dirty();
}

/* ----------------------------------------------------------- app main loop */
static struct rt_display_list g_dl;
static char *last_dump;
static Class find_delegate_class(void);
/* scripted input for headless tests: IOSRT_SCRIPT="5:tap:100,200;9:quit" */
static void run_script(unsigned frame) {
    const char *s = getenv("IOSRT_SCRIPT");
    if (!s) return;
    char *dup = rt_xstrdup(s), *save = NULL;
    for (char *tok = strtok_r(dup, ";", &save); tok; tok = strtok_r(NULL, ";", &save)) {
        unsigned f; char cmd[16]; float x = 0, y = 0;
        if (sscanf(tok, "%u:%15[a-z]:%f,%f", &f, cmd, &x, &y) < 2 || f != frame) continue;
        if (!strcmp(cmd, "tap")) { iosrt_push_touch(0, x, y); iosrt_push_touch(2, x, y); }
        else if (!strcmp(cmd, "down")) iosrt_push_touch(0, x, y);
        else if (!strcmp(cmd, "up")) iosrt_push_touch(2, x, y);
        else if (!strcmp(cmd, "quit")) rt_quit_requested = 1;
    }
    free(dup);
}
static void dump_frame(unsigned frame) {
    rt_sb sb; rt_sb_init(&sb);
    rt_dl_dump(&g_dl, &sb);
    if (!last_dump || strcmp(last_dump, sb.buf ? sb.buf : "")) {
        if (getenv("IOSRT_DUMP_FRAMES")) { char h[64]; int n = snprintf(h, sizeof h, "--- frame %u ---\n", frame); rt_out(1, h, (size_t)n); rt_out(1, sb.buf, sb.len); }
        free(last_dump); last_dump = rt_sb_take(&sb);
        if (rt_plat->present) rt_plat->present(&g_dl);
    } else free(sb.buf);
}

int iosrt_UIApplicationMain(int argc, char **argv, id principal, id delegateName) {
    (void)argc; (void)argv;
    void *pool = objc_autoreleasePoolPush();
    Class appc = principal ? rt_class_named(rt_str_utf8(principal)) : NULL;
    id app = app_shared(appc ? appc : RT_CLASS(UIApplication), NULL);
    Class dc = delegateName ? rt_class_named(rt_str_utf8(delegateName)) : find_delegate_class();
    if (!dc) rt_guest_exit(75, "UIApplicationMain: no app delegate class (storyboard/NIB launch is not supported)");
    id delegate = RT_SEND0(id, RT_SEND0(id, (id)dc, "alloc"), "init");
    app_setDelegate(app, NULL, delegate);
    rt_log("UIApplicationMain: delegate %s", rt_class_name(dc));
    if (rt_responds(delegate, rt_sel("application:willFinishLaunchingWithOptions:")))
        RT_SEND2(long, delegate, "application:willFinishLaunchingWithOptions:", id, app, id, NULL);
    if (rt_responds(delegate, rt_sel("application:didFinishLaunchingWithOptions:")))
        RT_SEND2(long, delegate, "application:didFinishLaunchingWithOptions:", id, app, id, NULL);
    else if (rt_responds(delegate, rt_sel("applicationDidFinishLaunching:")))
        RT_SEND1(void, delegate, "applicationDidFinishLaunching:", id, app);
    if (rt_responds(delegate, rt_sel("applicationDidBecomeActive:")))
        RT_SEND1(void, delegate, "applicationDidBecomeActive:", id, app);
    objc_autoreleasePoolPop(pool);

    const char *hf = getenv("IOSRT_HEADLESS_FRAMES");
    unsigned max_frames = hf ? (unsigned)atoi(hf) : 0;
    for (unsigned frame = 1; !rt_quit_requested; frame++) {
        pool = objc_autoreleasePoolPush();
        run_script(frame);
        for (;;) {
            pthread_mutex_lock(&input_lock);
            int has = tq_head != tq_tail; struct touch_ev e = touchq[tq_head];
            if (has) tq_head = (tq_head + 1) % 256;
            pthread_mutex_unlock(&input_lock);
            if (!has) break;
            deliver_touch(&e);
        }
        rt_runloop_run_once(max_frames ? 0 : 16);
        struct uiapp *a = (struct uiapp *)app;
        if (dirty && a->keyWindow) {
            dirty = 0;
            rt_dl_clear(&g_dl);
            g_dl.width = scr_w; g_dl.height = scr_h; g_dl.frame = frame;
            render_view(a->keyWindow, 0, 0, 1, &g_dl);
            dump_frame(frame);
        }
        objc_autoreleasePoolPop(pool);
        if (max_frames && frame >= max_frames) break;
        if (max_frames) usleep(1000);
    }
    if (max_frames && last_dump && !getenv("IOSRT_DUMP_FRAMES")) rt_out(1, last_dump, strlen(last_dump));
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
   iosrt_UIApplicationDidReceiveMemoryWarningNotification;

void rt_uikit_init(void) {
    Class o = RT_CLASS(NSObject);
#define NEWCLASS(N, SUPER, SZ) rt_native_class(RT_CLASS(N), &iosrt_meta_##N, #N, SUPER, SZ)
    NEWCLASS(UIResponder, o, 0);
    Class r = RT_CLASS(UIResponder);
    IM(r, "touchesBegan:withEvent:", r_touches); IM(r, "touchesMoved:withEvent:", r_touches);
    IM(r, "touchesEnded:withEvent:", r_touches); IM(r, "touchesCancelled:withEvent:", r_touches);
    IM(r, "nextResponder", r_nextResponder); IM(r, "becomeFirstResponder", r_yes); IM(r, "resignFirstResponder", r_yes);
    IM(r, "canBecomeFirstResponder", r_no);

    NEWCLASS(UIApplication, r, sizeof(struct uiapp) - sizeof(void *));
    Class ap = RT_CLASS(UIApplication);
    CM(ap, "sharedApplication", app_shared); IM(ap, "delegate", app_delegate); IM(ap, "setDelegate:", app_setDelegate);
    IM(ap, "keyWindow", app_keyWindow); IM(ap, "windows", app_windows); IM(ap, "applicationState", app_state);
    IM(ap, "statusBarOrientation", app_orientation); IM(ap, "openURL:", app_openURL); IM(ap, "canOpenURL:", app_openURL);
    IM(ap, "setIdleTimerDisabled:", app_setIdle); IM(ap, "setNetworkActivityIndicatorVisible:", app_setNetworkIndicator);
    IM(ap, "setStatusBarHidden:", app_setNetworkIndicator); IM(ap, "setStatusBarHidden:withAnimation:", v_noop2);
    IM(ap, "registerForRemoteNotifications", v_noop); IM(ap, "beginReceivingRemoteControlEvents", v_noop);
    IM(ap, "endReceivingRemoteControlEvents", v_noop);

    NEWCLASS(UIScreen, o, 0);
    CM(RT_CLASS(UIScreen), "mainScreen", sc_main); IM(RT_CLASS(UIScreen), "bounds", sc_bounds);
    IM(RT_CLASS(UIScreen), "applicationFrame", sc_bounds); IM(RT_CLASS(UIScreen), "nativeBounds", sc_nativeBounds);
    IM(RT_CLASS(UIScreen), "scale", sc_scale); IM(RT_CLASS(UIScreen), "nativeScale", sc_scale);
    NEWCLASS(UIDevice, o, 0);
    Class dv = RT_CLASS(UIDevice);
    CM(dv, "currentDevice", dev_current); IM(dv, "systemVersion", dev_sysver); IM(dv, "systemName", dev_sysname);
    IM(dv, "model", dev_model); IM(dv, "localizedModel", dev_model); IM(dv, "name", dev_name);
    IM(dv, "userInterfaceIdiom", dev_idiom); IM(dv, "orientation", dev_orientation);
    IM(dv, "beginGeneratingDeviceOrientationNotifications", v_noop); IM(dv, "setBatteryMonitoringEnabled:", v_setNoopL);

    NEWCLASS(UIView, r, sizeof(struct uiview) - sizeof(void *));
    Class v = RT_CLASS(UIView);
    IM(v, "initWithFrame:", v_initWithFrame); IM(v, "init", v_init); IM(v, "dealloc", v_dealloc);
    IM(v, "frame", v_frame); IM(v, "setFrame:", v_setFrame); IM(v, "bounds", v_bounds); IM(v, "setBounds:", v_setBounds);
    IM(v, "center", v_center); IM(v, "setCenter:", v_setCenter);
    IM(v, "backgroundColor", v_bg); IM(v, "setBackgroundColor:", v_setBg);
    IM(v, "subviews", v_subviews); IM(v, "superview", v_superview); IM(v, "window", v_window);
    IM(v, "addSubview:", v_addSubview); IM(v, "insertSubview:atIndex:", v_insertSubviewAt);
    IM(v, "removeFromSuperview", v_removeFromSuperview); IM(v, "bringSubviewToFront:", v_bringToFront);
    IM(v, "alpha", v_alpha); IM(v, "setAlpha:", v_setAlpha); IM(v, "isHidden", v_hidden); IM(v, "setHidden:", v_setHidden);
    IM(v, "tag", v_tag); IM(v, "setTag:", v_setTag); IM(v, "viewWithTag:", v_viewWithTag);
    IM(v, "isUserInteractionEnabled", v_interaction); IM(v, "setUserInteractionEnabled:", v_setInteraction);
    IM(v, "setNeedsDisplay", v_setNeedsDisplay); IM(v, "setNeedsLayout", v_setNeedsLayout); IM(v, "layoutIfNeeded", v_layoutIfNeeded);
    IM(v, "layoutSubviews", v_noop); IM(v, "drawRect:", v_drawRect); IM(v, "sizeToFit", v_sizeToFit);
    IM(v, "hitTest:withEvent:", v_hitTest); IM(v, "pointInside:withEvent:", v_pointInside);
    IM(v, "convertPoint:fromView:", v_convertPointFromView); IM(v, "convertPoint:toView:", v_convertPointToView);
    IM(v, "nextResponder", v_nextResponder); IM(v, "didMoveToSuperview", v_noop);
    IM(v, "setAutoresizingMask:", v_setNoopL); IM(v, "setContentMode:", v_setNoopL); IM(v, "setClipsToBounds:", v_setNoopL);
    IM(v, "setMultipleTouchEnabled:", v_setNoopL); IM(v, "setOpaque:", v_setNoopL); IM(v, "addGestureRecognizer:", v_noop1);
    IM(v, "setTranslatesAutoresizingMaskIntoConstraints:", v_setNoopL);

    NEWCLASS(UIWindow, v, sizeof(struct uiwindow) - sizeof(struct uiview));
    Class w = RT_CLASS(UIWindow);
    IM(w, "makeKeyAndVisible", w_makeKeyAndVisible); IM(w, "makeKeyWindow", w_makeKeyAndVisible);
    IM(w, "rootViewController", w_root); IM(w, "setRootViewController:", w_setRoot); IM(w, "screen", w_screen);

    NEWCLASS(UILabel, v, sizeof(struct uilabel) - sizeof(struct uiview));
    Class l = RT_CLASS(UILabel);
    IM(l, "initWithFrame:", l_initWithFrame); IM(l, "text", l_text); IM(l, "setText:", l_setText);
    IM(l, "textColor", l_textColor); IM(l, "setTextColor:", l_setTextColor); IM(l, "font", l_font); IM(l, "setFont:", l_setFont);
    IM(l, "textAlignment", l_align); IM(l, "setTextAlignment:", l_setAlign); IM(l, "setNumberOfLines:", l_setLines);
    IM(l, "setAdjustsFontSizeToFitWidth:", v_setNoopL); IM(l, "setLineBreakMode:", v_setNoopL);

    NEWCLASS(UIControl, v, sizeof(struct uicontrol) - sizeof(struct uiview));
    Class ct = RT_CLASS(UIControl);
    IM(ct, "initWithFrame:", ctl_initWithFrame); IM(ct, "addTarget:action:forControlEvents:", ctl_addTarget);
    IM(ct, "sendActionsForControlEvents:", ctl_sendActions); IM(ct, "isEnabled", ctl_enabled); IM(ct, "setEnabled:", ctl_setEnabled);
    IM(ct, "isSelected", ctl_selected); IM(ct, "setSelected:", ctl_setSelected);
    NEWCLASS(UIButton, ct, sizeof(struct uibutton) - sizeof(struct uicontrol));
    Class bt = RT_CLASS(UIButton);
    CM(bt, "buttonWithType:", b_buttonWithType); IM(bt, "setTitle:forState:", b_setTitle); IM(bt, "titleForState:", b_titleForState);
    IM(bt, "setTitleColor:forState:", b_setTitleColor); IM(bt, "titleLabel", b_titleLabel);

    NEWCLASS(UIViewController, r, sizeof(struct uivc) - sizeof(void *));
    Class vc = RT_CLASS(UIViewController);
    IM(vc, "init", vc_init); IM(vc, "initWithNibName:bundle:", vc_initWithNib); IM(vc, "loadView", vc_loadView);
    IM(vc, "view", vc_view); IM(vc, "setView:", vc_setView); IM(vc, "isViewLoaded", vc_isViewLoaded);
    IM(vc, "viewDidLoad", v_noop); IM(vc, "viewWillAppear:", v_setNoopL); IM(vc, "viewDidAppear:", v_setNoopL);
    IM(vc, "viewWillDisappear:", v_setNoopL); IM(vc, "viewDidDisappear:", v_setNoopL); IM(vc, "didReceiveMemoryWarning", v_noop);
    IM(vc, "title", vc_title); IM(vc, "setTitle:", vc_setTitle); IM(vc, "nextResponder", vc_nextResponder);
    IM(vc, "supportedInterfaceOrientations", vc_supportedOrientations); IM(vc, "prefersStatusBarHidden", r_yes);
    IM(vc, "shouldAutorotate", r_no); IM(vc, "presentViewController:animated:completion:", vc_presentViewController);

    NEWCLASS(UIColor, o, sizeof(struct uicolor) - sizeof(void *));
    Class c = RT_CLASS(UIColor);
    CM(c, "colorWithRed:green:blue:alpha:", c_rgba); CM(c, "colorWithWhite:alpha:", c_white);
    IM(c, "initWithRed:green:blue:alpha:", c_init_rgba);
    CM(c, "whiteColor", c_whiteColor); CM(c, "blackColor", c_blackColor); CM(c, "redColor", c_redColor);
    CM(c, "greenColor", c_greenColor); CM(c, "blueColor", c_blueColor); CM(c, "yellowColor", c_yellowColor);
    CM(c, "orangeColor", c_orangeColor); CM(c, "purpleColor", c_purpleColor); CM(c, "grayColor", c_grayColor);
    CM(c, "darkGrayColor", c_darkGrayColor); CM(c, "lightGrayColor", c_lightGrayColor); CM(c, "clearColor", c_clearColor);
    CM(c, "cyanColor", c_cyanColor); CM(c, "magentaColor", c_magentaColor);
    IM(c, "colorWithAlphaComponent:", c_withAlpha); IM(c, "getRed:green:blue:alpha:", c_getRGBA); IM(c, "CGColor", c_CGColor);
    IM(c, "set", c_set); IM(c, "setFill", c_setFill); IM(c, "setStroke", c_setStroke); IM(c, "description", c_description);

    NEWCLASS(UIFont, o, sizeof(struct uifont) - sizeof(void *));
    Class f = RT_CLASS(UIFont);
    CM(f, "systemFontOfSize:", f_system); CM(f, "boldSystemFontOfSize:", f_bold); CM(f, "fontWithName:size:", f_named);
    IM(f, "pointSize", f_pointSize);

    NEWCLASS(UITouch, o, sizeof(struct uitouch) - sizeof(void *));
    Class t = RT_CLASS(UITouch);
    IM(t, "locationInView:", t_locationInView); IM(t, "view", t_view); IM(t, "phase", t_phase);
    IM(t, "tapCount", t_tapCount); IM(t, "timestamp", t_timestamp);
    NEWCLASS(UIEvent, o, 0);

    iosrt_UIApplicationDidFinishLaunchingNotification = rt_str_new("UIApplicationDidFinishLaunchingNotification");
    iosrt_UIApplicationDidBecomeActiveNotification = rt_str_new("UIApplicationDidBecomeActiveNotification");
    iosrt_UIApplicationWillResignActiveNotification = rt_str_new("UIApplicationWillResignActiveNotification");
    iosrt_UIApplicationDidEnterBackgroundNotification = rt_str_new("UIApplicationDidEnterBackgroundNotification");
    iosrt_UIApplicationWillEnterForegroundNotification = rt_str_new("UIApplicationWillEnterForegroundNotification");
    iosrt_UIApplicationWillTerminateNotification = rt_str_new("UIApplicationWillTerminateNotification");
    iosrt_UIApplicationDidReceiveMemoryWarningNotification = rt_str_new("UIApplicationDidReceiveMemoryWarningNotification");
}
