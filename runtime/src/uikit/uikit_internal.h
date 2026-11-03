/* Shared declarations for the UIKit / CoreGraphics / QuartzCore HLE. */
#ifndef IOSRT_UIKIT_INTERNAL_H
#define IOSRT_UIKIT_INTERNAL_H
#include "../internal.h"
#include "gfx.h"

#define IM(c, s, f) rt_native_method(c, s, (void *)(f), "")
#define CM(c, s, f) rt_native_cmethod(c, s, (void *)(f), "")
#define NEWCLASS(N, SUPER, SZ) rt_native_class(RT_CLASS(N), &iosrt_meta_##N, #N, SUPER, SZ)

typedef gfx_mat CGAffineTransform;
typedef struct { double m11, m12, m13, m14, m21, m22, m23, m24, m31, m32, m33, m34, m41, m42, m43, m44; } CATransform3D;
typedef struct { double top, left, bottom, right; } UIEdgeInsets;

/* UIColor doubles as CGColorRef. `pattern` is a UIImage for colorWithPatternImage:. */
struct uicolor { Class isa; double r, g, b, a; id pattern; };
struct uifont { Class isa; double size; long bold; id name; struct gfx_font *gf; };
id rt_color_make(double r, double g, double b, double a);   /* autoreleased */
void rt_color_rgba(id color, float out[4]);                  /* nil -> transparent */
id rt_font_make(double size, int bold, id name);             /* autoreleased */
struct gfx_font *rt_font_gfx(id font, int *fake_bold);
double rt_font_size(id font);

/* ---- CoreGraphics (cg.c) */
struct cgctx;
struct cgctx *rt_cg_bitmap_new(int pw, int ph, double scale, int uikit_flip);  /* +1, owns pixels, transparent */
struct gfx_surface *rt_cg_surface(struct cgctx *c);
void rt_cg_set_trace(struct cgctx *c, rt_sb *trace, double points_per_device);
rt_sb *rt_cg_trace(struct cgctx *c, double *scale);
void rt_cg_push(struct cgctx *c);            /* UIGraphicsPushContext */
void rt_cg_pop(void);
struct cgctx *rt_cg_top(void);
gfx_mat rt_cg_ctm(struct cgctx *c);
float rt_cg_alpha(struct cgctx *c);
int rt_cg_blend(struct cgctx *c);
id rt_cgimage_new(struct gfx_image *im);     /* +1 CGImage wrapping a retained image */
struct gfx_image *rt_cgimage_gfx(id cgimage);
id rt_cgimage_from_ctx(struct cgctx *c);     /* +1 snapshot */
/* trace helpers (golden dumps), coordinates in points */
void rt_cg_trace_line(struct cgctx *c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int rt_cg_device_rect_points(struct cgctx *c, CGRect r, CGRect *out);   /* 0 if CTM rotates */

/* C API used across files */
void iosrt_CGContextSaveGState(struct cgctx *c);
void iosrt_CGContextRestoreGState(struct cgctx *c);
void iosrt_CGContextConcatCTM(struct cgctx *c, CGAffineTransform m);
void iosrt_CGContextTranslateCTM(struct cgctx *c, double x, double y);
void iosrt_CGContextScaleCTM(struct cgctx *c, double x, double y);
void iosrt_CGContextSetAlpha(struct cgctx *c, double a);
void iosrt_CGContextClipToRect(struct cgctx *c, CGRect r);
void iosrt_CGContextFillRect(struct cgctx *c, CGRect r);
void iosrt_CGContextSetFillColorWithColor(struct cgctx *c, id col);
void iosrt_CGContextSetStrokeColorWithColor(struct cgctx *c, id col);
void iosrt_CGContextSetLineWidth(struct cgctx *c, double w);
void iosrt_CGContextAddPath(struct cgctx *c, id path);
void iosrt_CGContextBeginPath(struct cgctx *c);
void iosrt_CGContextFillPath(struct cgctx *c);
void iosrt_CGContextEOFillPath(struct cgctx *c);
void iosrt_CGContextStrokePath(struct cgctx *c);
void iosrt_CGContextClip(struct cgctx *c);
void iosrt_CGContextEOClip(struct cgctx *c);
void iosrt_CGContextSetBlendMode(struct cgctx *c, int mode);
void iosrt_CGContextAddRect(struct cgctx *c, CGRect r);
void iosrt_CGContextSetLineCap(struct cgctx *c, int cap);
void iosrt_CGContextSetLineJoin(struct cgctx *c, int join);
void iosrt_CGContextSetLineDash(struct cgctx *c, double phase, const double *lengths, size_t n);
void iosrt_CGContextSetMiterLimit(struct cgctx *c, double m);
void iosrt_CGContextSetRGBFillColor(struct cgctx *c, double r, double g, double b, double a);
void iosrt_CGContextSetRGBStrokeColor(struct cgctx *c, double r, double g, double b, double a);
void iosrt_CGContextFillEllipseInRect(struct cgctx *c, CGRect r);
void iosrt_CGContextStrokeEllipseInRect(struct cgctx *c, CGRect r);
void iosrt_CGContextStrokeRect(struct cgctx *c, CGRect r);
void iosrt_CGContextMoveToPoint(struct cgctx *c, double x, double y);
void iosrt_CGContextAddLineToPoint(struct cgctx *c, double x, double y);
CGRect iosrt_CGRectApplyAffineTransform(CGRect r, gfx_mat t);
rt_bool iosrt_CGRectContainsPoint(CGRect r, CGPoint p);
id iosrt_CGPathCreateCopyByTransformingPath(id p, const gfx_mat *m);
id iosrt_CGPathCreateMutable(void);
id iosrt_CGPathCreateWithRoundedRect(CGRect r, double cw, double ch, const CGAffineTransform *m);
void iosrt_CGPathAddRoundedRect(id path, const CGAffineTransform *m, CGRect r, double cw, double ch);
CGRect iosrt_CGPathGetBoundingBox(id path);
rt_bool iosrt_CGPathContainsPoint(id path, const CGAffineTransform *m, CGPoint p, long eo);
/* draw a CGImage into rect (UIKit orientation: top row at rect top) */
void rt_cg_draw_image_uikit(struct cgctx *c, id cgimage, CGRect r, float alpha, int blend, const float *tint);
/* 9-slice: insets in image pixels */
void rt_cg_draw_image_9slice(struct cgctx *c, id cgimage, CGRect r, UIEdgeInsets capsPx, double scale, float alpha, const float *tint, int tile);

/* ---- text (text.c) */
CGSize rt_text_measure(const char *utf8, id font, double maxw, long lines);
/* draws text inside rect with alignment (0 left, 1 center, 2 right), vertically centered if vcenter */
void rt_text_draw(struct cgctx *c, const char *utf8, id font, const float *rgba, CGRect r, long align, long lines, int vcenter);
id rt_attrs_font(id attrs);
id rt_attrs_color(id attrs);
long rt_attrs_align(id attrs);

/* ---- UIImage (image.c) */
id rt_uiimage_cgimage(id uiimage);
double rt_uiimage_scale(id uiimage);
CGSize rt_uiimage_size(id uiimage);
void rt_uiimage_draw(id img, struct cgctx *c, CGRect r, float alpha, const float *tint_or_null);
id rt_uiimage_from_cg(id cgimage, double scale);      /* autoreleased */

/* ---- layers (layer.c) */
void rt_layer_render(id layer, struct cgctx *c);
void rt_layers_init(void);
void rt_display_links_tick(void);
int rt_display_links_active(void);
id rt_layer_for_view(id view);
gfx_mat rt_layer_local(id layer);
gfx_mat rt_layer_to_root(id layer);
int rt_layers_animating(void);
void rt_layers_begin_frame(void);

/* frame invalidation (uikit.c) */
void rt_ui_mark_dirty(void);
extern float rt_scr_w, rt_scr_h, rt_scr_scale;


/* ---- UIKit object layouts (uikit.c / controls.c / nib.c) */
struct uiview {
    Class isa; id layer; id subviews; id superview; id vc; id gestures; id bg; id tint;
    long tag, interaction, needsLayout, autoresizing, autoresizesSubviews, contentMode, multitouch, exclusive, tintAdjust;
    id accessibilityLabel, accessibilityIdentifier, restorationIdentifier; long isAccessibilityElement; UIEdgeInsets layoutMargins;
};
struct uilabel { struct uiview v; id text, attributedText, textColor, highlightedTextColor, font, shadowColor; long align, lines, lineBreak, highlighted, enabled, adjusts; double minScale, preferredMaxWidth; CGSize shadowOffset; };
struct uiimageview { struct uiview v; id image, highlightedImage, animationImages; long highlighted, animating, animationRepeat; double animationDuration, animStart; };
struct uicontrol { struct uiview v; id targets; long enabled, selected, highlighted, tracking, touchInside, hAlign, vAlign; };
struct bcontent { id title, attrTitle, titleColor, shadowColor, image, bgImage; };
struct uibutton { struct uicontrol c; long type; struct bcontent st[8]; id titleLabel, imageView, font; UIEdgeInsets contentInsets, titleInsets, imageInsets; long adjustsHighlighted, adjustsDisabled, showsTouch; };
struct uiscroll {
    struct uiview v; CGSize contentSize; UIEdgeInsets inset; id delegate;
    long paging, scrollEnabled, bounces, alwaysBounceV, alwaysBounceH, showsH, showsV, delaysContent, cancelsContent, directionalLock, dragging, decelerating, scrollsToTop;
    double zoom, minZoom, maxZoom; id pan; CGPoint panStart; CGPoint vel; double decelT;
};
struct uiwindow { struct uiview v; id root; double level; long hiddenWin; };
struct uivc {
    Class isa; id view; id title; long loaded; id nibName, nibBundle; id parent, children, presented, presenting, navItem, tabBarItem;
    long modalStyle, transitionStyle, appeared, hidesBottomBar, edges, extendedLayout; id toolbarItems; id restorationIdentifier; CGSize preferredContentSize;
    /* storyboard scenes: the storyboard, segue templates, external objects for the view nib, objects kept alive */
    id storyboard, segueTemplates, viewExternals, keepAlive, sbIdentifier;
};
struct uiapp { Class isa; id delegate; id keyWindow; id windows; long idle, badge, statusBarHidden, ignoring, statusBarStyle; };
struct uitouch { Class isa; CGPoint pt, prev; id view, window; long phase; double ts; long tapCount; id gestures; long tid; };
struct target_action { Class isa; id target; SEL action; unsigned long events; };

/* uikit.c helpers */
id rt_view_new(Class c, CGRect frame);                /* +1 */
CGRect rt_view_bounds(id v);
CGRect rt_view_frame(id v);
void rt_view_set_frame(id v, CGRect f);
void rt_view_add_subview(id parent, id sub);
void rt_view_remove(id v);
id rt_view_window(id v);
gfx_mat rt_view_to_window(id v);                      /* view coords -> window coords */
CGPoint rt_view_convert(id from, CGPoint p, id to);   /* nil = window/screen */
id rt_view_tint(id v);
id rt_app_key_window(void);
void rt_send_action(SEL action, id target, id sender, id event);
void rt_gesture_attach_scroll(id scrollView);
double rt_now(void);
void rt_after(double delay, void (*fn)(void *), void *ctx);
/* controls.c */
void rt_controls_init(void);
void rt_control_draw(id view, struct cgctx *c, CGRect b);   /* draws built-in control content; returns quietly for others */
void rt_vc_load_view(id vc);
void rt_vc_appear(id vc, int appear);
/* storyboard.c */
const char *rt_storyboard_dir(id storyboard);
void rt_segue_template_set_source(id tmpl, id vc);
int rt_segue_template_is_embed(id tmpl);
id rt_segue_template_identifier(id tmpl);
int rt_segue_template_perform(id tmpl, id sender);
void rt_soft_error(const char *msg);
/* nib.c */
void rt_nib_init(void);
id rt_nib_instantiate(const char *path, id owner, id external);   /* autoreleased top-level array or nil */
const char *rt_nib_find(const char *name, id bundle);            /* static buffer or NULL */

#endif
