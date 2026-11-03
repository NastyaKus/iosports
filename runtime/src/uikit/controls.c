/*
 * UIKit controls and views: UILabel, UIImageView, UIControl, UIButton,
 * UIScrollView, UISlider, UISwitch, UIActivityIndicatorView, UIProgressView,
 * UIPageControl, UISegmentedControl, UITextField/UITextView (display only),
 * bars and bar items, plus the drawing dispatcher used by the layer renderer.
 * View controllers, tables and alerts live in controls2.c.
 */
#include "uikit_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

RT_DECLARE_CLASS(NSObject);
RT_DECLARE_CLASS(NSMutableArray);
RT_DECLARE_CLASS(UIView);
RT_DECLARE_CLASS(UIColor);
RT_DECLARE_CLASS(UIImage);
RT_DEFINE_CLASS(UILabel);
RT_DEFINE_CLASS(UIImageView);
RT_DEFINE_CLASS(UIControl);
RT_DEFINE_CLASS(UIButton);
RT_DEFINE_CLASS(UIButtonContent);
RT_DEFINE_CLASS(UIScrollView);
RT_DEFINE_CLASS(UISlider);
RT_DEFINE_CLASS(UISwitch);
RT_DEFINE_CLASS(UIActivityIndicatorView);
RT_DEFINE_CLASS(UIProgressView);
RT_DEFINE_CLASS(UIPageControl);
RT_DEFINE_CLASS(UISegmentedControl);
RT_DEFINE_CLASS(UITextField);
RT_DEFINE_CLASS(UITextView);
RT_DEFINE_CLASS(UIStepper);
RT_DECLARE_CLASS(UIPanGestureRecognizer);

id rt_set_new(id *objs, size_t n);
static int g_continuous;   /* something on screen animates by itself (spinners) */
int rt_controls_continuous(void) { int c = g_continuous; g_continuous = 0; return c; }
static id mkarray(void) { return RT_SEND0(id, RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "alloc"), "init"); }
#define V(x) ((struct uiview *)(x))
#define SETID(field) do { objc_retain(v); objc_release(field); field = v; rt_ui_mark_dirty(); } while (0)
static void nop(id self, SEL s) { (void)self; (void)s; }
static void nopL(id self, SEL s, long v) { (void)self; (void)s; (void)v; }
static void nop1(id self, SEL s, id v) { (void)self; (void)s; (void)v; }
static void nop2(id self, SEL s, id a, long b) { (void)self; (void)s; (void)a; (void)b; }
static id nil0(id self, SEL s) { (void)self; (void)s; return NULL; }
static rt_bool yes0(id self, SEL s) { (void)self; (void)s; return 1; }
static rt_bool no0(id self, SEL s) { (void)self; (void)s; return 0; }
static id mksuper_call(id self, Class cls, const char *sel, CGRect fr) {
    struct { id r; Class c; } sup = { self, cls };
    return ((id (*)(void *, SEL, CGRect))objc_msgSendSuper2)(&sup, rt_sel(sel), fr);
}
static void touches_super(id self, Class cls, SEL s, id t, id e) {
    struct { id r; Class c; } sup = { self, cls };
    ((void (*)(void *, SEL, id, id))objc_msgSendSuper2)(&sup, s, t, e);
}

/* ================================================================ UILabel */
#define LB(x) ((struct uilabel *)(x))
static id l_initWithFrame(id self, SEL s, CGRect fr) {
    self = mksuper_call(self, RT_CLASS(UILabel), "initWithFrame:", fr); (void)s;
    struct uilabel *l = LB(self);
    l->textColor = objc_retain(rt_color_make(0, 0, 0, 1)); l->lines = 1; l->enabled = 1; l->lineBreak = 4;
    l->font = objc_retain(rt_font_make(17, 0, NULL));
    V(self)->interaction = 0;
    return self;
}
static void l_dealloc(id self, SEL s) {
    struct uilabel *l = LB(self);
    objc_release(l->text); objc_release(l->attributedText); objc_release(l->textColor); objc_release(l->highlightedTextColor); objc_release(l->font); objc_release(l->shadowColor);
    struct { id r; Class c; } sup = { self, RT_CLASS(UILabel) }; ((void (*)(void *, SEL))objc_msgSendSuper2)(&sup, s);
}
static id l_text(id self, SEL s) { (void)s; return LB(self)->text; }
static void l_setText(id self, SEL s, id t) {
    (void)s; struct uilabel *l = LB(self); id c = t ? RT_SEND0(id, t, "copy") : NULL;
    objc_release(l->text); l->text = c; objc_release(l->attributedText); l->attributedText = NULL; rt_ui_mark_dirty();
}
static id l_attributedText(id self, SEL s) { (void)s; struct uilabel *l = LB(self); return l->attributedText; }
static void l_setAttributedText(id self, SEL s, id t) {
    (void)s; struct uilabel *l = LB(self); id c = t ? RT_SEND0(id, t, "copy") : NULL;
    objc_release(l->attributedText); l->attributedText = c;
    objc_release(l->text); l->text = t ? objc_retain(RT_SEND0(id, t, "string")) : NULL;
    rt_ui_mark_dirty();
}
static id l_textColor(id self, SEL s) { (void)s; return LB(self)->textColor; }
static void l_setTextColor(id self, SEL s, id v) { (void)s; SETID(LB(self)->textColor); }
static id l_hlColor(id self, SEL s) { (void)s; return LB(self)->highlightedTextColor; }
static void l_setHlColor(id self, SEL s, id v) { (void)s; SETID(LB(self)->highlightedTextColor); }
static id l_font(id self, SEL s) { (void)s; return LB(self)->font; }
static void l_setFont(id self, SEL s, id v) { (void)s; if (!v) v = rt_font_make(17, 0, NULL); SETID(LB(self)->font); }
static id l_shadowColor(id self, SEL s) { (void)s; return LB(self)->shadowColor; }
static void l_setShadowColor(id self, SEL s, id v) { (void)s; SETID(LB(self)->shadowColor); }
static CGSize l_shadowOffset(id self, SEL s) { (void)s; return LB(self)->shadowOffset; }
static void l_setShadowOffset(id self, SEL s, CGSize o) { (void)s; LB(self)->shadowOffset = o; rt_ui_mark_dirty(); }
static long l_align(id self, SEL s) { (void)s; return LB(self)->align; }
static void l_setAlign(id self, SEL s, long a) { (void)s; LB(self)->align = a; rt_ui_mark_dirty(); }
static long l_lines(id self, SEL s) { (void)s; return LB(self)->lines; }
static void l_setLines(id self, SEL s, long n) { (void)s; LB(self)->lines = n; rt_ui_mark_dirty(); }
static long l_lineBreak(id self, SEL s) { (void)s; return LB(self)->lineBreak; }
static void l_setLineBreak(id self, SEL s, long n) { (void)s; LB(self)->lineBreak = n; rt_ui_mark_dirty(); }
static rt_bool l_highlighted(id self, SEL s) { (void)s; return LB(self)->highlighted != 0; }
static void l_setHighlighted(id self, SEL s, long h) { (void)s; LB(self)->highlighted = h & 0xff; rt_ui_mark_dirty(); }
static rt_bool l_enabled(id self, SEL s) { (void)s; return LB(self)->enabled != 0; }
static void l_setEnabled(id self, SEL s, long h) { (void)s; LB(self)->enabled = h & 0xff; rt_ui_mark_dirty(); }
static rt_bool l_adjusts(id self, SEL s) { (void)s; return LB(self)->adjusts != 0; }
static void l_setAdjusts(id self, SEL s, long h) { (void)s; LB(self)->adjusts = h & 0xff; rt_ui_mark_dirty(); }
static double l_minScale(id self, SEL s) { (void)s; return LB(self)->minScale; }
static void l_setMinScale(id self, SEL s, double v) { (void)s; LB(self)->minScale = v; }
static void l_setMinFontSize(id self, SEL s, double v) { (void)s; double fs = rt_font_size(LB(self)->font); LB(self)->minScale = fs > 0 ? v / fs : 0; }
static double l_pmw(id self, SEL s) { (void)s; return LB(self)->preferredMaxWidth; }
static void l_setPmw(id self, SEL s, double v) { (void)s; LB(self)->preferredMaxWidth = v; }
static id label_font_for(struct uilabel *l, double width) {
    id f = l->font;
    id af = l->attributedText && RT_SEND0(unsigned long, l->attributedText, "length") ? rt_attrs_font(RT_SEND2(id, l->attributedText, "attributesAtIndex:effectiveRange:", unsigned long, 0, void *, NULL)) : NULL;
    if (af) f = af;
    if (l->adjusts && l->text && l->lines == 1 && width > 0) {
        double size = rt_font_size(f), min = l->minScale > 0 ? size * l->minScale : size * 0.5;
        CGSize sz = rt_text_measure(rt_str_utf8(l->text), f, 0, 1);
        if (sz.width > width) {
            double ns = fmax(min, size * width / sz.width);
            f = RT_SEND1(id, f, "fontWithSize:", double, floor(ns));
        }
    }
    return f;
}
static CGSize l_sizeThatFits(id self, SEL s, CGSize sz) {
    (void)s; struct uilabel *l = LB(self);
    if (!l->text) return (CGSize){ 0, 0 };
    double w = l->lines == 1 ? 0 : (sz.width > 0 ? sz.width : l->preferredMaxWidth);
    return rt_text_measure(rt_str_utf8(l->text), label_font_for(l, 0), w, l->lines);
}
static CGSize l_intrinsic(id self, SEL s) { return l_sizeThatFits(self, s, (CGSize){ LB(self)->preferredMaxWidth, 0 }); }
static CGRect l_textRect(id self, SEL s, CGRect b, long lines) {
    (void)s; struct uilabel *l = LB(self);
    if (!l->text) return (CGRect){ b.origin, { 0, 0 } };
    CGSize sz = rt_text_measure(rt_str_utf8(l->text), label_font_for(l, b.size.width), b.size.width, lines);
    return (CGRect){ b.origin, { fmin(sz.width, b.size.width), fmin(sz.height, b.size.height) } };
}
static void l_drawTextInRect(id self, SEL s, CGRect r) {
    (void)s; struct uilabel *l = LB(self); struct cgctx *c = rt_cg_top();
    if (!c || !l->text) return;
    id color = l->highlighted && l->highlightedTextColor ? l->highlightedTextColor : l->textColor;
    long align = l->align;
    if (l->attributedText && RT_SEND0(unsigned long, l->attributedText, "length")) {
        id attrs = RT_SEND2(id, l->attributedText, "attributesAtIndex:effectiveRange:", unsigned long, 0, void *, NULL);
        if (rt_attrs_color(attrs)) color = rt_attrs_color(attrs);
        if (rt_dict_get(attrs, rt_str("NSParagraphStyle"))) align = rt_attrs_align(attrs);
    }
    float col[4]; rt_color_rgba(color ? color : rt_color_make(0, 0, 0, 1), col);
    if (!l->enabled) col[3] *= 0.45f;
    if (align == 4) align = 0;
    id f = label_font_for(l, r.size.width);
    const char *txt = rt_str_utf8(l->text);
    if (l->shadowColor && (l->shadowOffset.width || l->shadowOffset.height)) {
        float sc[4]; rt_color_rgba(l->shadowColor, sc);
        double sc_s; rt_sb *tr = rt_cg_trace(c, &sc_s); rt_cg_set_trace(c, NULL, sc_s);
        rt_text_draw(c, txt, f, sc, (CGRect){ { r.origin.x + l->shadowOffset.width, r.origin.y + l->shadowOffset.height }, r.size }, align, l->lines, 1);
        rt_cg_set_trace(c, tr, sc_s);
    }
    rt_text_draw(c, txt, f, col, r, align, l->lines, 1);
}
static void l_drawRect(id self, SEL s, CGRect r) { (void)s; ((void (*)(id, SEL, CGRect))objc_msgSend)(self, rt_sel("drawTextInRect:"), r); }

/* ============================================================ UIImageView */
#define IV(x) ((struct uiimageview *)(x))
static id iv_initWithImage(id self, SEL s, id img) {
    (void)s; CGSize sz = img ? rt_uiimage_size(img) : (CGSize){ 0, 0 };
    self = ((id (*)(id, SEL, CGRect))objc_msgSend)(self, rt_sel("initWithFrame:"), (CGRect){ { 0, 0 }, sz });
    IV(self)->image = objc_retain(img);
    return self;
}
static id iv_initWithImageHL(id self, SEL s, id img, id hl) { self = iv_initWithImage(self, s, img); IV(self)->highlightedImage = objc_retain(hl); return self; }
static id iv_initWithFrame(id self, SEL s, CGRect fr) { (void)s; self = mksuper_call(self, RT_CLASS(UIImageView), "initWithFrame:", fr); V(self)->interaction = 0; return self; }
static void iv_dealloc(id self, SEL s) {
    struct uiimageview *v = IV(self); objc_release(v->image); objc_release(v->highlightedImage); objc_release(v->animationImages);
    struct { id r; Class c; } sup = { self, RT_CLASS(UIImageView) }; ((void (*)(void *, SEL))objc_msgSendSuper2)(&sup, s);
}
static id iv_image(id self, SEL s) { (void)s; return IV(self)->image; }
static void iv_setImage(id self, SEL s, id v) { (void)s; SETID(IV(self)->image); }
static id iv_hlImage(id self, SEL s) { (void)s; return IV(self)->highlightedImage; }
static void iv_setHlImage(id self, SEL s, id v) { (void)s; SETID(IV(self)->highlightedImage); }
static rt_bool iv_highlighted(id self, SEL s) { (void)s; return IV(self)->highlighted != 0; }
static void iv_setHighlighted(id self, SEL s, long v) { (void)s; IV(self)->highlighted = v & 0xff; rt_ui_mark_dirty(); }
static id iv_animImages(id self, SEL s) { (void)s; return IV(self)->animationImages; }
static void iv_setAnimImages(id self, SEL s, id v) { (void)s; SETID(IV(self)->animationImages); }
static double iv_animDuration(id self, SEL s) { (void)s; return IV(self)->animationDuration; }
static void iv_setAnimDuration(id self, SEL s, double d) { (void)s; IV(self)->animationDuration = d; }
static void iv_setAnimRepeat(id self, SEL s, long n) { (void)s; IV(self)->animationRepeat = n; }
static void iv_start(id self, SEL s) { (void)s; IV(self)->animating = 1; IV(self)->animStart = rt_now(); rt_ui_mark_dirty(); }
static void iv_stop(id self, SEL s) { (void)s; IV(self)->animating = 0; rt_ui_mark_dirty(); }
static rt_bool iv_isAnimating(id self, SEL s) { (void)s; return IV(self)->animating != 0; }
static CGSize iv_sizeThatFits(id self, SEL s, CGSize sz) { (void)s; (void)sz; return IV(self)->image ? rt_uiimage_size(IV(self)->image) : (CGSize){ 0, 0 }; }
/* place `content` in `b` per UIViewContentMode */
static CGRect content_rect(long mode, CGRect b, CGSize c) {
    double bw = b.size.width, bh = b.size.height;
    if (c.width <= 0 || c.height <= 0) return b;
    switch (mode) {
    case 0: return b;                                          /* ScaleToFill */
    case 1: case 2: {                                          /* AspectFit / AspectFill */
        double k = mode == 1 ? fmin(bw / c.width, bh / c.height) : fmax(bw / c.width, bh / c.height);
        return (CGRect){ { b.origin.x + (bw - c.width * k) / 2, b.origin.y + (bh - c.height * k) / 2 }, { c.width * k, c.height * k } };
    }
    case 3: return b;                                          /* Redraw */
    default: {
        double x = b.origin.x + (bw - c.width) / 2, y = b.origin.y + (bh - c.height) / 2;
        if (mode == 5 || mode == 9 || mode == 11) y = b.origin.y;             /* Top, TopLeft, TopRight... */
        if (mode == 6 || mode == 10 || mode == 12) y = b.origin.y + bh - c.height;
        if (mode == 7 || mode == 9 || mode == 10) x = b.origin.x;
        if (mode == 8 || mode == 11 || mode == 12) x = b.origin.x + bw - c.width;
        if (mode == 9) { x = b.origin.x; y = b.origin.y; }
        if (mode == 10) { x = b.origin.x + bw - c.width; y = b.origin.y; }
        if (mode == 11) { x = b.origin.x; y = b.origin.y + bh - c.height; }
        if (mode == 12) { x = b.origin.x + bw - c.width; y = b.origin.y + bh - c.height; }
        return (CGRect){ { x, y }, c };
    }
    }
}
static void draw_image_mode(struct cgctx *c, id img, long mode, CGRect b, id view) {
    if (!img) return;
    CGSize sz = rt_uiimage_size(img);
    CGRect r = content_rect(mode, b, sz);
    float tint[4]; rt_color_rgba(rt_view_tint(view), tint);
    int clip = mode == 2 || (mode >= 4 && (sz.width > b.size.width || sz.height > b.size.height));
    if (clip) { iosrt_CGContextSaveGState(c); iosrt_CGContextClipToRect(c, b); }
    rt_uiimage_draw(img, c, r, 1, tint);
    if (clip) iosrt_CGContextRestoreGState(c);
}
static void draw_imageview(id view, struct cgctx *c, CGRect b) {
    struct uiimageview *v = IV(view);
    id img = v->highlighted && v->highlightedImage ? v->highlightedImage : v->image;
    if (v->animating && v->animationImages && rt_array_count(v->animationImages)) {
        size_t n = rt_array_count(v->animationImages);
        double d = v->animationDuration > 0 ? v->animationDuration : n / 30.0, t = rt_now() - v->animStart;
        if (v->animationRepeat > 0 && t > d * v->animationRepeat) { v->animating = 0; }
        else { img = rt_array_at(v->animationImages, (size_t)(fmod(t, d) / d * n) % n); g_continuous = 1; }
    }
    draw_image_mode(c, img, V(view)->contentMode, b, view);
}

/* ============================================================== UIControl */
#define CT(x) ((struct uicontrol *)(x))
static id ctl_initWithFrame(id self, SEL s, CGRect fr) {
    (void)s; self = mksuper_call(self, RT_CLASS(UIControl), "initWithFrame:", fr);
    CT(self)->targets = mkarray(); CT(self)->enabled = 1;
    return self;
}
static void ctl_dealloc(id self, SEL s) { objc_release(CT(self)->targets); struct { id r; Class c; } sup = { self, RT_CLASS(UIControl) }; ((void (*)(void *, SEL))objc_msgSendSuper2)(&sup, s); }
static void ctl_addTarget(id self, SEL s, id target, SEL action, unsigned long events) {
    (void)s; struct uicontrol *c = CT(self);
    if (!c->targets) c->targets = mkarray();
    for (size_t i = 0; i < rt_array_count(c->targets); i++) {
        struct target_action *t = (struct target_action *)rt_array_at(c->targets, i);
        if (t->target == target && t->action == action) { t->events |= events; return; }
    }
    id ta = rt_alloc_instance(RT_CLASS(NSObject), sizeof(struct target_action) - sizeof(void *));
    struct target_action *t = (struct target_action *)ta; t->target = target; t->action = action; t->events = events;
    RT_SEND1(void, c->targets, "addObject:", id, ta); objc_release(ta);
}
static void ctl_removeTarget(id self, SEL s, id target, SEL action, unsigned long events) {
    (void)s; id arr = CT(self)->targets;
    for (size_t i = arr ? rt_array_count(arr) : 0; i-- > 0;) {
        struct target_action *t = (struct target_action *)rt_array_at(arr, i);
        if ((!target || t->target == target) && (!action || t->action == action)) { t->events &= ~events; if (!t->events) RT_SEND1(void, arr, "removeObjectAtIndex:", unsigned long, (unsigned long)i); }
    }
}
static id ctl_allTargets(id self, SEL s) {
    (void)s; id out = RT_SEND0(id, (id)rt_class_named("NSMutableSet"), "set");
    for (size_t i = 0; CT(self)->targets && i < rt_array_count(CT(self)->targets); i++) { id t = ((struct target_action *)rt_array_at(CT(self)->targets, i))->target; if (t) RT_SEND1(void, out, "addObject:", id, t); }
    return out;
}
static id ctl_actionsForTarget(id self, SEL s, id target, unsigned long ev) {
    (void)s; id out = mkarray();
    for (size_t i = 0; CT(self)->targets && i < rt_array_count(CT(self)->targets); i++) {
        struct target_action *t = (struct target_action *)rt_array_at(CT(self)->targets, i);
        if (t->target == target && (t->events & ev)) RT_SEND1(void, out, "addObject:", id, rt_str((const char *)t->action));
    }
    return objc_autorelease(out);
}
static void ctl_sendAction(id self, SEL s, SEL action, id target, id event) { (void)s; rt_send_action(action, target, self, event); }
static void send_events(id self, unsigned long events, id event) {
    struct uicontrol *c = CT(self);
    id arr = c->targets ? objc_retain(RT_SEND0(id, c->targets, "copy")) : NULL;
    for (size_t i = 0; arr && i < rt_array_count(arr); i++) {
        struct target_action *t = (struct target_action *)rt_array_at(arr, i);
        if (t->events & events) ((void (*)(id, SEL, SEL, id, id))objc_msgSend)(self, rt_sel("sendAction:to:forEvent:"), t->action, t->target, event);
    }
    objc_release(arr);
}
static void ctl_sendActions(id self, SEL s, unsigned long events) { (void)s; send_events(self, events, NULL); }
static rt_bool ctl_enabled(id self, SEL s) { (void)s; return CT(self)->enabled != 0; }
static void ctl_setEnabled(id self, SEL s, long e) { (void)s; CT(self)->enabled = e & 0xff; rt_ui_mark_dirty(); }
static rt_bool ctl_selected(id self, SEL s) { (void)s; return CT(self)->selected != 0; }
static void ctl_setSelected(id self, SEL s, long e) { (void)s; CT(self)->selected = e & 0xff; rt_ui_mark_dirty(); }
static rt_bool ctl_highlighted(id self, SEL s) { (void)s; return CT(self)->highlighted != 0; }
static void ctl_setHighlighted(id self, SEL s, long e) { (void)s; CT(self)->highlighted = e & 0xff; rt_ui_mark_dirty(); }
static rt_bool ctl_tracking(id self, SEL s) { (void)s; return CT(self)->tracking != 0; }
static rt_bool ctl_touchInside(id self, SEL s) { (void)s; return CT(self)->touchInside != 0; }
static unsigned long ctl_state(id self, SEL s) { (void)s; struct uicontrol *c = CT(self); return (c->highlighted ? 1 : 0) | (!c->enabled ? 2 : 0) | (c->selected ? 4 : 0); }
static long ctl_hAlign(id self, SEL s) { (void)s; return CT(self)->hAlign; }
static void ctl_setHAlign(id self, SEL s, long v) { (void)s; CT(self)->hAlign = v; rt_ui_mark_dirty(); }
static long ctl_vAlign(id self, SEL s) { (void)s; return CT(self)->vAlign; }
static void ctl_setVAlign(id self, SEL s, long v) { (void)s; CT(self)->vAlign = v; rt_ui_mark_dirty(); }
static rt_bool ctl_beginTracking(id self, SEL s, id t, id e) { (void)self; (void)s; (void)t; (void)e; return 1; }
static rt_bool ctl_continueTracking(id self, SEL s, id t, id e) { (void)self; (void)s; (void)t; (void)e; return 1; }
static void ctl_endTracking(id self, SEL s, id t, id e) { (void)self; (void)s; (void)t; (void)e; }
static void ctl_cancelTracking(id self, SEL s, id e) { (void)self; (void)s; (void)e; }
static int inside_expanded(id self, id touch) {
    CGPoint p = RT_SEND1(CGPoint, touch, "locationInView:", id, self);
    CGRect b = rt_view_bounds(self);
    return p.x >= b.origin.x - 70 && p.y >= b.origin.y - 70 && p.x < b.origin.x + b.size.width + 70 && p.y < b.origin.y + b.size.height + 70;
}
enum { EV_DOWN = 1, EV_DOWN_REPEAT = 2, EV_DRAG_INSIDE = 4, EV_DRAG_OUTSIDE = 8, EV_DRAG_ENTER = 16, EV_DRAG_EXIT = 32, EV_UP_INSIDE = 64, EV_UP_OUTSIDE = 128, EV_CANCEL = 256, EV_VALUE = 4096 };
static void ctl_touchesBegan(id self, SEL s, id touches, id ev) {
    (void)s; struct uicontrol *c = CT(self);
    if (!c->enabled) return;
    id t = RT_SEND0(id, touches, "anyObject");
    c->touchInside = 1;
    c->tracking = (RT_SEND2(long, self, "beginTrackingWithTouch:withEvent:", id, t, id, ev) & 0xff) != 0;
    RT_SEND1(void, self, "setHighlighted:", long, 1);
    send_events(self, EV_DOWN | (RT_SEND0(unsigned long, t, "tapCount") > 1 ? EV_DOWN_REPEAT : 0), ev);
}
static void ctl_touchesMoved(id self, SEL s, id touches, id ev) {
    (void)s; struct uicontrol *c = CT(self);
    if (!c->enabled) return;
    id t = RT_SEND0(id, touches, "anyObject");
    int inside = inside_expanded(self, t), was = (int)c->touchInside;
    c->touchInside = inside;
    RT_SEND1(void, self, "setHighlighted:", long, inside);
    if (c->tracking) c->tracking = (RT_SEND2(long, self, "continueTrackingWithTouch:withEvent:", id, t, id, ev) & 0xff) != 0;
    send_events(self, (inside ? EV_DRAG_INSIDE : EV_DRAG_OUTSIDE) | (inside && !was ? EV_DRAG_ENTER : 0) | (!inside && was ? EV_DRAG_EXIT : 0), ev);
}
static void ctl_touchesEnded(id self, SEL s, id touches, id ev) {
    (void)s; struct uicontrol *c = CT(self);
    if (!c->enabled) return;
    id t = RT_SEND0(id, touches, "anyObject");
    int inside = inside_expanded(self, t);
    c->touchInside = inside;
    if (c->tracking) RT_SEND2(void, self, "endTrackingWithTouch:withEvent:", id, t, id, ev);
    c->tracking = 0;
    RT_SEND1(void, self, "setHighlighted:", long, 0);
    send_events(self, inside ? EV_UP_INSIDE : EV_UP_OUTSIDE, ev);
}
static void ctl_touchesCancelled(id self, SEL s, id touches, id ev) {
    (void)s; (void)touches; struct uicontrol *c = CT(self);
    if (c->tracking) RT_SEND1(void, self, "cancelTrackingWithEvent:", id, ev);
    c->tracking = 0; c->touchInside = 0;
    RT_SEND1(void, self, "setHighlighted:", long, 0);
    send_events(self, EV_CANCEL, ev);
}

/* =============================================================== UIButton */
#define BT(x) ((struct uibutton *)(x))
struct bcobj { Class isa; struct bcontent c; };
static void bc_dealloc(id self, SEL s) {
    (void)s; struct bcontent *c = &((struct bcobj *)self)->c;
    objc_release(c->title); objc_release(c->attrTitle); objc_release(c->titleColor); objc_release(c->shadowColor); objc_release(c->image); objc_release(c->bgImage);
    rt_dealloc_instance(self);
}
static id b_initWithFrame(id self, SEL s, CGRect fr) {
    (void)s; self = mksuper_call(self, RT_CLASS(UIButton), "initWithFrame:", fr);
    struct uibutton *b = BT(self);
    b->adjustsHighlighted = 1; b->adjustsDisabled = 1; b->c.hAlign = 0; b->c.vAlign = 0;
    b->font = objc_retain(rt_font_make(15, 0, NULL));
    return self;
}
static void b_dealloc(id self, SEL s) {
    struct uibutton *b = BT(self);
    for (int i = 0; i < 8; i++) { struct bcontent *c = &b->st[i]; objc_release(c->title); objc_release(c->attrTitle); objc_release(c->titleColor); objc_release(c->shadowColor); objc_release(c->image); objc_release(c->bgImage); }
    objc_release(b->titleLabel); objc_release(b->imageView); objc_release(b->font);
    struct { id r; Class c; } sup = { self, RT_CLASS(UIButton) }; ((void (*)(void *, SEL))objc_msgSendSuper2)(&sup, s);
}
static id b_buttonWithType(Class c, SEL s, long type) {
    (void)s;
    id b = ((id (*)(id, SEL, CGRect))objc_msgSend)(rt_alloc_instance(c, 0), rt_sel("initWithFrame:"), (CGRect){ { 0, 0 }, { 0, 0 } });
    struct uibutton *bt = BT(b);
    bt->type = type;
    if (type == 1) { objc_release(bt->font); bt->font = objc_retain(rt_font_make(15, 0, NULL)); }   /* system: tint colored title */
    else bt->st[0].titleColor = objc_retain(rt_color_make(1, 1, 1, 1));
    return objc_autorelease(b);
}
static long b_type(id self, SEL s) { (void)s; return BT(self)->type; }
static unsigned st_index(unsigned long state) { return (unsigned)(state & 7); }
#define BSET(field) \
    struct bcontent *c = &BT(self)->st[st_index(state)]; objc_retain(v); objc_release(c->field); c->field = v; rt_ui_mark_dirty();
static void b_setTitle(id self, SEL s, id t, unsigned long state) { (void)s; id v = t ? RT_SEND0(id, t, "copy") : NULL; struct bcontent *c = &BT(self)->st[st_index(state)]; objc_release(c->title); c->title = v; rt_ui_mark_dirty(); }
static void b_setAttrTitle(id self, SEL s, id v, unsigned long state) { (void)s; BSET(attrTitle) if (v) { id str = RT_SEND0(id, v, "string"); b_setTitle(self, s, str, state); } }
static void b_setTitleColor(id self, SEL s, id v, unsigned long state) { (void)s; BSET(titleColor) }
static void b_setShadowColor(id self, SEL s, id v, unsigned long state) { (void)s; BSET(shadowColor) }
static void b_setImage(id self, SEL s, id v, unsigned long state) { (void)s; BSET(image) }
static void b_setBgImage(id self, SEL s, id v, unsigned long state) { (void)s; BSET(bgImage) }
/* state lookup falls back to normal like UIKit */
static struct bcontent *bstate(id self, unsigned long state) { return &BT(self)->st[st_index(state)]; }
#define BGET(fn, field) \
    static id fn(id self, SEL s, unsigned long state) { (void)s; id v = bstate(self, state)->field; return v ? v : bstate(self, 0)->field; }
BGET(b_titleForState, title) BGET(b_attrTitleForState, attrTitle) BGET(b_titleColorForState, titleColor) BGET(b_shadowColorForState, shadowColor)
BGET(b_imageForState, image) BGET(b_bgImageForState, bgImage)
static unsigned long cur_state(id self) { return ctl_state(self, NULL); }
static id b_currentTitle(id self, SEL s) { return b_titleForState(self, s, cur_state(self)); }
static id b_currentAttrTitle(id self, SEL s) { return b_attrTitleForState(self, s, cur_state(self)); }
static id b_currentImage(id self, SEL s) { return b_imageForState(self, s, cur_state(self)); }
static id b_currentBgImage(id self, SEL s) { return b_bgImageForState(self, s, cur_state(self)); }
static id b_currentTitleColor(id self, SEL s) {
    id c = b_titleColorForState(self, s, cur_state(self));
    if (!c) c = BT(self)->type == 1 ? rt_view_tint(self) : rt_color_make(1, 1, 1, 1);
    return c;
}
static id b_titleLabel(id self, SEL s) {
    (void)s; struct uibutton *b = BT(self);
    if (!b->titleLabel) {
        b->titleLabel = rt_view_new((Class)rt_class_named("UILabel"), (CGRect){ { 0, 0 }, { 0, 0 } });
        RT_SEND1(void, b->titleLabel, "setFont:", id, b->font);
    }
    return b->titleLabel;
}
static id b_imageView(id self, SEL s) { (void)s; struct uibutton *b = BT(self); if (!b->imageView) b->imageView = rt_view_new(RT_CLASS(UIImageView), (CGRect){ { 0, 0 }, { 0, 0 } }); return b->imageView; }
static id button_font(id self) { struct uibutton *b = BT(self); return b->titleLabel ? LB(b->titleLabel)->font : b->font; }
static void b_setFont(id self, SEL s, id f) { (void)s; struct uibutton *b = BT(self); objc_retain(f); objc_release(b->font); b->font = f; if (b->titleLabel) RT_SEND1(void, b->titleLabel, "setFont:", id, f); rt_ui_mark_dirty(); }
static id b_font(id self, SEL s) { (void)s; return button_font(self); }
#define INSETS(get, set, field) \
    static UIEdgeInsets get(id self, SEL s) { (void)s; return BT(self)->field; } \
    static void set(id self, SEL s, UIEdgeInsets e) { (void)s; BT(self)->field = e; rt_ui_mark_dirty(); }
INSETS(b_contentInsets, b_setContentInsets, contentInsets) INSETS(b_titleInsets, b_setTitleInsets, titleInsets) INSETS(b_imageInsets, b_setImageInsets, imageInsets)
static rt_bool b_adjustsHL(id self, SEL s) { (void)s; return BT(self)->adjustsHighlighted != 0; }
static void b_setAdjustsHL(id self, SEL s, long v) { (void)s; BT(self)->adjustsHighlighted = v & 0xff; }
static void b_setAdjustsDisabled(id self, SEL s, long v) { (void)s; BT(self)->adjustsDisabled = v & 0xff; }
static void b_setShowsTouch(id self, SEL s, long v) { (void)s; BT(self)->showsTouch = v & 0xff; }
static CGSize b_sizeThatFits(id self, SEL s, CGSize sz) {
    (void)s; (void)sz; struct uibutton *b = BT(self);
    id title = b_currentTitle(self, NULL), img = b_currentImage(self, NULL), bg = b_currentBgImage(self, NULL);
    CGSize ts = title ? rt_text_measure(rt_str_utf8(title), button_font(self), 0, 1) : (CGSize){ 0, 0 };
    CGSize is = img ? rt_uiimage_size(img) : (CGSize){ 0, 0 };
    double w = ts.width + is.width + b->contentInsets.left + b->contentInsets.right, h = fmax(ts.height, is.height) + b->contentInsets.top + b->contentInsets.bottom;
    if (bg) { CGSize bs = rt_uiimage_size(bg); w = fmax(w, bs.width); h = fmax(h, bs.height); }
    if (b->type == 1 && !img) { w += 0; h = fmax(h, 30); }
    return (CGSize){ ceil(w), ceil(h) };
}
static CGSize b_intrinsic(id self, SEL s) { return b_sizeThatFits(self, s, (CGSize){ 0, 0 }); }
static void b_setHighlighted(id self, SEL s, long v) { ctl_setHighlighted(self, s, v); }
static void draw_button(id view, struct cgctx *c, CGRect b) {
    struct uibutton *bt = BT(view);
    unsigned long st = cur_state(view);
    id bg = b_bgImageForState(view, NULL, st), img = b_imageForState(view, NULL, st), title = b_titleForState(view, NULL, st);
    float alpha = 1;
    if (bt->type == 1 && bt->c.highlighted) alpha = 0.2f;
    if (!bt->c.enabled && bt->adjustsDisabled && !bstate(view, 2)->image && !bstate(view, 2)->bgImage) alpha *= 0.5f;
    iosrt_CGContextSaveGState(c);
    if (alpha < 1) iosrt_CGContextSetAlpha(c, rt_cg_alpha(c) * alpha);
    if (bg) rt_uiimage_draw(bg, c, b, 1, NULL);
    CGRect inner = { { b.origin.x + bt->contentInsets.left, b.origin.y + bt->contentInsets.top },
                     { b.size.width - bt->contentInsets.left - bt->contentInsets.right, b.size.height - bt->contentInsets.top - bt->contentInsets.bottom } };
    CGSize is = img ? rt_uiimage_size(img) : (CGSize){ 0, 0 };
    id font = button_font(view);
    CGSize ts = title ? rt_text_measure(rt_str_utf8(title), font, 0, 1) : (CGSize){ 0, 0 };
    if (ts.width > inner.size.width - is.width) ts.width = fmax(0, inner.size.width - is.width);
    double total = is.width + ts.width;
    double x0;
    long ha = bt->c.hAlign;
    if (ha == 1 || ha == 4) x0 = inner.origin.x; else if (ha == 2 || ha == 5) x0 = inner.origin.x + inner.size.width - total; else x0 = inner.origin.x + (inner.size.width - total) / 2;
    if (ha == 3 && img && !title) { is = inner.size; x0 = inner.origin.x; }   /* fill */
    if (img) {
        double y = bt->c.vAlign == 1 ? inner.origin.y : bt->c.vAlign == 2 ? inner.origin.y + inner.size.height - is.height : inner.origin.y + (inner.size.height - is.height) / 2;
        if (bt->c.vAlign == 3) { y = inner.origin.y; is.height = inner.size.height; }
        CGRect ir = { { x0 + bt->imageInsets.left - bt->imageInsets.right, y + bt->imageInsets.top - bt->imageInsets.bottom }, is };
        float tint[4]; rt_color_rgba(rt_view_tint(view), tint);
        rt_uiimage_draw(img, c, ir, 1, bt->type == 1 ? tint : NULL);
    }
    if (title && RT_SEND0(unsigned long, title, "length")) {
        float col[4]; rt_color_rgba(b_currentTitleColor(view, NULL), col);
        long align = 1; CGRect tr = inner;
        if (img) { tr.origin.x = x0 + is.width; tr.size.width = ts.width + 1; align = 0; }
        else if (ha == 1 || ha == 4) align = 0; else if (ha == 2 || ha == 5) align = 2;
        tr.origin.x += bt->titleInsets.left - bt->titleInsets.right; tr.origin.y += bt->titleInsets.top - bt->titleInsets.bottom;
        long lines = bt->titleLabel ? LB(bt->titleLabel)->lines : 1;
        id shadow = b_shadowColorForState(view, NULL, st);
        (void)shadow;
        rt_text_draw(c, rt_str_utf8(title), font, col, tr, align, lines, 1);
    }
    if (bt->adjustsHighlighted && bt->c.highlighted && bt->type != 1 && (bg || img) && !bstate(view, 1)->image && !bstate(view, 1)->bgImage) {
        /* UIKit darkens custom buttons' images while pressed */
        iosrt_CGContextSetRGBFillColor(c, 0, 0, 0, 0.3);
        iosrt_CGContextSetBlendMode(c, 0);
        if (bg) rt_uiimage_draw(bg, c, b, 0.3f, (float[4]){ 0, 0, 0, 1 });
        else if (img) { /* tint the image area */ }
    }
    iosrt_CGContextRestoreGState(c);
}

/* ============================================================ UIScrollView */
#define SV(x) ((struct uiscroll *)(x))
static void sv_handlePan(id self, SEL s, id g);
static id sv_initWithFrame(id self, SEL s, CGRect fr) {
    (void)s; self = mksuper_call(self, RT_CLASS(UIScrollView), "initWithFrame:", fr);
    struct uiscroll *v = SV(self);
    v->scrollEnabled = 1; v->bounces = 1; v->showsH = v->showsV = 1; v->delaysContent = 1; v->cancelsContent = 1; v->zoom = 1; v->minZoom = 1; v->maxZoom = 1; v->scrollsToTop = 1;
    RT_SEND1(void, self, "setClipsToBounds:", long, 1);
    v->pan = RT_SEND2(id, RT_SEND0(id, (id)RT_CLASS(UIPanGestureRecognizer), "alloc"), "initWithTarget:action:", id, self, SEL, rt_sel("iosrt_handlePan:"));
    RT_SEND1(void, self, "addGestureRecognizer:", id, v->pan);
    return self;
}
static void sv_dealloc(id self, SEL s) { objc_release(SV(self)->pan); struct { id r; Class c; } sup = { self, RT_CLASS(UIScrollView) }; ((void (*)(void *, SEL))objc_msgSendSuper2)(&sup, s); }
static CGPoint sv_offset(id self, SEL s) { (void)s; return rt_view_bounds(self).origin; }
static void sv_setOffset(id self, SEL s, CGPoint p) {
    (void)s; CGRect b = rt_view_bounds(self);
    if (b.origin.x == p.x && b.origin.y == p.y) return;
    b.origin = p; RT_SEND1(void, self, "setBounds:", CGRect, b);
    id d = SV(self)->delegate;
    if (d && rt_responds(d, rt_sel("scrollViewDidScroll:"))) RT_SEND1(void, d, "scrollViewDidScroll:", id, self);
    RT_SEND0(void, self, "setNeedsLayout");
}
static void sv_setOffsetAnim(id self, SEL s, CGPoint p, long a) {
    sv_setOffset(self, s, p);
    id d = SV(self)->delegate;
    if ((a & 0xff) && d && rt_responds(d, rt_sel("scrollViewDidEndScrollingAnimation:"))) RT_SEND1(void, d, "scrollViewDidEndScrollingAnimation:", id, self);
}
static CGSize sv_contentSize(id self, SEL s) { (void)s; return SV(self)->contentSize; }
static void sv_setContentSize(id self, SEL s, CGSize sz) { (void)s; SV(self)->contentSize = sz; rt_ui_mark_dirty(); }
static UIEdgeInsets sv_inset(id self, SEL s) { (void)s; return SV(self)->inset; }
static void sv_setInset(id self, SEL s, UIEdgeInsets e) { (void)s; SV(self)->inset = e; }
static id sv_delegate(id self, SEL s) { (void)s; return SV(self)->delegate; }
static void sv_setDelegate(id self, SEL s, id d) { (void)s; SV(self)->delegate = d; }
#define SVB(get, set, field) \
    static rt_bool get(id self, SEL s) { (void)s; return SV(self)->field != 0; } \
    static void set(id self, SEL s, long v) { (void)s; SV(self)->field = v & 0xff; }
SVB(sv_paging, sv_setPaging, paging) SVB(sv_scrollEnabled, sv_setScrollEnabled, scrollEnabled) SVB(sv_bounces, sv_setBounces, bounces)
SVB(sv_alwaysV, sv_setAlwaysV, alwaysBounceV) SVB(sv_alwaysH, sv_setAlwaysH, alwaysBounceH) SVB(sv_showsH, sv_setShowsH, showsH)
SVB(sv_showsV, sv_setShowsV, showsV) SVB(sv_delays, sv_setDelays, delaysContent) SVB(sv_cancels, sv_setCancels, cancelsContent)
SVB(sv_lock, sv_setLock, directionalLock) SVB(sv_toTop, sv_setToTop, scrollsToTop)
static rt_bool sv_dragging(id self, SEL s) { (void)s; return SV(self)->dragging != 0; }
static rt_bool sv_decelerating(id self, SEL s) { (void)s; return SV(self)->decelerating != 0; }
static rt_bool sv_tracking(id self, SEL s) { (void)s; return SV(self)->dragging != 0; }
static double sv_zoom(id self, SEL s) { (void)s; return SV(self)->zoom; }
static void sv_setZoom(id self, SEL s, double z) { (void)s; SV(self)->zoom = z; }
static void sv_setZoomAnim(id self, SEL s, double z, long a) { (void)a; sv_setZoom(self, s, z); }
static double sv_minZoom(id self, SEL s) { (void)s; return SV(self)->minZoom; }
static void sv_setMinZoom(id self, SEL s, double z) { (void)s; SV(self)->minZoom = z; }
static double sv_maxZoom(id self, SEL s) { (void)s; return SV(self)->maxZoom; }
static void sv_setMaxZoom(id self, SEL s, double z) { (void)s; SV(self)->maxZoom = z; }
static id sv_pan(id self, SEL s) { (void)s; return SV(self)->pan; }
static void sv_flash(id self, SEL s) { (void)self; (void)s; }
static void sv_scrollRectToVisible(id self, SEL s, CGRect r, long a) {
    CGRect b = rt_view_bounds(self); CGPoint o = b.origin;
    if (r.origin.x < o.x) o.x = r.origin.x; else if (r.origin.x + r.size.width > o.x + b.size.width) o.x = r.origin.x + r.size.width - b.size.width;
    if (r.origin.y < o.y) o.y = r.origin.y; else if (r.origin.y + r.size.height > o.y + b.size.height) o.y = r.origin.y + r.size.height - b.size.height;
    sv_setOffsetAnim(self, s, o, a);
}
static CGPoint clamp_offset(id self, CGPoint p) {
    struct uiscroll *v = SV(self); CGRect b = rt_view_bounds(self);
    double minx = -v->inset.left, miny = -v->inset.top;
    double maxx = fmax(minx, v->contentSize.width + v->inset.right - b.size.width), maxy = fmax(miny, v->contentSize.height + v->inset.bottom - b.size.height);
    p.x = fmin(fmax(p.x, minx), maxx); p.y = fmin(fmax(p.y, miny), maxy);
    return p;
}
static void sv_handlePan(id self, SEL s, id g) {
    (void)s; struct uiscroll *v = SV(self);
    if (!v->scrollEnabled) return;
    long st = RT_SEND0(long, g, "state");
    id d = v->delegate;
    CGPoint tr = RT_SEND1(CGPoint, g, "translationInView:", id, self);
    if (st == 1) {
        v->panStart = sv_offset(self, NULL); v->dragging = 1;
        if (d && rt_responds(d, rt_sel("scrollViewWillBeginDragging:"))) RT_SEND1(void, d, "scrollViewWillBeginDragging:", id, self);
    }
    if (st == 1 || st == 2) {
        CGRect b = rt_view_bounds(self);
        int canx = v->contentSize.width > b.size.width || v->alwaysBounceH, cany = v->contentSize.height > b.size.height || v->alwaysBounceV;
        CGPoint p = { v->panStart.x - (canx ? tr.x : 0), v->panStart.y - (cany ? tr.y : 0) };
        CGPoint cp = clamp_offset(self, p);
        if (v->bounces) { p.x = cp.x + (p.x - cp.x) * 0.4; p.y = cp.y + (p.y - cp.y) * 0.4; } else p = cp;
        /* translation in the pan is relative to the gesture start, which is fine for bounds origin in the scroll view's own space */
        sv_setOffset(self, NULL, p);
    }
    if (st == 3 || st == 4) {
        v->dragging = 0;
        CGPoint p = clamp_offset(self, sv_offset(self, NULL));
        if (v->paging) {
            CGRect b = rt_view_bounds(self); CGPoint vel = RT_SEND1(CGPoint, g, "velocityInView:", id, self);
            double pw = b.size.width, ph = b.size.height;
            if (pw > 0) { double page = round(p.x / pw); if (vel.x < -300) page = floor(p.x / pw) + 1; else if (vel.x > 300) page = ceil(p.x / pw) - 1; p.x = page * pw; }
            if (ph > 0 && v->contentSize.height > ph) { double page = round(p.y / ph); if (vel.y < -300) page = floor(p.y / ph) + 1; else if (vel.y > 300) page = ceil(p.y / ph) - 1; p.y = page * ph; }
            p = clamp_offset(self, p);
        }
        if (d && rt_responds(d, rt_sel("scrollViewWillEndDragging:withVelocity:targetContentOffset:"))) {
            CGPoint vel = RT_SEND1(CGPoint, g, "velocityInView:", id, self);
            ((void (*)(id, SEL, id, CGPoint, CGPoint *))objc_msgSend)(d, rt_sel("scrollViewWillEndDragging:withVelocity:targetContentOffset:"), self, vel, &p);
        }
        sv_setOffset(self, NULL, p);
        if (d && rt_responds(d, rt_sel("scrollViewDidEndDragging:willDecelerate:"))) RT_SEND2(void, d, "scrollViewDidEndDragging:willDecelerate:", id, self, long, 0);
        if (d && rt_responds(d, rt_sel("scrollViewDidEndDecelerating:"))) RT_SEND1(void, d, "scrollViewDidEndDecelerating:", id, self);
    }
}
static rt_bool sv_touchesShouldCancel(id self, SEL s, id view) { (void)self; (void)s; return !rt_is_kind_of(view, RT_CLASS(UIControl)); }
static rt_bool sv_touchesShouldBegin(id self, SEL s, id t, id e, id v) { (void)self; (void)s; (void)t; (void)e; (void)v; return 1; }

/* ================================================================ UISlider */
struct uislider { struct uicontrol c; float value, min, max; long continuous; id minTint, maxTint, thumbTint; id thumbImg[8], minImg[8], maxImg[8]; };
#define SL(x) ((struct uislider *)(x))
static id sl_initWithFrame(id self, SEL s, CGRect fr) { (void)s; if (fr.size.height < 31) fr.size.height = 31; self = mksuper_call(self, RT_CLASS(UISlider), "initWithFrame:", fr); SL(self)->max = 1; SL(self)->continuous = 1; return self; }
static float sl_value(id self, SEL s) { (void)s; return SL(self)->value; }
static void sl_setValue(id self, SEL s, float v) { (void)s; struct uislider *x = SL(self); x->value = fminf(fmaxf(v, x->min), x->max); rt_ui_mark_dirty(); }
static void sl_setValueAnim(id self, SEL s, float v, long a) { (void)a; sl_setValue(self, s, v); }
static float sl_min(id self, SEL s) { (void)s; return SL(self)->min; }
static void sl_setMin(id self, SEL s, float v) { (void)s; SL(self)->min = v; if (SL(self)->value < v) SL(self)->value = v; rt_ui_mark_dirty(); }
static float sl_max(id self, SEL s) { (void)s; return SL(self)->max; }
static void sl_setMax(id self, SEL s, float v) { (void)s; SL(self)->max = v; if (SL(self)->value > v) SL(self)->value = v; rt_ui_mark_dirty(); }
static rt_bool sl_continuous(id self, SEL s) { (void)s; return SL(self)->continuous != 0; }
static void sl_setContinuous(id self, SEL s, long v) { (void)s; SL(self)->continuous = v & 0xff; }
static void sl_setMinTint(id self, SEL s, id v) { (void)s; SETID(SL(self)->minTint); }
static void sl_setMaxTint(id self, SEL s, id v) { (void)s; SETID(SL(self)->maxTint); }
static void sl_setThumbTint(id self, SEL s, id v) { (void)s; SETID(SL(self)->thumbTint); }
static id sl_minTint(id self, SEL s) { (void)s; return SL(self)->minTint; }
static id sl_maxTint(id self, SEL s) { (void)s; return SL(self)->maxTint; }
static void sl_setThumbImage(id self, SEL s, id v, unsigned long st) { (void)s; SETID(SL(self)->thumbImg[st & 7]); }
static void sl_setMinImage(id self, SEL s, id v, unsigned long st) { (void)s; SETID(SL(self)->minImg[st & 7]); }
static void sl_setMaxImage(id self, SEL s, id v, unsigned long st) { (void)s; SETID(SL(self)->maxImg[st & 7]); }
static id sl_thumbImage(id self, SEL s, unsigned long st) { (void)s; return SL(self)->thumbImg[st & 7] ? SL(self)->thumbImg[st & 7] : SL(self)->thumbImg[0]; }
static id sl_currentThumb(id self, SEL s) { return sl_thumbImage(self, s, cur_state(self)); }
static double thumb_w(id self) { id t = sl_currentThumb(self, NULL); return t ? rt_uiimage_size(t).width : 28; }
static CGRect sl_trackRect(id self, SEL s, CGRect b) { (void)s; (void)self; return (CGRect){ { b.origin.x, b.origin.y + b.size.height / 2 - 1 }, { b.size.width, 2 } }; }
static CGRect sl_thumbRect(id self, SEL s, CGRect b, CGRect track, float value) {
    (void)s; (void)track; struct uislider *x = SL(self); double tw = thumb_w(self);
    double f = x->max > x->min ? (value - x->min) / (x->max - x->min) : 0;
    id t = sl_currentThumb(self, NULL); double th = t ? rt_uiimage_size(t).height : 28;
    return (CGRect){ { b.origin.x + f * (b.size.width - tw), b.origin.y + (b.size.height - th) / 2 }, { tw, th } };
}
static void slider_track_to(id self, id touch) {
    struct uislider *x = SL(self); CGRect b = rt_view_bounds(self);
    CGPoint p = RT_SEND1(CGPoint, touch, "locationInView:", id, self);
    double tw = thumb_w(self), f = (p.x - b.origin.x - tw / 2) / fmax(1, b.size.width - tw);
    float nv = (float)(x->min + fmin(1, fmax(0, f)) * (x->max - x->min));
    if (nv != x->value) { x->value = nv; rt_ui_mark_dirty(); if (x->continuous) send_events(self, EV_VALUE, NULL); }
}
static rt_bool sl_begin(id self, SEL s, id t, id e) { (void)s; (void)e; slider_track_to(self, t); return 1; }
static rt_bool sl_continue(id self, SEL s, id t, id e) { (void)s; (void)e; slider_track_to(self, t); return 1; }
static void sl_end(id self, SEL s, id t, id e) { (void)s; (void)e; (void)t; if (!SL(self)->continuous) send_events(self, EV_VALUE, NULL); }
static void draw_slider(id view, struct cgctx *c, CGRect b) {
    struct uislider *x = SL(view);
    CGRect thumb = ((CGRect (*)(id, SEL, CGRect, CGRect, float))objc_msgSend)(view, rt_sel("thumbRectForBounds:trackRect:value:"), b, b, x->value);
    double cx = thumb.origin.x + thumb.size.width / 2;
    id minI = x->minImg[0], maxI = x->maxImg[0];
    if (minI || maxI) {
        CGSize ms = minI ? rt_uiimage_size(minI) : rt_uiimage_size(maxI);
        double ty = b.origin.y + (b.size.height - ms.height) / 2;
        if (minI) { iosrt_CGContextSaveGState(c); iosrt_CGContextClipToRect(c, (CGRect){ { b.origin.x, b.origin.y }, { cx - b.origin.x, b.size.height } }); rt_uiimage_draw(minI, c, (CGRect){ { b.origin.x, ty }, { b.size.width, ms.height } }, 1, NULL); iosrt_CGContextRestoreGState(c); }
        if (maxI) { iosrt_CGContextSaveGState(c); iosrt_CGContextClipToRect(c, (CGRect){ { cx, b.origin.y }, { b.origin.x + b.size.width - cx, b.size.height } }); rt_uiimage_draw(maxI, c, (CGRect){ { b.origin.x, ty }, { b.size.width, ms.height } }, 1, NULL); iosrt_CGContextRestoreGState(c); }
    } else {
        double ty = b.origin.y + b.size.height / 2 - 1;
        iosrt_CGContextSetFillColorWithColor(c, x->minTint ? x->minTint : rt_view_tint(view));
        iosrt_CGContextFillRect(c, (CGRect){ { b.origin.x, ty }, { cx - b.origin.x, 2 } });
        iosrt_CGContextSetFillColorWithColor(c, x->maxTint ? x->maxTint : rt_color_make(0.72, 0.72, 0.74, 1));
        iosrt_CGContextFillRect(c, (CGRect){ { cx, ty }, { b.origin.x + b.size.width - cx, 2 } });
    }
    id ti = sl_currentThumb(view, NULL);
    if (ti) rt_uiimage_draw(ti, c, thumb, 1, NULL);
    else {
        iosrt_CGContextSetFillColorWithColor(c, x->thumbTint ? x->thumbTint : rt_color_make(1, 1, 1, 1));
        iosrt_CGContextFillEllipseInRect(c, thumb);
        iosrt_CGContextSetStrokeColorWithColor(c, rt_color_make(0, 0, 0, 0.15)); iosrt_CGContextSetLineWidth(c, 0.5);
        iosrt_CGContextStrokeEllipseInRect(c, thumb);
    }
}

/* ================================================================ UISwitch */
struct uiswitch { struct uicontrol c; long on; id onTint, thumbTint, tint; };
#define SW(x) ((struct uiswitch *)(x))
static id sw_initWithFrame(id self, SEL s, CGRect fr) { (void)s; fr.size = (CGSize){ 51, 31 }; return mksuper_call(self, RT_CLASS(UISwitch), "initWithFrame:", fr); }
static rt_bool sw_on(id self, SEL s) { (void)s; return SW(self)->on != 0; }
static void sw_setOn(id self, SEL s, long v) { (void)s; SW(self)->on = v & 0xff; rt_ui_mark_dirty(); }
static void sw_setOnAnim(id self, SEL s, long v, long a) { (void)a; sw_setOn(self, s, v); }
static void sw_setOnTint(id self, SEL s, id v) { (void)s; SETID(SW(self)->onTint); }
static void sw_setThumbTint(id self, SEL s, id v) { (void)s; SETID(SW(self)->thumbTint); }
static void sw_setTint(id self, SEL s, id v) { (void)s; SETID(SW(self)->tint); }
static id sw_onTint(id self, SEL s) { (void)s; return SW(self)->onTint; }
static void sw_end(id self, SEL s, id t, id e) { (void)s; (void)e; if (inside_expanded(self, t)) { SW(self)->on = !SW(self)->on; rt_ui_mark_dirty(); send_events(self, EV_VALUE, NULL); } }
static void draw_switch(id view, struct cgctx *c, CGRect b) {
    struct uiswitch *x = SW(view);
    CGRect r = { { b.origin.x + (b.size.width - 51) / 2, b.origin.y + (b.size.height - 31) / 2 }, { 51, 31 } };
    id track = x->on ? (x->onTint ? x->onTint : rt_color_make(0.204, 0.78, 0.349, 1)) : rt_color_make(0.9, 0.9, 0.92, 1);
    id p = iosrt_CGPathCreateWithRoundedRect(r, 15.5, 15.5, NULL);
    iosrt_CGContextBeginPath(c); iosrt_CGContextAddPath(c, p); iosrt_CGContextSetFillColorWithColor(c, track); iosrt_CGContextFillPath(c); objc_release(p);
    CGRect th = { { x->on ? r.origin.x + 22 : r.origin.x + 2, r.origin.y + 2 }, { 27, 27 } };
    iosrt_CGContextSetFillColorWithColor(c, x->thumbTint ? x->thumbTint : rt_color_make(1, 1, 1, 1));
    iosrt_CGContextFillEllipseInRect(c, th);
}

/* ===================================================== UIActivityIndicatorView */
struct uiactivity { struct uiview v; long style, animating, hides; id color; };
#define AI(x) ((struct uiactivity *)(x))
static id ai_initWithStyle(id self, SEL s, long st) {
    (void)s; double sz = st == 0 ? 37 : 20;
    self = ((id (*)(id, SEL, CGRect))objc_msgSend)(self, rt_sel("initWithFrame:"), (CGRect){ { 0, 0 }, { sz, sz } });
    AI(self)->style = st; AI(self)->hides = 1; V(self)->interaction = 0;
    RT_SEND1(void, self, "setHidden:", long, 1);
    return self;
}
static id ai_initWithFrame(id self, SEL s, CGRect fr) { (void)s; self = mksuper_call(self, RT_CLASS(UIActivityIndicatorView), "initWithFrame:", fr); AI(self)->hides = 1; AI(self)->style = 2; return self; }
static void ai_start(id self, SEL s) { (void)s; AI(self)->animating = 1; RT_SEND1(void, self, "setHidden:", long, 0); rt_ui_mark_dirty(); }
static void ai_stop(id self, SEL s) { (void)s; AI(self)->animating = 0; if (AI(self)->hides) RT_SEND1(void, self, "setHidden:", long, 1); rt_ui_mark_dirty(); }
static rt_bool ai_isAnimating(id self, SEL s) { (void)s; return AI(self)->animating != 0; }
static rt_bool ai_hides(id self, SEL s) { (void)s; return AI(self)->hides != 0; }
static void ai_setHides(id self, SEL s, long v) { (void)s; AI(self)->hides = v & 0xff; if (!AI(self)->animating) RT_SEND1(void, self, "setHidden:", long, v & 0xff); }
static long ai_style(id self, SEL s) { (void)s; return AI(self)->style; }
static void ai_setStyle(id self, SEL s, long v) { (void)s; AI(self)->style = v; rt_ui_mark_dirty(); }
static id ai_color(id self, SEL s) { (void)s; return AI(self)->color; }
static void ai_setColor(id self, SEL s, id v) { (void)s; SETID(AI(self)->color); }
static void draw_activity(id view, struct cgctx *c, CGRect b) {
    struct uiactivity *x = AI(view);
    if (!x->animating && x->hides) return;
    double cx = b.origin.x + b.size.width / 2, cy = b.origin.y + b.size.height / 2, r = fmin(b.size.width, b.size.height) / 2;
    float col[4]; rt_color_rgba(x->color ? x->color : x->style == 2 ? rt_color_make(0.5, 0.5, 0.5, 1) : rt_color_make(1, 1, 1, 1), col);
    int phase = (int)(rt_now() * 12) % 12;
    iosrt_CGContextSetLineWidth(c, r * 0.18); iosrt_CGContextSetLineCap(c, 1);
    for (int i = 0; i < 12; i++) {
        double a = i * M_PI / 6 - M_PI / 2;
        float alpha = 0.25f + 0.75f * (float)((i - phase + 12) % 12) / 11.f;
        iosrt_CGContextSetRGBStrokeColor(c, col[0], col[1], col[2], col[3] * alpha);
        iosrt_CGContextBeginPath(c);
        iosrt_CGContextMoveToPoint(c, cx + cos(a) * r * 0.5, cy + sin(a) * r * 0.5);
        iosrt_CGContextAddLineToPoint(c, cx + cos(a) * r * 0.9, cy + sin(a) * r * 0.9);
        iosrt_CGContextStrokePath(c);
    }
    if (x->animating) g_continuous = 1;
}

/* ============================================================ UIProgressView */
struct uiprogress { struct uiview v; float progress; long style; id progressTint, trackTint, progressImage, trackImage; };
#define PV(x) ((struct uiprogress *)(x))
static id pv_initWithStyle(id self, SEL s, long st) { (void)s; self = ((id (*)(id, SEL, CGRect))objc_msgSend)(self, rt_sel("initWithFrame:"), (CGRect){ { 0, 0 }, { 150, 2 } }); PV(self)->style = st; return self; }
static float pv_progress(id self, SEL s) { (void)s; return PV(self)->progress; }
static void pv_setProgress(id self, SEL s, float v) { (void)s; PV(self)->progress = fminf(1, fmaxf(0, v)); rt_ui_mark_dirty(); }
static void pv_setProgressAnim(id self, SEL s, float v, long a) { (void)a; pv_setProgress(self, s, v); }
static void pv_setPT(id self, SEL s, id v) { (void)s; SETID(PV(self)->progressTint); }
static void pv_setTT(id self, SEL s, id v) { (void)s; SETID(PV(self)->trackTint); }
static void pv_setPI(id self, SEL s, id v) { (void)s; SETID(PV(self)->progressImage); }
static void pv_setTI(id self, SEL s, id v) { (void)s; SETID(PV(self)->trackImage); }
static void draw_progress(id view, struct cgctx *c, CGRect b) {
    struct uiprogress *x = PV(view);
    if (x->trackImage) rt_uiimage_draw(x->trackImage, c, b, 1, NULL);
    else { iosrt_CGContextSetFillColorWithColor(c, x->trackTint ? x->trackTint : rt_color_make(0.72, 0.72, 0.74, 1)); iosrt_CGContextFillRect(c, b); }
    CGRect p = { b.origin, { b.size.width * x->progress, b.size.height } };
    if (x->progressImage) rt_uiimage_draw(x->progressImage, c, p, 1, NULL);
    else { iosrt_CGContextSetFillColorWithColor(c, x->progressTint ? x->progressTint : rt_view_tint(view)); iosrt_CGContextFillRect(c, p); }
}

/* ============================================================ UIPageControl */
struct uipage { struct uicontrol c; long pages, current, hidesSingle, defersUpdate; id pageTint, currentTint; };
#define PC(x) ((struct uipage *)(x))
static long pc_pages(id self, SEL s) { (void)s; return PC(self)->pages; }
static void pc_setPages(id self, SEL s, long n) { (void)s; PC(self)->pages = n; if (PC(self)->current >= n) PC(self)->current = n > 0 ? n - 1 : 0; rt_ui_mark_dirty(); }
static long pc_current(id self, SEL s) { (void)s; return PC(self)->current; }
static void pc_setCurrent(id self, SEL s, long n) { (void)s; PC(self)->current = n < 0 ? 0 : n >= PC(self)->pages ? PC(self)->pages - 1 : n; if (PC(self)->current < 0) PC(self)->current = 0; rt_ui_mark_dirty(); }
static rt_bool pc_hides(id self, SEL s) { (void)s; return PC(self)->hidesSingle != 0; }
static void pc_setHides(id self, SEL s, long v) { (void)s; PC(self)->hidesSingle = v & 0xff; rt_ui_mark_dirty(); }
static void pc_setPageTint(id self, SEL s, id v) { (void)s; SETID(PC(self)->pageTint); }
static void pc_setCurrentTint(id self, SEL s, id v) { (void)s; SETID(PC(self)->currentTint); }
static CGSize pc_sizeForPages(id self, SEL s, long n) { (void)self; (void)s; return (CGSize){ n > 0 ? n * 16 - 9 + 14 : 0, 37 }; }
static void pc_end(id self, SEL s, id t, id e) {
    (void)s; (void)e; struct uipage *x = PC(self);
    CGPoint p = RT_SEND1(CGPoint, t, "locationInView:", id, self); CGRect b = rt_view_bounds(self);
    long old = x->current;
    if (p.x < b.origin.x + b.size.width / 2) { if (x->current > 0) x->current--; } else if (x->current < x->pages - 1) x->current++;
    if (old != x->current) { rt_ui_mark_dirty(); send_events(self, EV_VALUE, NULL); }
}
static void draw_pagecontrol(id view, struct cgctx *c, CGRect b) {
    struct uipage *x = PC(view);
    if (x->pages <= 0 || (x->pages == 1 && x->hidesSingle)) return;
    double w = x->pages * 16 - 9, x0 = b.origin.x + (b.size.width - w) / 2, cy = b.origin.y + b.size.height / 2;
    for (long i = 0; i < x->pages; i++) {
        id col = i == x->current ? (x->currentTint ? x->currentTint : rt_color_make(1, 1, 1, 1)) : (x->pageTint ? x->pageTint : rt_color_make(1, 1, 1, 0.2));
        iosrt_CGContextSetFillColorWithColor(c, col);
        iosrt_CGContextFillEllipseInRect(c, (CGRect){ { x0 + i * 16, cy - 3.5 }, { 7, 7 } });
    }
}

/* ======================================================= UISegmentedControl */
struct uisegment { struct uicontrol c; id items; long selected, momentary; id widths; };
#define SG(x) ((struct uisegment *)(x))
static id sg_initWithItems(id self, SEL s, id items) {
    (void)s; self = ((id (*)(id, SEL, CGRect))objc_msgSend)(self, rt_sel("initWithFrame:"), (CGRect){ { 0, 0 }, { (items ? rt_array_count(items) : 1) * 60.0, 29 } });
    SG(self)->items = mkarray(); SG(self)->selected = -1;
    for (size_t i = 0; items && i < rt_array_count(items); i++) RT_SEND1(void, SG(self)->items, "addObject:", id, rt_array_at(items, i));
    return self;
}
static id sg_initWithFrame(id self, SEL s, CGRect fr) { (void)s; self = mksuper_call(self, RT_CLASS(UISegmentedControl), "initWithFrame:", fr); if (!SG(self)->items) SG(self)->items = mkarray(); SG(self)->selected = -1; return self; }
static long sg_selected(id self, SEL s) { (void)s; return SG(self)->selected; }
static void sg_setSelected(id self, SEL s, long i) { (void)s; SG(self)->selected = i; rt_ui_mark_dirty(); }
static unsigned long sg_count(id self, SEL s) { (void)s; return SG(self)->items ? rt_array_count(SG(self)->items) : 0; }
static void sg_insert(id self, SEL s, id item, unsigned long idx, long anim) {
    (void)s; (void)anim; if (!SG(self)->items) SG(self)->items = mkarray();
    unsigned long n = rt_array_count(SG(self)->items); if (idx > n) idx = n;
    RT_SEND2(void, SG(self)->items, "insertObject:atIndex:", id, item ? item : rt_str(""), unsigned long, idx); rt_ui_mark_dirty();
}
static void sg_setTitle(id self, SEL s, id t, unsigned long i) { (void)s; if (SG(self)->items && i < rt_array_count(SG(self)->items) && t) RT_SEND2(void, SG(self)->items, "replaceObjectAtIndex:withObject:", unsigned long, i, id, t); rt_ui_mark_dirty(); }
static id sg_title(id self, SEL s, unsigned long i) { (void)s; id it = SG(self)->items && i < rt_array_count(SG(self)->items) ? rt_array_at(SG(self)->items, i) : NULL; return it && !rt_is_kind_of(it, RT_CLASS(UIImage)) ? it : NULL; }
static void sg_removeAll(id self, SEL s) { (void)s; if (SG(self)->items) RT_SEND0(void, SG(self)->items, "removeAllObjects"); SG(self)->selected = -1; rt_ui_mark_dirty(); }
static void sg_remove(id self, SEL s, unsigned long i, long a) { (void)s; (void)a; if (SG(self)->items && i < rt_array_count(SG(self)->items)) RT_SEND1(void, SG(self)->items, "removeObjectAtIndex:", unsigned long, i); rt_ui_mark_dirty(); }
static void sg_setMomentary(id self, SEL s, long v) { (void)s; SG(self)->momentary = v & 0xff; }
static void sg_end(id self, SEL s, id t, id e) {
    (void)s; (void)e; struct uisegment *x = SG(self); size_t n = rt_array_count(x->items);
    if (!n || !inside_expanded(self, t)) return;
    CGPoint p = RT_SEND1(CGPoint, t, "locationInView:", id, self); CGRect b = rt_view_bounds(self);
    long i = (long)((p.x - b.origin.x) / (b.size.width / n)); if (i < 0) i = 0; if (i >= (long)n) i = (long)n - 1;
    if (i != x->selected || x->momentary) { x->selected = x->momentary ? -1 : i; rt_ui_mark_dirty(); send_events(self, EV_VALUE, NULL); if (x->momentary) x->selected = -1; }
}
static void draw_segmented(id view, struct cgctx *c, CGRect b) {
    struct uisegment *x = SG(view); size_t n = x->items ? rt_array_count(x->items) : 0;
    if (!n) return;
    id tint = rt_view_tint(view); float tc[4]; rt_color_rgba(tint, tc);
    double sw = b.size.width / n;
    for (size_t i = 0; i < n; i++) {
        CGRect r = { { b.origin.x + i * sw, b.origin.y }, { sw, b.size.height } };
        if ((long)i == x->selected) { iosrt_CGContextSetFillColorWithColor(c, tint); iosrt_CGContextFillRect(c, r); }
        id it = rt_array_at(x->items, i);
        float col[4] = { tc[0], tc[1], tc[2], tc[3] };
        if ((long)i == x->selected) { col[0] = col[1] = col[2] = 1; col[3] = 1; }
        if (rt_is_kind_of(it, RT_CLASS(UIImage))) { CGSize is = rt_uiimage_size(it); rt_uiimage_draw(it, c, (CGRect){ { r.origin.x + (sw - is.width) / 2, r.origin.y + (r.size.height - is.height) / 2 }, is }, 1, col); }
        else rt_text_draw(c, rt_str_utf8(it), rt_font_make(13, 0, NULL), col, r, 1, 1, 1);
    }
    iosrt_CGContextSetStrokeColorWithColor(c, tint); iosrt_CGContextSetLineWidth(c, 1);
    iosrt_CGContextStrokeRect(c, (CGRect){ { b.origin.x + 0.5, b.origin.y + 0.5 }, { b.size.width - 1, b.size.height - 1 } });
}

/* ================================================== UITextField / UITextView */
struct uitextfield { struct uicontrol c; id text, placeholder, font, textColor, delegate, attrPlaceholder; long align, border, secure, clearMode, editing; id leftView, rightView, background; long leftMode, rightMode; };
#define TF(x) ((struct uitextfield *)(x))
static id tf_initWithFrame(id self, SEL s, CGRect fr) { (void)s; self = mksuper_call(self, RT_CLASS(UITextField), "initWithFrame:", fr); TF(self)->font = objc_retain(rt_font_make(17, 0, NULL)); return self; }
#define TFID(get, set, field) static id get(id self, SEL s) { (void)s; return TF(self)->field; } static void set(id self, SEL s, id v) { (void)s; SETID(TF(self)->field); }
TFID(tf_placeholder, tf_setPlaceholder, placeholder) TFID(tf_font, tf_setFont, font) TFID(tf_textColor, tf_setTextColor, textColor)
TFID(tf_leftView, tf_setLeftView, leftView) TFID(tf_rightView, tf_setRightView, rightView) TFID(tf_background, tf_setBackground, background)
TFID(tf_attrPlaceholder, tf_setAttrPlaceholder, attrPlaceholder)
static id tf_text(id self, SEL s) { (void)s; return TF(self)->text ? TF(self)->text : rt_str(""); }
static void tf_setText(id self, SEL s, id t) { (void)s; id v = t ? RT_SEND0(id, t, "copy") : NULL; objc_release(TF(self)->text); TF(self)->text = v; rt_ui_mark_dirty(); }
static id tf_delegate(id self, SEL s) { (void)s; return TF(self)->delegate; }
static void tf_setDelegate(id self, SEL s, id d) { (void)s; TF(self)->delegate = d; }
static long tf_align(id self, SEL s) { (void)s; return TF(self)->align; }
static void tf_setAlign(id self, SEL s, long v) { (void)s; TF(self)->align = v; rt_ui_mark_dirty(); }
static long tf_border(id self, SEL s) { (void)s; return TF(self)->border; }
static void tf_setBorder(id self, SEL s, long v) { (void)s; TF(self)->border = v; rt_ui_mark_dirty(); }
static void tf_setSecure(id self, SEL s, long v) { (void)s; TF(self)->secure = v & 0xff; }
static rt_bool tf_editing(id self, SEL s) { (void)s; return TF(self)->editing != 0; }
static rt_bool tf_becomeFirst(id self, SEL s) {
    (void)s; id d = TF(self)->delegate;
    if (d && rt_responds(d, rt_sel("textFieldShouldBeginEditing:")) && !(RT_SEND1(long, d, "textFieldShouldBeginEditing:", id, self) & 0xff)) return 0;
    TF(self)->editing = 1;
    if (d && rt_responds(d, rt_sel("textFieldDidBeginEditing:"))) RT_SEND1(void, d, "textFieldDidBeginEditing:", id, self);
    if (rt_first_time("keyboard")) rt_log("text input: the software keyboard is not implemented yet");
    return 1;
}
static rt_bool tf_resignFirst(id self, SEL s) {
    (void)s; if (!TF(self)->editing) return 1;
    TF(self)->editing = 0; id d = TF(self)->delegate;
    if (d && rt_responds(d, rt_sel("textFieldDidEndEditing:"))) RT_SEND1(void, d, "textFieldDidEndEditing:", id, self);
    return 1;
}
static void draw_textfield(id view, struct cgctx *c, CGRect b) {
    struct uitextfield *x = TF(view);
    if (x->background) rt_uiimage_draw(x->background, c, b, 1, NULL);
    else if (x->border == 3) {
        id p = iosrt_CGPathCreateWithRoundedRect((CGRect){ { b.origin.x + 0.5, b.origin.y + 0.5 }, { b.size.width - 1, b.size.height - 1 } }, 5, 5, NULL);
        iosrt_CGContextBeginPath(c); iosrt_CGContextAddPath(c, p); iosrt_CGContextSetFillColorWithColor(c, rt_color_make(1, 1, 1, 1)); iosrt_CGContextFillPath(c);
        iosrt_CGContextBeginPath(c); iosrt_CGContextAddPath(c, p); iosrt_CGContextSetStrokeColorWithColor(c, rt_color_make(0.8, 0.8, 0.8, 1)); iosrt_CGContextSetLineWidth(c, 1); iosrt_CGContextStrokePath(c);
        objc_release(p);
    } else if (x->border == 1 || x->border == 2) { iosrt_CGContextSetStrokeColorWithColor(c, rt_color_make(0.5, 0.5, 0.5, 1)); iosrt_CGContextSetLineWidth(c, 1); iosrt_CGContextStrokeRect(c, (CGRect){ { b.origin.x + 0.5, b.origin.y + 0.5 }, { b.size.width - 1, b.size.height - 1 } }); }
    CGRect tr = { { b.origin.x + 7, b.origin.y }, { b.size.width - 14, b.size.height } };
    const char *t = x->text ? rt_str_utf8(x->text) : "";
    if (*t) {
        float col[4]; rt_color_rgba(x->textColor ? x->textColor : rt_color_make(0, 0, 0, 1), col);
        if (x->secure) { size_t n = strlen(t); char *dots = malloc(n * 3 + 1); for (size_t i = 0; i < n; i++) memcpy(dots + i * 3, "\xe2\x80\xa2", 3); dots[n * 3] = 0; rt_text_draw(c, dots, x->font, col, tr, x->align, 1, 1); free(dots); }
        else rt_text_draw(c, t, x->font, col, tr, x->align, 1, 1);
    } else if (x->placeholder) {
        float col[4] = { 0.7f, 0.7f, 0.72f, 1 };
        rt_text_draw(c, rt_str_utf8(x->placeholder), x->font, col, tr, x->align, 1, 1);
    }
}
struct uitextview { struct uiscroll s; id text, font, textColor, delegate; long align, editable, selectable; UIEdgeInsets tci; };
#define TV(x) ((struct uitextview *)(x))
static id tv_initWithFrame(id self, SEL s, CGRect fr) { (void)s; self = mksuper_call(self, RT_CLASS(UITextView), "initWithFrame:", fr); TV(self)->font = objc_retain(rt_font_make(12, 0, NULL)); TV(self)->editable = 1; TV(self)->tci = (UIEdgeInsets){ 8, 0, 8, 0 }; RT_SEND1(void, self, "setBackgroundColor:", id, rt_color_make(1, 1, 1, 1)); return self; }
static id tv_text(id self, SEL s) { (void)s; return TV(self)->text ? TV(self)->text : rt_str(""); }
static void tv_setText(id self, SEL s, id t) { (void)s; id v = t ? RT_SEND0(id, t, "copy") : NULL; objc_release(TV(self)->text); TV(self)->text = v; rt_ui_mark_dirty(); }
static void tv_setAttrText(id self, SEL s, id t) { tv_setText(self, s, t ? RT_SEND0(id, t, "string") : NULL); }
static id tv_attrText(id self, SEL s) { (void)s; return TV(self)->text ? objc_autorelease(RT_SEND1(id, RT_SEND0(id, (id)rt_class_named("NSAttributedString"), "alloc"), "initWithString:", id, TV(self)->text)) : NULL; }
static id tv_font(id self, SEL s) { (void)s; return TV(self)->font; }
static void tv_setFont(id self, SEL s, id v) { (void)s; SETID(TV(self)->font); }
static id tv_textColor(id self, SEL s) { (void)s; return TV(self)->textColor; }
static void tv_setTextColor(id self, SEL s, id v) { (void)s; SETID(TV(self)->textColor); }
static void tv_setDelegate(id self, SEL s, id d) { (void)s; TV(self)->delegate = d; SV(self)->delegate = d; }
static id tv_delegate(id self, SEL s) { (void)s; return TV(self)->delegate; }
static void tv_setAlign(id self, SEL s, long v) { (void)s; TV(self)->align = v; rt_ui_mark_dirty(); }
static void tv_setEditable(id self, SEL s, long v) { (void)s; TV(self)->editable = v & 0xff; }
static rt_bool tv_editable(id self, SEL s) { (void)s; return TV(self)->editable != 0; }
static void tv_setTCI(id self, SEL s, UIEdgeInsets e) { (void)s; TV(self)->tci = e; rt_ui_mark_dirty(); }
static void draw_textview(id view, struct cgctx *c, CGRect b) {
    struct uitextview *x = TV(view);
    if (!x->text) return;
    float col[4]; rt_color_rgba(x->textColor ? x->textColor : rt_color_make(0, 0, 0, 1), col);
    CGRect bb = rt_view_bounds(view);
    CGRect r = { { x->tci.left + 5, x->tci.top }, { bb.size.width - x->tci.left - x->tci.right - 10, 1e6 } };
    (void)b;
    CGSize sz = rt_text_measure(rt_str_utf8(x->text), x->font, r.size.width, 0);
    r.size.height = sz.height;
    SV(view)->contentSize = (CGSize){ bb.size.width, sz.height + x->tci.top + x->tci.bottom };
    rt_text_draw(c, rt_str_utf8(x->text), x->font, col, r, x->align, 0, 0);
}

/* ============================================================== UIStepper */
struct uistepper { struct uicontrol c; double value, min, max, step; long wraps, continuous; };
#define STP(x) ((struct uistepper *)(x))
static id stp_initWithFrame(id self, SEL s, CGRect fr) { (void)s; fr.size = (CGSize){ 94, 29 }; self = mksuper_call(self, RT_CLASS(UIStepper), "initWithFrame:", fr); STP(self)->max = 100; STP(self)->step = 1; return self; }
static double stp_value(id self, SEL s) { (void)s; return STP(self)->value; }
static void stp_setValue(id self, SEL s, double v) { (void)s; STP(self)->value = fmin(STP(self)->max, fmax(STP(self)->min, v)); rt_ui_mark_dirty(); }
static void stp_setMin(id self, SEL s, double v) { (void)s; STP(self)->min = v; }
static void stp_setMax(id self, SEL s, double v) { (void)s; STP(self)->max = v; }
static void stp_setStep(id self, SEL s, double v) { (void)s; STP(self)->step = v; }
static void stp_end(id self, SEL s, id t, id e) {
    (void)s; (void)e; CGPoint p = RT_SEND1(CGPoint, t, "locationInView:", id, self); CGRect b = rt_view_bounds(self);
    double nv = STP(self)->value + (p.x < b.size.width / 2 ? -STP(self)->step : STP(self)->step);
    stp_setValue(self, NULL, nv); send_events(self, EV_VALUE, NULL);
}
static void draw_stepper(id view, struct cgctx *c, CGRect b) {
    float tc[4]; rt_color_rgba(rt_view_tint(view), tc);
    iosrt_CGContextSetStrokeColorWithColor(c, rt_view_tint(view)); iosrt_CGContextSetLineWidth(c, 1);
    iosrt_CGContextStrokeRect(c, (CGRect){ { b.origin.x + 0.5, b.origin.y + 0.5 }, { b.size.width - 1, b.size.height - 1 } });
    rt_text_draw(c, "\xe2\x88\x92", rt_font_make(20, 0, NULL), tc, (CGRect){ b.origin, { b.size.width / 2, b.size.height } }, 1, 1, 1);
    rt_text_draw(c, "+", rt_font_make(20, 0, NULL), tc, (CGRect){ { b.origin.x + b.size.width / 2, b.origin.y }, { b.size.width / 2, b.size.height } }, 1, 1, 1);
}

/* ============================================================ draw dispatch */
void rt_controls2_draw(id view, struct cgctx *c, CGRect b);
void rt_control_draw(id view, struct cgctx *c, CGRect b) {
    if (rt_is_kind_of(view, RT_CLASS(UIImageView))) draw_imageview(view, c, b);
    else if (rt_is_kind_of(view, RT_CLASS(UIButton))) draw_button(view, c, b);
    else if (rt_is_kind_of(view, RT_CLASS(UISlider))) draw_slider(view, c, b);
    else if (rt_is_kind_of(view, RT_CLASS(UISwitch))) draw_switch(view, c, b);
    else if (rt_is_kind_of(view, RT_CLASS(UIActivityIndicatorView))) draw_activity(view, c, b);
    else if (rt_is_kind_of(view, RT_CLASS(UIProgressView))) draw_progress(view, c, b);
    else if (rt_is_kind_of(view, RT_CLASS(UIPageControl))) draw_pagecontrol(view, c, b);
    else if (rt_is_kind_of(view, RT_CLASS(UISegmentedControl))) draw_segmented(view, c, b);
    else if (rt_is_kind_of(view, RT_CLASS(UITextField))) draw_textfield(view, c, b);
    else if (rt_is_kind_of(view, RT_CLASS(UITextView))) draw_textview(view, c, b);
    else if (rt_is_kind_of(view, RT_CLASS(UIStepper))) draw_stepper(view, c, b);
    else rt_controls2_draw(view, c, b);
}

/* button content objects decoded from NIBs */
static id bc_title(id self, SEL s) { (void)s; return ((struct bcobj *)self)->c.title; }
void rt_button_apply_content(id button, unsigned long state, id content) {
    if (!content || !rt_is_kind_of(content, RT_CLASS(UIButtonContent))) return;
    struct bcontent *src = &((struct bcobj *)content)->c, *d = &BT(button)->st[state & 7];
#define CP(f) do { objc_retain(src->f); objc_release(d->f); d->f = src->f; } while (0)
    CP(title); CP(attrTitle); CP(titleColor); CP(shadowColor); CP(image); CP(bgImage);
#undef CP
}
id rt_button_content_new(id title, id attrTitle, id titleColor, id shadowColor, id image, id bgImage) {
    id o = rt_alloc_instance(RT_CLASS(UIButtonContent), 0); struct bcontent *c = &((struct bcobj *)o)->c;
    c->title = objc_retain(title); c->attrTitle = objc_retain(attrTitle); c->titleColor = objc_retain(titleColor);
    c->shadowColor = objc_retain(shadowColor); c->image = objc_retain(image); c->bgImage = objc_retain(bgImage);
    return o;
}

void rt_controls2_init(void);
void rt_controls_init(void) {
    Class o = RT_CLASS(NSObject), v = RT_CLASS(UIView);
    NEWCLASS(UILabel, v, sizeof(struct uilabel) - sizeof(struct uiview));
    Class l = RT_CLASS(UILabel);
    IM(l, "initWithFrame:", l_initWithFrame); IM(l, "dealloc", l_dealloc); IM(l, "text", l_text); IM(l, "setText:", l_setText);
    IM(l, "attributedText", l_attributedText); IM(l, "setAttributedText:", l_setAttributedText);
    IM(l, "textColor", l_textColor); IM(l, "setTextColor:", l_setTextColor); IM(l, "highlightedTextColor", l_hlColor); IM(l, "setHighlightedTextColor:", l_setHlColor);
    IM(l, "font", l_font); IM(l, "setFont:", l_setFont); IM(l, "shadowColor", l_shadowColor); IM(l, "setShadowColor:", l_setShadowColor);
    IM(l, "shadowOffset", l_shadowOffset); IM(l, "setShadowOffset:", l_setShadowOffset);
    IM(l, "textAlignment", l_align); IM(l, "setTextAlignment:", l_setAlign); IM(l, "numberOfLines", l_lines); IM(l, "setNumberOfLines:", l_setLines);
    IM(l, "lineBreakMode", l_lineBreak); IM(l, "setLineBreakMode:", l_setLineBreak); IM(l, "isHighlighted", l_highlighted); IM(l, "setHighlighted:", l_setHighlighted);
    IM(l, "isEnabled", l_enabled); IM(l, "setEnabled:", l_setEnabled); IM(l, "adjustsFontSizeToFitWidth", l_adjusts); IM(l, "setAdjustsFontSizeToFitWidth:", l_setAdjusts);
    IM(l, "minimumScaleFactor", l_minScale); IM(l, "setMinimumScaleFactor:", l_setMinScale); IM(l, "setMinimumFontSize:", l_setMinFontSize);
    IM(l, "preferredMaxLayoutWidth", l_pmw); IM(l, "setPreferredMaxLayoutWidth:", l_setPmw);
    IM(l, "sizeThatFits:", l_sizeThatFits); IM(l, "intrinsicContentSize", l_intrinsic); IM(l, "textRectForBounds:limitedToNumberOfLines:", l_textRect);
    IM(l, "drawTextInRect:", l_drawTextInRect); IM(l, "drawRect:", l_drawRect);
    IM(l, "setBaselineAdjustment:", nopL); IM(l, "setAdjustsLetterSpacingToFitWidth:", nopL); IM(l, "setAllowsDefaultTighteningForTruncation:", nopL);

    NEWCLASS(UIImageView, v, sizeof(struct uiimageview) - sizeof(struct uiview));
    Class iv = RT_CLASS(UIImageView);
    IM(iv, "initWithImage:", iv_initWithImage); IM(iv, "initWithImage:highlightedImage:", iv_initWithImageHL); IM(iv, "initWithFrame:", iv_initWithFrame);
    IM(iv, "dealloc", iv_dealloc); IM(iv, "image", iv_image); IM(iv, "setImage:", iv_setImage); IM(iv, "highlightedImage", iv_hlImage);
    IM(iv, "setHighlightedImage:", iv_setHlImage); IM(iv, "isHighlighted", iv_highlighted); IM(iv, "setHighlighted:", iv_setHighlighted);
    IM(iv, "animationImages", iv_animImages); IM(iv, "setAnimationImages:", iv_setAnimImages); IM(iv, "animationDuration", iv_animDuration);
    IM(iv, "setAnimationDuration:", iv_setAnimDuration); IM(iv, "setAnimationRepeatCount:", iv_setAnimRepeat);
    IM(iv, "startAnimating", iv_start); IM(iv, "stopAnimating", iv_stop); IM(iv, "isAnimating", iv_isAnimating); IM(iv, "sizeThatFits:", iv_sizeThatFits);
    IM(iv, "intrinsicContentSize", iv_sizeThatFits);

    NEWCLASS(UIControl, v, sizeof(struct uicontrol) - sizeof(struct uiview));
    Class ct = RT_CLASS(UIControl);
    IM(ct, "initWithFrame:", ctl_initWithFrame); IM(ct, "dealloc", ctl_dealloc);
    IM(ct, "addTarget:action:forControlEvents:", ctl_addTarget); IM(ct, "removeTarget:action:forControlEvents:", ctl_removeTarget);
    IM(ct, "allTargets", ctl_allTargets); IM(ct, "actionsForTarget:forControlEvent:", ctl_actionsForTarget);
    IM(ct, "sendActionsForControlEvents:", ctl_sendActions); IM(ct, "sendAction:to:forEvent:", ctl_sendAction);
    IM(ct, "isEnabled", ctl_enabled); IM(ct, "setEnabled:", ctl_setEnabled); IM(ct, "isSelected", ctl_selected); IM(ct, "setSelected:", ctl_setSelected);
    IM(ct, "isHighlighted", ctl_highlighted); IM(ct, "setHighlighted:", ctl_setHighlighted); IM(ct, "isTracking", ctl_tracking);
    IM(ct, "isTouchInside", ctl_touchInside); IM(ct, "state", ctl_state);
    IM(ct, "contentHorizontalAlignment", ctl_hAlign); IM(ct, "setContentHorizontalAlignment:", ctl_setHAlign);
    IM(ct, "contentVerticalAlignment", ctl_vAlign); IM(ct, "setContentVerticalAlignment:", ctl_setVAlign);
    IM(ct, "beginTrackingWithTouch:withEvent:", ctl_beginTracking); IM(ct, "continueTrackingWithTouch:withEvent:", ctl_continueTracking);
    IM(ct, "endTrackingWithTouch:withEvent:", ctl_endTracking); IM(ct, "cancelTrackingWithEvent:", ctl_cancelTracking);
    IM(ct, "touchesBegan:withEvent:", ctl_touchesBegan); IM(ct, "touchesMoved:withEvent:", ctl_touchesMoved);
    IM(ct, "touchesEnded:withEvent:", ctl_touchesEnded); IM(ct, "touchesCancelled:withEvent:", ctl_touchesCancelled);

    NEWCLASS(UIButtonContent, o, sizeof(struct bcobj) - sizeof(void *));
    IM(RT_CLASS(UIButtonContent), "dealloc", bc_dealloc); IM(RT_CLASS(UIButtonContent), "title", bc_title);
    NEWCLASS(UIButton, ct, sizeof(struct uibutton) - sizeof(struct uicontrol));
    Class bt = RT_CLASS(UIButton);
    CM(bt, "buttonWithType:", b_buttonWithType); IM(bt, "initWithFrame:", b_initWithFrame); IM(bt, "dealloc", b_dealloc); IM(bt, "buttonType", b_type);
    IM(bt, "setTitle:forState:", b_setTitle); IM(bt, "titleForState:", b_titleForState); IM(bt, "setAttributedTitle:forState:", b_setAttrTitle);
    IM(bt, "attributedTitleForState:", b_attrTitleForState); IM(bt, "setTitleColor:forState:", b_setTitleColor); IM(bt, "titleColorForState:", b_titleColorForState);
    IM(bt, "setTitleShadowColor:forState:", b_setShadowColor); IM(bt, "titleShadowColorForState:", b_shadowColorForState);
    IM(bt, "setImage:forState:", b_setImage); IM(bt, "imageForState:", b_imageForState);
    IM(bt, "setBackgroundImage:forState:", b_setBgImage); IM(bt, "backgroundImageForState:", b_bgImageForState);
    IM(bt, "currentTitle", b_currentTitle); IM(bt, "currentAttributedTitle", b_currentAttrTitle); IM(bt, "currentImage", b_currentImage);
    IM(bt, "currentBackgroundImage", b_currentBgImage); IM(bt, "currentTitleColor", b_currentTitleColor);
    IM(bt, "titleLabel", b_titleLabel); IM(bt, "imageView", b_imageView); IM(bt, "setFont:", b_setFont); IM(bt, "font", b_font);
    IM(bt, "contentEdgeInsets", b_contentInsets); IM(bt, "setContentEdgeInsets:", b_setContentInsets);
    IM(bt, "titleEdgeInsets", b_titleInsets); IM(bt, "setTitleEdgeInsets:", b_setTitleInsets);
    IM(bt, "imageEdgeInsets", b_imageInsets); IM(bt, "setImageEdgeInsets:", b_setImageInsets);
    IM(bt, "adjustsImageWhenHighlighted", b_adjustsHL); IM(bt, "setAdjustsImageWhenHighlighted:", b_setAdjustsHL);
    IM(bt, "setAdjustsImageWhenDisabled:", b_setAdjustsDisabled); IM(bt, "setShowsTouchWhenHighlighted:", b_setShowsTouch);
    IM(bt, "setReversesTitleShadowWhenHighlighted:", nopL); IM(bt, "sizeThatFits:", b_sizeThatFits); IM(bt, "intrinsicContentSize", b_intrinsic);
    IM(bt, "setHighlighted:", b_setHighlighted);

    NEWCLASS(UIScrollView, v, sizeof(struct uiscroll) - sizeof(struct uiview));
    Class sv = RT_CLASS(UIScrollView);
    IM(sv, "initWithFrame:", sv_initWithFrame); IM(sv, "dealloc", sv_dealloc);
    IM(sv, "contentOffset", sv_offset); IM(sv, "setContentOffset:", sv_setOffset); IM(sv, "setContentOffset:animated:", sv_setOffsetAnim);
    IM(sv, "contentSize", sv_contentSize); IM(sv, "setContentSize:", sv_setContentSize); IM(sv, "contentInset", sv_inset); IM(sv, "setContentInset:", sv_setInset);
    IM(sv, "scrollIndicatorInsets", sv_inset); IM(sv, "setScrollIndicatorInsets:", sv_setInset);
    IM(sv, "delegate", sv_delegate); IM(sv, "setDelegate:", sv_setDelegate);
    IM(sv, "isPagingEnabled", sv_paging); IM(sv, "setPagingEnabled:", sv_setPaging); IM(sv, "isScrollEnabled", sv_scrollEnabled); IM(sv, "setScrollEnabled:", sv_setScrollEnabled);
    IM(sv, "bounces", sv_bounces); IM(sv, "setBounces:", sv_setBounces); IM(sv, "alwaysBounceVertical", sv_alwaysV); IM(sv, "setAlwaysBounceVertical:", sv_setAlwaysV);
    IM(sv, "alwaysBounceHorizontal", sv_alwaysH); IM(sv, "setAlwaysBounceHorizontal:", sv_setAlwaysH);
    IM(sv, "showsHorizontalScrollIndicator", sv_showsH); IM(sv, "setShowsHorizontalScrollIndicator:", sv_setShowsH);
    IM(sv, "showsVerticalScrollIndicator", sv_showsV); IM(sv, "setShowsVerticalScrollIndicator:", sv_setShowsV);
    IM(sv, "delaysContentTouches", sv_delays); IM(sv, "setDelaysContentTouches:", sv_setDelays); IM(sv, "canCancelContentTouches", sv_cancels);
    IM(sv, "setCanCancelContentTouches:", sv_setCancels); IM(sv, "isDirectionalLockEnabled", sv_lock); IM(sv, "setDirectionalLockEnabled:", sv_setLock);
    IM(sv, "scrollsToTop", sv_toTop); IM(sv, "setScrollsToTop:", sv_setToTop);
    IM(sv, "isDragging", sv_dragging); IM(sv, "isDecelerating", sv_decelerating); IM(sv, "isTracking", sv_tracking);
    IM(sv, "zoomScale", sv_zoom); IM(sv, "setZoomScale:", sv_setZoom); IM(sv, "setZoomScale:animated:", sv_setZoomAnim);
    IM(sv, "minimumZoomScale", sv_minZoom); IM(sv, "setMinimumZoomScale:", sv_setMinZoom); IM(sv, "maximumZoomScale", sv_maxZoom); IM(sv, "setMaximumZoomScale:", sv_setMaxZoom);
    IM(sv, "panGestureRecognizer", sv_pan); IM(sv, "flashScrollIndicators", sv_flash); IM(sv, "scrollRectToVisible:animated:", sv_scrollRectToVisible);
    IM(sv, "iosrt_handlePan:", sv_handlePan); IM(sv, "touchesShouldCancelInContentView:", sv_touchesShouldCancel);
    IM(sv, "touchesShouldBegin:withEvent:inContentView:", sv_touchesShouldBegin);
    IM(sv, "setDecelerationRate:", nopL); IM(sv, "setIndicatorStyle:", nopL); IM(sv, "setBouncesZoom:", nopL); IM(sv, "setKeyboardDismissMode:", nopL);
    IM(sv, "setContentInsetAdjustmentBehavior:", nopL); IM(sv, "pinchGestureRecognizer", nil0);

    NEWCLASS(UISlider, ct, sizeof(struct uislider) - sizeof(struct uicontrol));
    Class sl = RT_CLASS(UISlider);
    IM(sl, "initWithFrame:", sl_initWithFrame); IM(sl, "value", sl_value); IM(sl, "setValue:", sl_setValue); IM(sl, "setValue:animated:", sl_setValueAnim);
    IM(sl, "minimumValue", sl_min); IM(sl, "setMinimumValue:", sl_setMin); IM(sl, "maximumValue", sl_max); IM(sl, "setMaximumValue:", sl_setMax);
    IM(sl, "isContinuous", sl_continuous); IM(sl, "setContinuous:", sl_setContinuous);
    IM(sl, "minimumTrackTintColor", sl_minTint); IM(sl, "setMinimumTrackTintColor:", sl_setMinTint);
    IM(sl, "maximumTrackTintColor", sl_maxTint); IM(sl, "setMaximumTrackTintColor:", sl_setMaxTint); IM(sl, "setThumbTintColor:", sl_setThumbTint);
    IM(sl, "setThumbImage:forState:", sl_setThumbImage); IM(sl, "thumbImageForState:", sl_thumbImage); IM(sl, "currentThumbImage", sl_currentThumb);
    IM(sl, "setMinimumTrackImage:forState:", sl_setMinImage); IM(sl, "setMaximumTrackImage:forState:", sl_setMaxImage);
    IM(sl, "trackRectForBounds:", sl_trackRect); IM(sl, "thumbRectForBounds:trackRect:value:", sl_thumbRect);
    IM(sl, "beginTrackingWithTouch:withEvent:", sl_begin); IM(sl, "continueTrackingWithTouch:withEvent:", sl_continue); IM(sl, "endTrackingWithTouch:withEvent:", sl_end);
    IM(sl, "setMinimumValueImage:", nop1); IM(sl, "setMaximumValueImage:", nop1);

    NEWCLASS(UISwitch, ct, sizeof(struct uiswitch) - sizeof(struct uicontrol));
    Class sw = RT_CLASS(UISwitch);
    IM(sw, "initWithFrame:", sw_initWithFrame); IM(sw, "isOn", sw_on); IM(sw, "setOn:", sw_setOn); IM(sw, "setOn:animated:", sw_setOnAnim);
    IM(sw, "onTintColor", sw_onTint); IM(sw, "setOnTintColor:", sw_setOnTint); IM(sw, "setThumbTintColor:", sw_setThumbTint); IM(sw, "setTintColor:", sw_setTint);
    IM(sw, "endTrackingWithTouch:withEvent:", sw_end); IM(sw, "setOnImage:", nop1); IM(sw, "setOffImage:", nop1);

    NEWCLASS(UIActivityIndicatorView, v, sizeof(struct uiactivity) - sizeof(struct uiview));
    Class ai = RT_CLASS(UIActivityIndicatorView);
    IM(ai, "initWithActivityIndicatorStyle:", ai_initWithStyle); IM(ai, "initWithFrame:", ai_initWithFrame);
    IM(ai, "startAnimating", ai_start); IM(ai, "stopAnimating", ai_stop); IM(ai, "isAnimating", ai_isAnimating);
    IM(ai, "hidesWhenStopped", ai_hides); IM(ai, "setHidesWhenStopped:", ai_setHides);
    IM(ai, "activityIndicatorViewStyle", ai_style); IM(ai, "setActivityIndicatorViewStyle:", ai_setStyle); IM(ai, "color", ai_color); IM(ai, "setColor:", ai_setColor);

    NEWCLASS(UIProgressView, v, sizeof(struct uiprogress) - sizeof(struct uiview));
    Class pv = RT_CLASS(UIProgressView);
    IM(pv, "initWithProgressViewStyle:", pv_initWithStyle); IM(pv, "progress", pv_progress); IM(pv, "setProgress:", pv_setProgress);
    IM(pv, "setProgress:animated:", pv_setProgressAnim); IM(pv, "setProgressTintColor:", pv_setPT); IM(pv, "setTrackTintColor:", pv_setTT);
    IM(pv, "setProgressImage:", pv_setPI); IM(pv, "setTrackImage:", pv_setTI); IM(pv, "setProgressViewStyle:", nopL);

    NEWCLASS(UIPageControl, ct, sizeof(struct uipage) - sizeof(struct uicontrol));
    Class pc = RT_CLASS(UIPageControl);
    IM(pc, "numberOfPages", pc_pages); IM(pc, "setNumberOfPages:", pc_setPages); IM(pc, "currentPage", pc_current); IM(pc, "setCurrentPage:", pc_setCurrent);
    IM(pc, "hidesForSinglePage", pc_hides); IM(pc, "setHidesForSinglePage:", pc_setHides);
    IM(pc, "setPageIndicatorTintColor:", pc_setPageTint); IM(pc, "setCurrentPageIndicatorTintColor:", pc_setCurrentTint);
    IM(pc, "sizeForNumberOfPages:", pc_sizeForPages); IM(pc, "endTrackingWithTouch:withEvent:", pc_end);
    IM(pc, "setDefersCurrentPageDisplay:", nopL); IM(pc, "updateCurrentPageDisplay", nop);

    NEWCLASS(UISegmentedControl, ct, sizeof(struct uisegment) - sizeof(struct uicontrol));
    Class sg = RT_CLASS(UISegmentedControl);
    IM(sg, "initWithItems:", sg_initWithItems); IM(sg, "initWithFrame:", sg_initWithFrame);
    IM(sg, "selectedSegmentIndex", sg_selected); IM(sg, "setSelectedSegmentIndex:", sg_setSelected); IM(sg, "numberOfSegments", sg_count);
    IM(sg, "insertSegmentWithTitle:atIndex:animated:", sg_insert); IM(sg, "insertSegmentWithImage:atIndex:animated:", sg_insert);
    IM(sg, "setTitle:forSegmentAtIndex:", sg_setTitle); IM(sg, "titleForSegmentAtIndex:", sg_title); IM(sg, "setImage:forSegmentAtIndex:", sg_setTitle);
    IM(sg, "removeAllSegments", sg_removeAll); IM(sg, "removeSegmentAtIndex:animated:", sg_remove); IM(sg, "setMomentary:", sg_setMomentary);
    IM(sg, "endTrackingWithTouch:withEvent:", sg_end); IM(sg, "setTitleTextAttributes:forState:", nop2);
    IM(sg, "setWidth:forSegmentAtIndex:", nop2); IM(sg, "setEnabled:forSegmentAtIndex:", nop2);

    NEWCLASS(UITextField, ct, sizeof(struct uitextfield) - sizeof(struct uicontrol));
    Class tf = RT_CLASS(UITextField);
    IM(tf, "initWithFrame:", tf_initWithFrame); IM(tf, "text", tf_text); IM(tf, "setText:", tf_setText);
    IM(tf, "placeholder", tf_placeholder); IM(tf, "setPlaceholder:", tf_setPlaceholder); IM(tf, "attributedPlaceholder", tf_attrPlaceholder);
    IM(tf, "setAttributedPlaceholder:", tf_setAttrPlaceholder); IM(tf, "font", tf_font); IM(tf, "setFont:", tf_setFont);
    IM(tf, "textColor", tf_textColor); IM(tf, "setTextColor:", tf_setTextColor); IM(tf, "delegate", tf_delegate); IM(tf, "setDelegate:", tf_setDelegate);
    IM(tf, "textAlignment", tf_align); IM(tf, "setTextAlignment:", tf_setAlign); IM(tf, "borderStyle", tf_border); IM(tf, "setBorderStyle:", tf_setBorder);
    IM(tf, "setSecureTextEntry:", tf_setSecure); IM(tf, "isEditing", tf_editing); IM(tf, "becomeFirstResponder", tf_becomeFirst);
    IM(tf, "resignFirstResponder", tf_resignFirst); IM(tf, "canBecomeFirstResponder", yes0);
    IM(tf, "leftView", tf_leftView); IM(tf, "setLeftView:", tf_setLeftView); IM(tf, "rightView", tf_rightView); IM(tf, "setRightView:", tf_setRightView);
    IM(tf, "background", tf_background); IM(tf, "setBackground:", tf_setBackground);
    IM(tf, "setLeftViewMode:", nopL); IM(tf, "setRightViewMode:", nopL); IM(tf, "setClearButtonMode:", nopL); IM(tf, "setKeyboardType:", nopL);
    IM(tf, "setReturnKeyType:", nopL); IM(tf, "setAutocorrectionType:", nopL); IM(tf, "setAutocapitalizationType:", nopL); IM(tf, "setKeyboardAppearance:", nopL);
    IM(tf, "setSpellCheckingType:", nopL); IM(tf, "setEnablesReturnKeyAutomatically:", nopL); IM(tf, "setClearsOnBeginEditing:", nopL);
    IM(tf, "setAdjustsFontSizeToFitWidth:", nopL); IM(tf, "setMinimumFontSize:", nopL); IM(tf, "setInputAccessoryView:", nop1); IM(tf, "setInputView:", nop1);
    IM(tf, "setDefaultTextAttributes:", nop1); IM(tf, "setTypingAttributes:", nop1); IM(tf, "setTextContentType:", nop1);

    NEWCLASS(UITextView, sv, sizeof(struct uitextview) - sizeof(struct uiscroll));
    Class tv = RT_CLASS(UITextView);
    IM(tv, "initWithFrame:", tv_initWithFrame); IM(tv, "text", tv_text); IM(tv, "setText:", tv_setText);
    IM(tv, "attributedText", tv_attrText); IM(tv, "setAttributedText:", tv_setAttrText);
    IM(tv, "font", tv_font); IM(tv, "setFont:", tv_setFont); IM(tv, "textColor", tv_textColor); IM(tv, "setTextColor:", tv_setTextColor);
    IM(tv, "delegate", tv_delegate); IM(tv, "setDelegate:", tv_setDelegate); IM(tv, "setTextAlignment:", tv_setAlign);
    IM(tv, "isEditable", tv_editable); IM(tv, "setEditable:", tv_setEditable); IM(tv, "setSelectable:", nopL); IM(tv, "setTextContainerInset:", tv_setTCI);
    IM(tv, "setDataDetectorTypes:", nopL); IM(tv, "setLinkTextAttributes:", nop1); IM(tv, "setKeyboardType:", nopL); IM(tv, "setReturnKeyType:", nopL);
    IM(tv, "setAutocorrectionType:", nopL); IM(tv, "setInputAccessoryView:", nop1); IM(tv, "canBecomeFirstResponder", no0);
    IM(tv, "scrollRangeToVisible:", nop2); IM(tv, "setSelectedRange:", nop2);

    NEWCLASS(UIStepper, ct, sizeof(struct uistepper) - sizeof(struct uicontrol));
    Class stp = RT_CLASS(UIStepper);
    IM(stp, "initWithFrame:", stp_initWithFrame); IM(stp, "value", stp_value); IM(stp, "setValue:", stp_setValue);
    IM(stp, "setMinimumValue:", stp_setMin); IM(stp, "setMaximumValue:", stp_setMax); IM(stp, "setStepValue:", stp_setStep);
    IM(stp, "endTrackingWithTouch:withEvent:", stp_end); IM(stp, "setWraps:", nopL); IM(stp, "setContinuous:", nopL); IM(stp, "setAutorepeat:", nopL);
    (void)touches_super;
    rt_controls2_init();
}
