/*
 * UIKit, part 3: UIViewController (NIB-backed views, containment, modal
 * presentation), UINavigationController/Bar/Item, UIBarButtonItem, UIToolbar,
 * UITabBarController, UITableView/Cell (visible-row virtualization),
 * UIAlertView/UIActionSheet/UIAlertController, and assorted classes apps
 * expect to exist (pasteboard, menus, activity/popover/picker controllers,
 * web view, pickers, accessibility elements, NSLayoutConstraint).
 */
#include "uikit_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

RT_DECLARE_CLASS(NSObject);
RT_DECLARE_CLASS(NSMutableArray);
RT_DECLARE_CLASS(UIResponder);
RT_DECLARE_CLASS(UIView);
RT_DECLARE_CLASS(UIWindow);
RT_DECLARE_CLASS(UIControl);
RT_DECLARE_CLASS(UIButton);
RT_DECLARE_CLASS(UILabel);
RT_DECLARE_CLASS(UIImageView);
RT_DECLARE_CLASS(UIScrollView);
RT_DECLARE_CLASS(UITextField);
RT_DEFINE_CLASS(UIViewController);
RT_DEFINE_CLASS(UINavigationController);
RT_DEFINE_CLASS(UITabBarController);
RT_DEFINE_CLASS(UITableViewController);
RT_DEFINE_CLASS(UINavigationBar);
RT_DEFINE_CLASS(UINavigationItem);
RT_DEFINE_CLASS(UIBarItem);
RT_DEFINE_CLASS(UIBarButtonItem);
RT_DEFINE_CLASS(UITabBarItem);
RT_DEFINE_CLASS(UIToolbar);
RT_DEFINE_CLASS(UITabBar);
RT_DEFINE_CLASS(UITableView);
RT_DEFINE_CLASS(UITableViewCell);
RT_DEFINE_CLASS(UIAlertView);
RT_DEFINE_CLASS(UIActionSheet);
RT_DEFINE_CLASS(UIAlertController);
RT_DEFINE_CLASS(UIAlertAction);
RT_DEFINE_CLASS(UIPasteboard);
RT_DEFINE_CLASS(UIMenuController);
RT_DEFINE_CLASS(UIMenuItem);
RT_DEFINE_CLASS(UIActivity);
RT_DEFINE_CLASS(UIActivityItemProvider);
RT_DEFINE_CLASS(UIActivityViewController);
RT_DEFINE_CLASS(UIPopoverController);
RT_DEFINE_CLASS(UIDocumentInteractionController);
RT_DEFINE_CLASS(UIImagePickerController);
RT_DEFINE_CLASS(UIWebView);
RT_DEFINE_CLASS(UIPickerView);
RT_DEFINE_CLASS(UIDatePicker);
RT_DEFINE_CLASS(UIAccessibilityElement);
RT_DEFINE_CLASS(NSLayoutConstraint);
RT_DEFINE_CLASS(UIVisualEffectView);
RT_DEFINE_CLASS(UIBlurEffect);
RT_DEFINE_CLASS(UIStoryboard);
RT_DEFINE_CLASS(UITraitCollection);
RT_DEFINE_CLASS(UIRefreshControl);
RT_DEFINE_CLASS(UIPopoverPresentationController);

id rt_set_new(id *objs, size_t n);
#define V(x) ((struct uiview *)(x))
#define VC(x) ((struct uivc *)(x))
static id mkarray(void) { return RT_SEND0(id, RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "alloc"), "init"); }
static void nop(id self, SEL s) { (void)self; (void)s; }
static void nopL(id self, SEL s, long v) { (void)self; (void)s; (void)v; }
static void nop1(id self, SEL s, id v) { (void)self; (void)s; (void)v; }
static void nop1L(id self, SEL s, id v, long b) { (void)self; (void)s; (void)v; (void)b; }
static id nil0(id self, SEL s) { (void)self; (void)s; return NULL; }
static id self0(id self, SEL s) { (void)s; return self; }
static rt_bool yes0(id self, SEL s) { (void)self; (void)s; return 1; }
static rt_bool no0(id self, SEL s) { (void)self; (void)s; return 0; }
static long zeroL(id self, SEL s) { (void)self; (void)s; return 0; }
static void call_block0(void *b) { if (b) ((void (*)(void *))((struct rt_block *)b)->invoke)(b); }
static id supercall_frame(id self, Class cls, CGRect fr) { struct { id r; Class c; } sup = { self, cls }; return ((id (*)(void *, SEL, CGRect))objc_msgSendSuper2)(&sup, rt_sel("initWithFrame:"), fr); }
static CGRect screen_bounds(void) { return (CGRect){ { 0, 0 }, { rt_scr_w, rt_scr_h } }; }

/* ======================================================= UIViewController */
static id vc_init(id self, SEL s) { (void)s; return RT_SEND2(id, self, "initWithNibName:bundle:", id, NULL, id, NULL); }
static id vc_initWithNib(id self, SEL s, id nib, id bundle) {
    (void)s; struct uivc *c = VC(self);
    c->nibName = nib ? RT_SEND0(id, nib, "copy") : NULL; c->nibBundle = objc_retain(bundle);
    c->children = mkarray(); c->edges = 15; c->extendedLayout = 15;
    return self;
}
static id vc_initWithCoder(id self, SEL s, id coder) {
    (void)s; self = vc_initWithNib(self, NULL, NULL, NULL);
    id nib = RT_SEND1(id, coder, "decodeObjectForKey:", id, rt_str("UINibName"));
    if (nib) VC(self)->nibName = objc_retain(nib);
    id title = RT_SEND1(id, coder, "decodeObjectForKey:", id, rt_str("UITitle"));
    if (title) VC(self)->title = objc_retain(title);
    id view = RT_SEND1(id, coder, "decodeObjectForKey:", id, rt_str("UIView"));
    if (view) RT_SEND1(void, self, "setView:", id, view);
    return self;
}
static void vc_dealloc(id self, SEL s) {
    (void)s; struct uivc *c = VC(self);
    if (c->view && V(c->view)->vc == self) V(c->view)->vc = NULL;
    objc_release(c->view); objc_release(c->title); objc_release(c->nibName); objc_release(c->nibBundle); objc_release(c->children);
    objc_release(c->presented); objc_release(c->navItem); objc_release(c->tabBarItem); objc_release(c->toolbarItems);
    rt_dealloc_instance(self);
}
static id vc_nibName(id self, SEL s) { (void)s; return VC(self)->nibName; }
static id vc_nibBundle(id self, SEL s) { (void)s; return VC(self)->nibBundle; }
static void vc_loadView(id self, SEL s) {
    (void)s; struct uivc *c = VC(self);
    /* NIB: explicit name, else class name, else class name without "Controller" */
    const char *path = NULL;
    if (c->nibName) path = rt_nib_find(rt_str_utf8(c->nibName), c->nibBundle);
    else {
        char n[256]; snprintf(n, sizeof n, "%s", rt_class_name(self->isa));
        char *ctl = strstr(n, "Controller");
        if (ctl && !ctl[10]) { *ctl = 0; path = rt_nib_find(n, NULL); *ctl = 'C'; }
        if (!path) path = rt_nib_find(n, NULL);
    }
    if (path) {
        rt_nib_instantiate(path, self, NULL);
        if (!c->view) rt_warn("NIB %s did not connect a view to its owner", path);
    }
    if (!c->view) {
        id v = rt_view_new(RT_CLASS(UIView), screen_bounds());
        V(v)->autoresizing = 2 | 16;
        RT_SEND1(void, self, "setView:", id, v);
        objc_release(v);
    }
}
void rt_vc_load_view(id self) {
    struct uivc *c = VC(self);
    if (c->view || c->loaded) return;
    c->loaded = 1;
    if (rt_responds(self, rt_sel("loadViewIfNeeded")) && 0) {}
    RT_SEND0(void, self, "loadView");
    if (c->view) { V(c->view)->vc = self; RT_SEND0(void, self, "viewDidLoad"); }
}
static id vc_view(id self, SEL s) { (void)s; rt_vc_load_view(self); return VC(self)->view; }
static id vc_viewIfLoaded(id self, SEL s) { (void)s; return VC(self)->view; }
static void vc_loadViewIfNeeded(id self, SEL s) { (void)s; rt_vc_load_view(self); }
static void vc_setView(id self, SEL s, id v) {
    (void)s; struct uivc *c = VC(self);
    if (c->view == v) return;
    if (c->view && V(c->view)->vc == self) V(c->view)->vc = NULL;
    objc_retain(v); objc_release(c->view); c->view = v; c->loaded = 1;
    if (v) V(v)->vc = self;
}
static rt_bool vc_isViewLoaded(id self, SEL s) { (void)s; return VC(self)->view != NULL; }
static id vc_title(id self, SEL s) { (void)s; return VC(self)->title; }
static void vc_setTitle(id self, SEL s, id t) { (void)s; struct uivc *c = VC(self); id v = t ? RT_SEND0(id, t, "copy") : NULL; objc_release(c->title); c->title = v; rt_ui_mark_dirty(); }
static id vc_nextResponder(id self, SEL s) {
    (void)s; struct uivc *c = VC(self);
    if (c->view && V(c->view)->superview) return V(c->view)->superview;
    if (c->presenting) return c->presenting;
    return c->parent;
}
static unsigned long vc_orientations(id self, SEL s) { (void)self; (void)s; return 2; }
static long vc_interfaceOrientation(id self, SEL s) { (void)self; (void)s; return 1; }
void rt_vc_appear(id vc, int appear) {
    struct uivc *c = VC(vc);
    if (!vc || (appear && c->appeared) || (!appear && !c->appeared)) return;
    rt_vc_load_view(vc);
    if (appear) { RT_SEND1(void, vc, "viewWillAppear:", long, 0); c->appeared = 1; }
    else { RT_SEND1(void, vc, "viewWillDisappear:", long, 0); c->appeared = 0; }
    /* containment: forward to children whose views are on screen */
    for (size_t i = 0; c->children && i < rt_array_count(c->children); i++) {
        id ch = rt_array_at(c->children, i); id cv = VC(ch)->view;
        if (!appear || (cv && V(cv)->superview)) rt_vc_appear(ch, appear);
    }
    if (c->view) RT_SEND0(void, c->view, "setNeedsLayout");
    if (appear) RT_SEND1(void, vc, "viewDidAppear:", long, 0); else RT_SEND1(void, vc, "viewDidDisappear:", long, 0);
}
static id vc_parent(id self, SEL s) { (void)s; return VC(self)->parent; }
static id vc_children(id self, SEL s) { (void)s; return VC(self)->children ? VC(self)->children : objc_autorelease(mkarray()); }
static void vc_addChild(id self, SEL s, id child) {
    (void)s; struct uivc *c = VC(self);
    if (!child || VC(child)->parent == self) return;
    if (!c->children) c->children = mkarray();
    RT_SEND1(void, child, "willMoveToParentViewController:", id, self);
    RT_SEND1(void, c->children, "addObject:", id, child);
    VC(child)->parent = self;
}
static void vc_removeFromParent(id self, SEL s) {
    (void)s; id p = VC(self)->parent; if (!p) return;
    objc_retain(self);
    VC(self)->parent = NULL;
    RT_SEND1(void, VC(p)->children, "removeObjectIdenticalTo:", id, self);
    RT_SEND1(void, self, "didMoveToParentViewController:", id, NULL);
    objc_autorelease(self);
}
static id find_ancestor(id self, Class cls) { for (id p = VC(self)->parent; p; p = VC(p)->parent) if (rt_is_kind_of(p, cls)) return p; return NULL; }
static id vc_navController(id self, SEL s) { (void)s; return find_ancestor(self, RT_CLASS(UINavigationController)); }
static id vc_tabBarController(id self, SEL s) { (void)s; return find_ancestor(self, RT_CLASS(UITabBarController)); }
static id vc_navItem(id self, SEL s) {
    (void)s; struct uivc *c = VC(self);
    if (!c->navItem) { c->navItem = RT_SEND0(id, RT_SEND0(id, (id)RT_CLASS(UINavigationItem), "alloc"), "init"); RT_SEND1(void, c->navItem, "setTitle:", id, c->title); }
    return c->navItem;
}
static id vc_tabBarItem(id self, SEL s) { (void)s; struct uivc *c = VC(self); if (!c->tabBarItem) c->tabBarItem = RT_SEND0(id, RT_SEND0(id, (id)RT_CLASS(UITabBarItem), "alloc"), "init"); return c->tabBarItem; }
static void vc_setTabBarItem(id self, SEL s, id v) { (void)s; objc_retain(v); objc_release(VC(self)->tabBarItem); VC(self)->tabBarItem = v; }
static id vc_presented(id self, SEL s) { (void)s; return VC(self)->presented; }
static id vc_presenting(id self, SEL s) { (void)s; return VC(self)->presenting; }
static long vc_modalStyle(id self, SEL s) { (void)s; return VC(self)->modalStyle; }
static void vc_setModalStyle(id self, SEL s, long v) { (void)s; VC(self)->modalStyle = v; }
static void vc_setTransitionStyle(id self, SEL s, long v) { (void)s; VC(self)->transitionStyle = v; }
struct pres_done { id vc; void *block; };
static void pres_done(void *p) { struct pres_done *d = p; call_block0(d->block); if (d->block) _Block_release(d->block); objc_release(d->vc); free(d); }
static void vc_present(id self, SEL s, id vc, long animated, void *completion) {
    (void)s; (void)animated;
    if (!vc) return;
    id top = self; while (VC(top)->presented) top = VC(top)->presented;   /* present from the topmost */
    VC(top)->presented = objc_retain(vc); VC(vc)->presenting = top;
    id win = VC(top)->view ? rt_view_window(VC(top)->view) : NULL;
    if (!win) win = rt_app_key_window();
    id view = RT_SEND0(id, vc, "view");
    if (win && view) {
        CGRect wb = rt_view_bounds(win);
        long style = VC(vc)->modalStyle;
        if (style == 2 || style == 3 || style == 7) {   /* form/page sheet, popover: centered card on phones = full screen */ }
        rt_view_set_frame(view, wb);
        V(view)->autoresizing = 2 | 16;
        rt_view_add_subview(win, view);
    }
    rt_vc_appear(vc, 1);
    struct pres_done *d = malloc(sizeof *d); d->vc = objc_retain(vc); d->block = completion ? _Block_copy(completion) : NULL;
    rt_after(0, pres_done, d);
}
static void vc_presentModal(id self, SEL s, id vc, long animated) { vc_present(self, s, vc, animated, NULL); }
static void vc_dismiss(id self, SEL s, long animated, void *completion) {
    (void)s; (void)animated;
    id target = VC(self)->presented ? VC(self)->presented : self;
    id presenter = VC(target)->presenting;
    if (!presenter) { if (completion) { struct pres_done *d = malloc(sizeof *d); d->vc = NULL; d->block = _Block_copy(completion); rt_after(0, pres_done, d); } return; }
    if (VC(target)->presented) vc_dismiss(target, NULL, 0, NULL);
    rt_vc_appear(target, 0);
    if (VC(target)->view) rt_view_remove(VC(target)->view);
    VC(presenter)->presented = NULL; VC(target)->presenting = NULL;
    struct pres_done *d = malloc(sizeof *d); d->vc = target; d->block = completion ? _Block_copy(completion) : NULL;   /* takes the presenter's reference */
    rt_after(0, pres_done, d);
}
static void vc_dismissModal(id self, SEL s, long animated) { vc_dismiss(self, s, animated, NULL); }
static void vc_transition(id self, SEL s, id from, id to, double d, unsigned long o, void *anims, void *comp) {
    (void)s; (void)d; (void)o; (void)self;
    id fv = VC(from)->view, sup = fv ? V(fv)->superview : NULL;
    if (sup) { id tv = RT_SEND0(id, to, "view"); rt_view_set_frame(tv, rt_view_frame(fv)); rt_view_remove(fv); rt_view_add_subview(sup, tv); }
    call_block0(anims);
    if (comp) ((void (*)(void *, long))((struct rt_block *)comp)->invoke)(comp, 1);
}
static id vc_storyboard(id self, SEL s) { (void)self; (void)s; return NULL; }
static CGSize vc_preferredSize(id self, SEL s) { (void)s; return VC(self)->preferredContentSize; }
static void vc_setPreferredSize(id self, SEL s, CGSize v) { (void)s; VC(self)->preferredContentSize = v; }
static id vc_toolbarItems(id self, SEL s) { (void)s; return VC(self)->toolbarItems; }
static void vc_setToolbarItems(id self, SEL s, id v) { (void)s; objc_retain(v); objc_release(VC(self)->toolbarItems); VC(self)->toolbarItems = v; }
static void vc_setToolbarItemsAnim(id self, SEL s, id v, long a) { (void)a; vc_setToolbarItems(self, s, v); }
static id vc_traits(id self, SEL s) { (void)self; (void)s; return objc_autorelease(rt_alloc_instance(RT_CLASS(UITraitCollection), 0)); }
static void vc_performSegue(id self, SEL s, id ident, id sender) { (void)self; (void)s; (void)sender; rt_warn("performSegueWithIdentifier:%s ignored (storyboards are not supported)", ident ? rt_str_utf8(ident) : "?"); }
static id vc_popoverPC(id self, SEL s) { (void)self; (void)s; return NULL; }
static void vc_setNeedsStatusBarUpdate(id self, SEL s) { (void)self; (void)s; }
static long vc_statusBarStyle(id self, SEL s) { (void)self; (void)s; return 0; }
static UIEdgeInsets vc_safeArea(id self, SEL s) { (void)self; (void)s; return (UIEdgeInsets){ 0, 0, 0, 0 }; }
static id vc_layoutGuide(id self, SEL s) { (void)s; return self; }
static double vc_guideLength(id self, SEL s) { (void)self; (void)s; return 0; }

/* =================================================== nav item & bar items */
struct uinavitem { Class isa; id title, titleView, left, right, back, prompt; long hidesBack; };
#define NI(x) ((struct uinavitem *)(x))
static id ni_initWithTitle(id self, SEL s, id t) { (void)s; NI(self)->title = t ? RT_SEND0(id, t, "copy") : NULL; return self; }
#define NIID(get, set, field) static id get(id self, SEL s) { (void)s; return NI(self)->field; } static void set(id self, SEL s, id v) { (void)s; objc_retain(v); objc_release(NI(self)->field); NI(self)->field = v; rt_ui_mark_dirty(); }
NIID(ni_title, ni_setTitle, title) NIID(ni_titleView, ni_setTitleView, titleView) NIID(ni_back, ni_setBack, back) NIID(ni_prompt, ni_setPrompt, prompt)
static id ni_left(id self, SEL s) { (void)s; id l = NI(self)->left; return l && rt_array_count(l) ? rt_array_at(l, 0) : NULL; }
static id ni_right(id self, SEL s) { (void)s; id r = NI(self)->right; return r && rt_array_count(r) ? rt_array_at(r, 0) : NULL; }
static void ni_setLefts(id self, SEL s, id arr) { (void)s; id c = arr ? RT_SEND0(id, arr, "copy") : NULL; objc_release(NI(self)->left); NI(self)->left = c; rt_ui_mark_dirty(); }
static void ni_setRights(id self, SEL s, id arr) { (void)s; id c = arr ? RT_SEND0(id, arr, "copy") : NULL; objc_release(NI(self)->right); NI(self)->right = c; rt_ui_mark_dirty(); }
static void ni_setLeft(id self, SEL s, id b) { ni_setLefts(self, s, b ? objc_autorelease(rt_array_new(&b, 1)) : NULL); }
static void ni_setRight(id self, SEL s, id b) { ni_setRights(self, s, b ? objc_autorelease(rt_array_new(&b, 1)) : NULL); }
static void ni_setLeftAnim(id self, SEL s, id b, long a) { (void)a; ni_setLeft(self, s, b); }
static void ni_setRightAnim(id self, SEL s, id b, long a) { (void)a; ni_setRight(self, s, b); }
static void ni_setLeftsAnim(id self, SEL s, id b, long a) { (void)a; ni_setLefts(self, s, b); }
static void ni_setRightsAnim(id self, SEL s, id b, long a) { (void)a; ni_setRights(self, s, b); }
static id ni_lefts(id self, SEL s) { (void)s; return NI(self)->left; }
static id ni_rights(id self, SEL s) { (void)s; return NI(self)->right; }
static void ni_setHidesBack(id self, SEL s, long v) { (void)s; NI(self)->hidesBack = v & 0xff; }
static void ni_setHidesBackAnim(id self, SEL s, long v, long a) { (void)a; ni_setHidesBack(self, s, v); }

struct uibaritem { Class isa; id title, image, target, customView, tint; SEL action; long style, enabled, tag, sys; double width; id landscapeImage; };
#define BI(x) ((struct uibaritem *)(x))
static id bi_init(id self, SEL s) { (void)s; BI(self)->enabled = 1; return self; }
static id bi_initTitle(id self, SEL s, id t, long style, id target, SEL action) { bi_init(self, s); BI(self)->title = objc_retain(t); BI(self)->style = style; BI(self)->target = target; BI(self)->action = action; return self; }
static id bi_initImage(id self, SEL s, id img, long style, id target, SEL action) { bi_init(self, s); BI(self)->image = objc_retain(img); BI(self)->style = style; BI(self)->target = target; BI(self)->action = action; return self; }
static id bi_initImage2(id self, SEL s, id img, id li, long style, id target, SEL action) { (void)li; return bi_initImage(self, s, img, style, target, action); }
static const char *sys_title(long sys) {
    static const char *t[] = { "Done", "Cancel", "Edit", "Save", "+", "", "", "Compose", "Reply", "Action", "Organize", "Bookmarks", "Search", "Refresh", "Stop", "Camera", "Trash", "Play", "Pause", "Rewind", "Forward", "Undo", "Redo", "" };
    return sys >= 0 && sys < 24 ? t[sys] : "";
}
static id bi_initSystem(id self, SEL s, long sys, id target, SEL action) { bi_init(self, s); BI(self)->sys = sys; BI(self)->title = objc_retain(rt_str(sys_title(sys))); BI(self)->target = target; BI(self)->action = action; return self; }
static id bi_initCustom(id self, SEL s, id v) { bi_init(self, s); BI(self)->customView = objc_retain(v); return self; }
#define BIID(get, set, field) static id get(id self, SEL s) { (void)s; return BI(self)->field; } static void set(id self, SEL s, id v) { (void)s; objc_retain(v); objc_release(BI(self)->field); BI(self)->field = v; rt_ui_mark_dirty(); }
BIID(bi_title, bi_setTitle, title) BIID(bi_image, bi_setImage, image) BIID(bi_customView, bi_setCustomView, customView) BIID(bi_tint, bi_setTint, tint)
static id bi_target(id self, SEL s) { (void)s; return BI(self)->target; }
static void bi_setTarget(id self, SEL s, id t) { (void)s; BI(self)->target = t; }
static SEL bi_action(id self, SEL s) { (void)s; return BI(self)->action; }
static void bi_setAction(id self, SEL s, SEL a) { (void)s; BI(self)->action = a; }
static rt_bool bi_enabled(id self, SEL s) { (void)s; return BI(self)->enabled != 0; }
static void bi_setEnabled(id self, SEL s, long v) { (void)s; BI(self)->enabled = v & 0xff; rt_ui_mark_dirty(); }
static long bi_tag(id self, SEL s) { (void)s; return BI(self)->tag; }
static void bi_setTag(id self, SEL s, long v) { (void)s; BI(self)->tag = v; }
static long bi_style(id self, SEL s) { (void)s; return BI(self)->style; }
static void bi_setStyle(id self, SEL s, long v) { (void)s; BI(self)->style = v; }
static double bi_width(id self, SEL s) { (void)s; return BI(self)->width; }
static void bi_setWidth(id self, SEL s, double v) { (void)s; BI(self)->width = v; }
static void bar_item_fire(id item, id sender) { if (BI(item)->enabled && BI(item)->action) rt_send_action(BI(item)->action, BI(item)->target, item, NULL); (void)sender; }

/* ===================================================== bars (nav, tool, tab) */
struct uibar { struct uiview v; id items; id nav; id tint, barTint, bgImage; long style, translucent; id delegate; long selected; };
#define BAR(x) ((struct uibar *)(x))
static double item_width(id it) {
    if (BI(it)->customView) return rt_view_frame(BI(it)->customView).size.width;
    if (BI(it)->width > 0) return BI(it)->width;
    if (BI(it)->sys == 5 || BI(it)->sys == 6) return BI(it)->sys == 6 ? BI(it)->width : -1;   /* flexible space */
    if (BI(it)->image) return rt_uiimage_size(BI(it)->image).width + 16;
    if (BI(it)->title) return rt_text_measure(rt_str_utf8(BI(it)->title), rt_font_make(17, 0, NULL), 0, 1).width + 20;
    return 44;
}
/* lay items out across a bar; returns count, fills rects */
static int layout_items(id items, CGRect b, CGRect *out, int max) {
    int n = items ? (int)rt_array_count(items) : 0; if (n > max) n = max;
    double fixed = 0; int flex = 0;
    for (int i = 0; i < n; i++) { double w = item_width(rt_array_at(items, i)); if (w < 0) flex++; else fixed += w; }
    double fw = flex ? fmax(0, (b.size.width - 16 - fixed) / flex) : 0, x = b.origin.x + 8;
    if (!flex && n) x = b.origin.x + 8;
    for (int i = 0; i < n; i++) { double w = item_width(rt_array_at(items, i)); if (w < 0) w = fw; out[i] = (CGRect){ { x, b.origin.y }, { w, b.size.height } }; x += w; }
    return n;
}
static void draw_bar_item(struct cgctx *c, id it, CGRect r, id tint) {
    if (BI(it)->customView || BI(it)->sys == 5 || BI(it)->sys == 6) return;
    float col[4]; rt_color_rgba(BI(it)->tint ? BI(it)->tint : tint, col);
    if (!BI(it)->enabled) col[3] *= 0.35f;
    if (BI(it)->image) { CGSize sz = rt_uiimage_size(BI(it)->image); rt_uiimage_draw(BI(it)->image, c, (CGRect){ { r.origin.x + (r.size.width - sz.width) / 2, r.origin.y + (r.size.height - sz.height) / 2 }, sz }, 1, col); }
    else if (BI(it)->title) rt_text_draw(c, rt_str_utf8(BI(it)->title), rt_font_make(17, BI(it)->style == 2, NULL), col, r, 1, 1, 1);
}
static void sync_custom_views(id bar, id items, CGRect *rects, int n) {
    for (int i = 0; i < n; i++) {
        id cv = BI(rt_array_at(items, i))->customView;
        if (!cv) continue;
        CGRect f = rt_view_frame(cv);
        f.origin.x = rects[i].origin.x; f.origin.y = rects[i].origin.y + (rects[i].size.height - f.size.height) / 2;
        rt_view_set_frame(cv, f);
        if (V(cv)->superview != bar) rt_view_add_subview(bar, cv);
    }
}
static id bar_init(id self, Class cls, CGRect fr) { self = supercall_frame(self, cls, fr); BAR(self)->translucent = 1; BAR(self)->items = mkarray(); return self; }
static id tb_initWithFrame(id self, SEL s, CGRect fr) { (void)s; return bar_init(self, RT_CLASS(UIToolbar), fr); }
static id tb_items(id self, SEL s) { (void)s; return BAR(self)->items; }
static void tb_setItems(id self, SEL s, id items) { (void)s; id c = items ? RT_SEND0(id, items, "mutableCopy") : mkarray(); objc_release(BAR(self)->items); BAR(self)->items = c; RT_SEND0(void, self, "setNeedsLayout"); rt_ui_mark_dirty(); }
static void tb_setItemsAnim(id self, SEL s, id items, long a) { (void)a; tb_setItems(self, s, items); }
#define BARID(get, set, field) static id get(id self, SEL s) { (void)s; return BAR(self)->field; } static void set(id self, SEL s, id v) { (void)s; objc_retain(v); objc_release(BAR(self)->field); BAR(self)->field = v; rt_ui_mark_dirty(); }
BARID(bar_barTint, bar_setBarTint, barTint)
static void bar_setStyle(id self, SEL s, long v) { (void)s; BAR(self)->style = v; rt_ui_mark_dirty(); }
static long bar_style(id self, SEL s) { (void)s; return BAR(self)->style; }
static void bar_setTranslucent(id self, SEL s, long v) { (void)s; BAR(self)->translucent = v & 0xff; }
static rt_bool bar_translucent(id self, SEL s) { (void)s; return BAR(self)->translucent != 0; }
static void bar_setBgImage(id self, SEL s, id img, long pos, long metrics) { (void)s; (void)pos; (void)metrics; objc_retain(img); objc_release(BAR(self)->bgImage); BAR(self)->bgImage = img; rt_ui_mark_dirty(); }
static void bar_setBgImage2(id self, SEL s, id img, long metrics) { bar_setBgImage(self, s, img, 0, metrics); }
static void draw_bar_bg(id view, struct cgctx *c, CGRect b) {
    struct uibar *x = BAR(view);
    if (x->bgImage) { rt_uiimage_draw(x->bgImage, c, b, 1, NULL); return; }
    id bg = x->barTint ? x->barTint : x->style == 1 ? rt_color_make(0.11, 0.11, 0.11, x->translucent ? 0.92 : 1) : rt_color_make(0.97, 0.97, 0.97, x->translucent ? 0.94 : 1);
    iosrt_CGContextSetFillColorWithColor(c, bg); iosrt_CGContextFillRect(c, b);
}
static void draw_toolbar(id view, struct cgctx *c, CGRect b) {
    draw_bar_bg(view, c, b);
    CGRect r[32]; int n = layout_items(BAR(view)->items, b, r, 32);
    sync_custom_views(view, BAR(view)->items, r, n);
    for (int i = 0; i < n; i++) draw_bar_item(c, rt_array_at(BAR(view)->items, i), r[i], rt_view_tint(view));
}
static void bar_tap_items(id view, id touches, id items, CGRect b) {
    CGPoint p = RT_SEND1(CGPoint, RT_SEND0(id, touches, "anyObject"), "locationInView:", id, view);
    CGRect r[32]; int n = layout_items(items, b, r, 32);
    for (int i = 0; i < n; i++) if (p.x >= r[i].origin.x && p.x < r[i].origin.x + r[i].size.width) { bar_item_fire(rt_array_at(items, i), view); return; }
}
static void tb_touchesEnded(id self, SEL s, id t, id e) { (void)s; (void)e; bar_tap_items(self, t, BAR(self)->items, rt_view_bounds(self)); }
static void bar_touchesBegan(id self, SEL s, id t, id e) { (void)self; (void)s; (void)t; (void)e; }
/* navigation bar: driven by its UINavigationController (x->nav) */
static id nb_initWithFrame(id self, SEL s, CGRect fr) { (void)s; return bar_init(self, RT_CLASS(UINavigationBar), fr); }
static id nb_items(id self, SEL s) { (void)s; return BAR(self)->items; }
static id nb_topItem(id self, SEL s) { (void)s; id it = BAR(self)->items; return it && rt_array_count(it) ? rt_array_at(it, rt_array_count(it) - 1) : NULL; }
static id nb_backItem(id self, SEL s) { (void)s; id it = BAR(self)->items; return it && rt_array_count(it) > 1 ? rt_array_at(it, rt_array_count(it) - 2) : NULL; }
static void nb_pushItem(id self, SEL s, id item, long a) { (void)s; (void)a; if (item) RT_SEND1(void, BAR(self)->items, "addObject:", id, item); rt_ui_mark_dirty(); }
static id nb_popItem(id self, SEL s, long a) { (void)s; (void)a; id it = BAR(self)->items; size_t n = rt_array_count(it); if (!n) return NULL; id last = objc_autorelease(objc_retain(rt_array_at(it, n - 1))); RT_SEND0(void, it, "removeLastObject"); rt_ui_mark_dirty(); return last; }
static void nb_setItems(id self, SEL s, id items, long a) { (void)a; tb_setItems(self, s, items); }
static id nb_delegate(id self, SEL s) { (void)s; return BAR(self)->delegate; }
static void nb_setDelegate(id self, SEL s, id d) { (void)s; BAR(self)->delegate = d; }
static void nb_setTitleAttrs(id self, SEL s, id a) { (void)self; (void)s; (void)a; }
static void navbar_rects(id view, CGRect b, CGRect *left, CGRect *right, int *nl, int *nr, id *li, id *ri) {
    id top = nb_topItem(view, NULL);
    *li = top ? NI(top)->left : NULL; *ri = top ? NI(top)->right : NULL;
    *nl = layout_items(*li, (CGRect){ { b.origin.x, b.origin.y + b.size.height - 44 }, { b.size.width / 2, 44 } }, left, 8);
    CGRect rr = { { b.origin.x + b.size.width / 2, b.origin.y + b.size.height - 44 }, { b.size.width / 2, 44 } };
    *nr = layout_items(*ri, rr, right, 8);
    /* right items are right-aligned */
    double used = 0; for (int i = 0; i < *nr; i++) used += right[i].size.width;
    double shift = rr.size.width - 8 - used - 8;
    for (int i = 0; i < *nr; i++) right[i].origin.x += shift;
}
static void draw_navbar(id view, struct cgctx *c, CGRect b) {
    draw_bar_bg(view, c, b);
    id top = nb_topItem(view, NULL), back = nb_backItem(view, NULL);
    id tint = rt_view_tint(view);
    CGRect l[8], r[8]; int nl, nr; id li, ri;
    navbar_rects(view, b, l, r, &nl, &nr, &li, &ri);
    sync_custom_views(view, li, l, nl); sync_custom_views(view, ri, r, nr);
    for (int i = 0; i < nl; i++) draw_bar_item(c, rt_array_at(li, i), l[i], tint);
    for (int i = 0; i < nr; i++) draw_bar_item(c, rt_array_at(ri, i), r[i], tint);
    if (!nl && back && !(top && NI(top)->hidesBack)) {
        float col[4]; rt_color_rgba(tint, col);
        id bt = NI(back)->back ? BI(NI(back)->back)->title : NI(back)->title;
        char buf[256]; snprintf(buf, sizeof buf, "\xe2\x80\xb9 %s", bt ? rt_str_utf8(bt) : "Back");
        rt_text_draw(c, buf, rt_font_make(17, 0, NULL), col, (CGRect){ { b.origin.x + 8, b.origin.y + b.size.height - 44 }, { b.size.width / 3, 44 } }, 0, 1, 1);
    }
    if (top && NI(top)->titleView) {
        id tv = NI(top)->titleView; CGRect f = rt_view_frame(tv);
        f.origin.x = (b.size.width - f.size.width) / 2; f.origin.y = b.origin.y + b.size.height - 44 + (44 - f.size.height) / 2;
        rt_view_set_frame(tv, f); if (V(tv)->superview != view) rt_view_add_subview(view, tv);
    } else if (top && NI(top)->title) {
        float col[4]; rt_color_rgba(BAR(view)->style == 1 ? rt_color_make(1, 1, 1, 1) : rt_color_make(0, 0, 0, 1), col);
        rt_text_draw(c, rt_str_utf8(NI(top)->title), rt_font_make(17, 1, NULL), col, (CGRect){ { b.origin.x + b.size.width / 4, b.origin.y + b.size.height - 44 }, { b.size.width / 2, 44 } }, 1, 1, 1);
    }
}
static void nb_touchesEnded(id self, SEL s, id t, id e) {
    (void)s; (void)e;
    CGRect b = rt_view_bounds(self), l[8], r[8]; int nl, nr; id li, ri;
    navbar_rects(self, b, l, r, &nl, &nr, &li, &ri);
    CGPoint p = RT_SEND1(CGPoint, RT_SEND0(id, t, "anyObject"), "locationInView:", id, self);
    for (int i = 0; i < nl; i++) if (p.x >= l[i].origin.x && p.x < l[i].origin.x + l[i].size.width) { bar_item_fire(rt_array_at(li, i), self); return; }
    for (int i = 0; i < nr; i++) if (p.x >= r[i].origin.x && p.x < r[i].origin.x + r[i].size.width) { bar_item_fire(rt_array_at(ri, i), self); return; }
    if (!nl && p.x < b.size.width / 3 && nb_backItem(self, NULL) && BAR(self)->nav) RT_SEND1(id, BAR(self)->nav, "popViewControllerAnimated:", long, 1);
}
/* tab bar */
static id tab_initWithFrame(id self, SEL s, CGRect fr) { (void)s; self = bar_init(self, RT_CLASS(UITabBar), fr); BAR(self)->selected = -1; return self; }
static void draw_tabbar(id view, struct cgctx *c, CGRect b) {
    draw_bar_bg(view, c, b);
    id items = BAR(view)->items; size_t n = items ? rt_array_count(items) : 0;
    if (!n) return;
    double w = b.size.width / n;
    for (size_t i = 0; i < n; i++) {
        id it = rt_array_at(items, i);
        float col[4]; rt_color_rgba((long)i == BAR(view)->selected ? rt_view_tint(view) : rt_color_make(0.57, 0.57, 0.57, 1), col);
        CGRect r = { { b.origin.x + i * w, b.origin.y }, { w, b.size.height } };
        if (BI(it)->image) { CGSize sz = rt_uiimage_size(BI(it)->image); rt_uiimage_draw(BI(it)->image, c, (CGRect){ { r.origin.x + (w - sz.width) / 2, r.origin.y + 4 }, sz }, 1, col); }
        if (BI(it)->title) rt_text_draw(c, rt_str_utf8(BI(it)->title), rt_font_make(10, 0, NULL), col, (CGRect){ { r.origin.x, r.origin.y + r.size.height - 16 }, { w, 14 } }, 1, 1, 0);
    }
}
static void tab_touchesEnded(id self, SEL s, id t, id e);

/* =================================================== UINavigationController */
struct uinav { struct uivc vc; id stack, bar, toolbar, delegate; long barHidden, toolbarHidden; };
#define NAV(x) ((struct uinav *)(x))
static void nav_ensure(id self) {
    struct uinav *n = NAV(self);
    if (!n->stack) n->stack = mkarray();
    if (!n->bar) { n->bar = rt_view_new(RT_CLASS(UINavigationBar), (CGRect){ { 0, 0 }, { rt_scr_w, 64 } }); BAR(n->bar)->nav = self; V(n->bar)->autoresizing = 2; }
    if (!n->toolbar) { n->toolbar = rt_view_new(RT_CLASS(UIToolbar), (CGRect){ { 0, rt_scr_h - 44 }, { rt_scr_w, 44 } }); n->toolbarHidden = 1; V(n->toolbar)->autoresizing = 2 | 8; }
}
static id nav_init(id self, SEL s) { (void)s; self = vc_initWithNib(self, NULL, NULL, NULL); nav_ensure(self); return self; }
static id nav_initWithRoot(id self, SEL s, id root);
static id nav_top(id self, SEL s) { (void)s; id st = NAV(self)->stack; return st && rt_array_count(st) ? rt_array_at(st, rt_array_count(st) - 1) : NULL; }
static void nav_relayout(id self) {
    struct uinav *n = NAV(self); struct uivc *c = VC(self);
    if (!c->view) return;
    CGRect b = rt_view_bounds(c->view);
    id top = nav_top(self, NULL);
    /* only the top controller's view is installed */
    for (size_t i = 0; i < rt_array_count(n->stack); i++) {
        id vc = rt_array_at(n->stack, i), v = VC(vc)->view;
        if (vc != top && v && V(v)->superview == c->view) rt_view_remove(v);
    }
    if (top) {
        id tv = RT_SEND0(id, top, "view");
        rt_view_set_frame(tv, b); V(tv)->autoresizing = 2 | 16;
        if (V(tv)->superview != c->view) RT_SEND2(void, c->view, "insertSubview:atIndex:", id, tv, long, 0);
    }
    /* bar items mirror the stack */
    id items = mkarray();
    for (size_t i = 0; i < rt_array_count(n->stack); i++) RT_SEND1(void, items, "addObject:", id, RT_SEND0(id, rt_array_at(n->stack, i), "navigationItem"));
    objc_release(BAR(n->bar)->items); BAR(n->bar)->items = items;
    rt_view_set_frame(n->bar, (CGRect){ { 0, 0 }, { b.size.width, 64 } });
    RT_SEND1(void, n->bar, "setHidden:", long, n->barHidden);
    if (V(n->bar)->superview != c->view) rt_view_add_subview(c->view, n->bar);
    rt_view_set_frame(n->toolbar, (CGRect){ { 0, b.size.height - 44 }, { b.size.width, 44 } });
    tb_setItems(n->toolbar, NULL, top ? VC(top)->toolbarItems : NULL);
    RT_SEND1(void, n->toolbar, "setHidden:", long, n->toolbarHidden);
    if (V(n->toolbar)->superview != c->view) rt_view_add_subview(c->view, n->toolbar);
    rt_ui_mark_dirty();
}
static void nav_loadView(id self, SEL s) {
    (void)s; nav_ensure(self);
    id v = rt_view_new(RT_CLASS(UIView), screen_bounds()); V(v)->autoresizing = 2 | 16;
    RT_SEND1(void, v, "setBackgroundColor:", id, rt_color_make(1, 1, 1, 1));
    vc_setView(self, NULL, v); objc_release(v);
    nav_relayout(self);
}
static void nav_push(id self, SEL s, id vc, long animated) {
    (void)s; (void)animated; struct uinav *n = NAV(self); nav_ensure(self);
    if (!vc) return;
    id old = nav_top(self, NULL);
    id d = n->delegate;
    vc_addChild(self, NULL, vc);
    RT_SEND1(void, n->stack, "addObject:", id, vc);
    if (d && rt_responds(d, rt_sel("navigationController:willShowViewController:animated:"))) RT_SEND3(void, d, "navigationController:willShowViewController:animated:", id, self, id, vc, long, animated);
    if (VC(self)->view) {
        if (old && VC(self)->appeared) rt_vc_appear(old, 0);
        nav_relayout(self);
        if (VC(self)->appeared) rt_vc_appear(vc, 1);
    }
    RT_SEND1(void, vc, "didMoveToParentViewController:", id, self);
    if (d && rt_responds(d, rt_sel("navigationController:didShowViewController:animated:"))) RT_SEND3(void, d, "navigationController:didShowViewController:animated:", id, self, id, vc, long, animated);
}
static id nav_pop(id self, SEL s, long animated) {
    (void)s; (void)animated; struct uinav *n = NAV(self);
    if (!n->stack || rt_array_count(n->stack) < 2) return NULL;
    id top = objc_autorelease(objc_retain(nav_top(self, NULL)));
    if (VC(self)->appeared) rt_vc_appear(top, 0);
    RT_SEND0(void, n->stack, "removeLastObject");
    RT_SEND1(void, top, "willMoveToParentViewController:", id, NULL);
    if (VC(top)->view) rt_view_remove(VC(top)->view);
    vc_removeFromParent(top, NULL);
    nav_relayout(self);
    if (VC(self)->appeared) rt_vc_appear(nav_top(self, NULL), 1);
    return top;
}
static id nav_popToRoot(id self, SEL s, long a) {
    id popped = mkarray();
    while (NAV(self)->stack && rt_array_count(NAV(self)->stack) > 1) { id p = nav_pop(self, s, a); if (p) RT_SEND1(void, popped, "addObject:", id, p); }
    return objc_autorelease(popped);
}
static id nav_popTo(id self, SEL s, id vc, long a) {
    id popped = mkarray();
    while (NAV(self)->stack && rt_array_count(NAV(self)->stack) > 1 && nav_top(self, NULL) != vc) { id p = nav_pop(self, s, a); if (p) RT_SEND1(void, popped, "addObject:", id, p); }
    return objc_autorelease(popped);
}
static id nav_initWithRoot(id self, SEL s, id root) { self = nav_init(self, s); if (root) nav_push(self, NULL, root, 0); return self; }
static id nav_vcs(id self, SEL s) { (void)s; nav_ensure(self); return objc_autorelease(RT_SEND0(id, NAV(self)->stack, "copy")); }
static void nav_setVcs(id self, SEL s, id arr, long a) {
    (void)a; nav_ensure(self);
    while (rt_array_count(NAV(self)->stack)) {
        id v = rt_array_at(NAV(self)->stack, 0); objc_retain(v);
        RT_SEND1(void, NAV(self)->stack, "removeObjectAtIndex:", unsigned long, 0);
        if (VC(v)->view) rt_view_remove(VC(v)->view);
        vc_removeFromParent(v, NULL); objc_release(v);
    }
    for (size_t i = 0; arr && i < rt_array_count(arr); i++) nav_push(self, s, rt_array_at(arr, i), 0);
}
static void nav_setVcs1(id self, SEL s, id arr) { nav_setVcs(self, s, arr, 0); }
static id nav_bar(id self, SEL s) { (void)s; nav_ensure(self); return NAV(self)->bar; }
static id nav_toolbar(id self, SEL s) { (void)s; nav_ensure(self); return NAV(self)->toolbar; }
static rt_bool nav_barHidden(id self, SEL s) { (void)s; return NAV(self)->barHidden != 0; }
static void nav_setBarHidden(id self, SEL s, long h) { (void)s; NAV(self)->barHidden = h & 0xff; nav_relayout(self); }
static void nav_setBarHiddenAnim(id self, SEL s, long h, long a) { (void)a; nav_setBarHidden(self, s, h); }
static void nav_setToolbarHidden(id self, SEL s, long h) { (void)s; NAV(self)->toolbarHidden = h & 0xff; nav_relayout(self); }
static void nav_setToolbarHiddenAnim(id self, SEL s, long h, long a) { (void)a; nav_setToolbarHidden(self, s, h); }
static id nav_delegate(id self, SEL s) { (void)s; return NAV(self)->delegate; }
static void nav_setDelegate(id self, SEL s, id d) { (void)s; NAV(self)->delegate = d; }
static void nav_viewWillAppear(id self, SEL s, long a) {
    (void)s; (void)a; id top = nav_top(self, NULL);
    if (top) VC(top)->appeared = 0;   /* rt_vc_appear forwards to the top child via containment */
}

/* ======================================================= UITabBarController */
struct uitab { struct uivc vc; id vcs, bar, delegate; long selected; };
#define TAB(x) ((struct uitab *)(x))
static void tab_relayout(id self) {
    struct uitab *t = TAB(self); struct uivc *c = VC(self);
    if (!c->view) return;
    CGRect b = rt_view_bounds(c->view);
    if (!t->bar) { t->bar = rt_view_new(RT_CLASS(UITabBar), (CGRect){ { 0, b.size.height - 49 }, { b.size.width, 49 } }); BAR(t->bar)->nav = self; V(t->bar)->autoresizing = 2 | 8; }
    id items = mkarray(); id sel = NULL;
    for (size_t i = 0; t->vcs && i < rt_array_count(t->vcs); i++) {
        id vc = rt_array_at(t->vcs, i);
        RT_SEND1(void, items, "addObject:", id, RT_SEND0(id, vc, "tabBarItem"));
        id v = VC(vc)->view;
        if ((long)i == t->selected) sel = vc; else if (v && V(v)->superview == c->view) rt_view_remove(v);
    }
    objc_release(BAR(t->bar)->items); BAR(t->bar)->items = items; BAR(t->bar)->selected = t->selected;
    if (sel) { id v = RT_SEND0(id, sel, "view"); rt_view_set_frame(v, (CGRect){ { 0, 0 }, { b.size.width, b.size.height - 49 } }); V(v)->autoresizing = 2 | 16; if (V(v)->superview != c->view) RT_SEND2(void, c->view, "insertSubview:atIndex:", id, v, long, 0); }
    rt_view_set_frame(t->bar, (CGRect){ { 0, b.size.height - 49 }, { b.size.width, 49 } });
    if (V(t->bar)->superview != c->view) rt_view_add_subview(c->view, t->bar);
    rt_ui_mark_dirty();
}
static void tab_loadView(id self, SEL s) { (void)s; id v = rt_view_new(RT_CLASS(UIView), screen_bounds()); V(v)->autoresizing = 2 | 16; vc_setView(self, NULL, v); objc_release(v); tab_relayout(self); }
static id tab_vcs(id self, SEL s) { (void)s; return TAB(self)->vcs; }
static void tab_setVcs(id self, SEL s, id arr, long a) {
    (void)s; (void)a; struct uitab *t = TAB(self);
    for (size_t i = 0; t->vcs && i < rt_array_count(t->vcs); i++) vc_removeFromParent(rt_array_at(t->vcs, i), NULL);
    id c = arr ? RT_SEND0(id, arr, "copy") : NULL; objc_release(t->vcs); t->vcs = c;
    for (size_t i = 0; c && i < rt_array_count(c); i++) vc_addChild(self, NULL, rt_array_at(c, i));
    t->selected = c && rt_array_count(c) ? 0 : -1;
    tab_relayout(self);
}
static void tab_setVcs1(id self, SEL s, id arr) { tab_setVcs(self, s, arr, 0); }
static unsigned long tab_selected(id self, SEL s) { (void)s; return (unsigned long)TAB(self)->selected; }
static void tab_setSelected(id self, SEL s, unsigned long i) {
    (void)s; struct uitab *t = TAB(self);
    if (!t->vcs || i >= rt_array_count(t->vcs) || (long)i == t->selected) return;
    id old = t->selected >= 0 ? rt_array_at(t->vcs, (size_t)t->selected) : NULL;
    if (old && VC(self)->appeared) rt_vc_appear(old, 0);
    t->selected = (long)i; tab_relayout(self);
    if (VC(self)->appeared) rt_vc_appear(rt_array_at(t->vcs, i), 1);
}
static id tab_selectedVC(id self, SEL s) { (void)s; struct uitab *t = TAB(self); return t->vcs && t->selected >= 0 && (size_t)t->selected < rt_array_count(t->vcs) ? rt_array_at(t->vcs, (size_t)t->selected) : NULL; }
static void tab_setSelectedVC(id self, SEL s, id vc) { struct uitab *t = TAB(self); for (size_t i = 0; t->vcs && i < rt_array_count(t->vcs); i++) if (rt_array_at(t->vcs, i) == vc) tab_setSelected(self, s, i); }
static id tab_bar(id self, SEL s) { (void)s; if (!TAB(self)->bar) RT_SEND0(id, self, "view"); return TAB(self)->bar; }
static id tab_delegate(id self, SEL s) { (void)s; return TAB(self)->delegate; }
static void tab_setDelegate(id self, SEL s, id d) { (void)s; TAB(self)->delegate = d; }
static void tab_touchesEnded(id self, SEL s, id t, id e) {
    (void)s; (void)e; id ctl = BAR(self)->nav; id items = BAR(self)->items; size_t n = items ? rt_array_count(items) : 0;
    if (!n || !ctl) return;
    CGPoint p = RT_SEND1(CGPoint, RT_SEND0(id, t, "anyObject"), "locationInView:", id, self);
    size_t i = (size_t)(p.x / (rt_view_bounds(self).size.width / n)); if (i >= n) i = n - 1;
    id vc = rt_array_at(TAB(ctl)->vcs, i), d = TAB(ctl)->delegate;
    if (d && rt_responds(d, rt_sel("tabBarController:shouldSelectViewController:")) && !(RT_SEND2(long, d, "tabBarController:shouldSelectViewController:", id, ctl, id, vc) & 0xff)) return;
    tab_setSelected(ctl, NULL, i);
    if (d && rt_responds(d, rt_sel("tabBarController:didSelectViewController:"))) RT_SEND2(void, d, "tabBarController:didSelectViewController:", id, ctl, id, vc);
}

/* ================================================================ UITableView */
struct uitable {
    struct uiscroll s; id dataSource, delegate; long style, sepStyle; double rowHeight, headerHeight, footerHeight; id sepColor;
    id header, footer, visible, reuse, registered, selected, bgView; long editing, allowsSelection, allowsMulti, loaded;
    long nsect; long *nrows; double *rowY; double *rowH; long nflat; double *sectY;
};
#define TB(x) ((struct uitable *)(x))
struct uicell { struct uiview v; id reuseId, textLabel, detailLabel, imageView, contentView, accessoryView, bgView, selBgView; long style, accessory, selectionStyle, selected, highlighted, editingStyle; id indexPath; };
#define CELL(x) ((struct uicell *)(x))
static id ip_make(long sec, long row) { return RT_SEND2(id, (id)rt_class_named("NSIndexPath"), "indexPathForRow:inSection:", long, row, long, sec); }
static long ip_section(id ip) { return RT_SEND0(long, ip, "section"); }
static long ip_row(id ip) { return RT_SEND0(long, ip, "row"); }
/* metrics: row y positions per section */
static double section_header_h(id self, long sec) {
    struct uitable *t = TB(self); id d = t->delegate;
    if (d && rt_responds(d, rt_sel("tableView:heightForHeaderInSection:"))) return ((double (*)(id, SEL, id, long))objc_msgSend)(d, rt_sel("tableView:heightForHeaderInSection:"), self, sec);
    id ds = t->dataSource;
    if (ds && rt_responds(ds, rt_sel("tableView:titleForHeaderInSection:")) && RT_SEND2(id, ds, "tableView:titleForHeaderInSection:", id, self, long, sec)) return 28;
    if (d && rt_responds(d, rt_sel("tableView:viewForHeaderInSection:"))) return t->headerHeight > 0 ? t->headerHeight : 28;
    return t->style == 1 ? 18 : 0;
}
static void table_metrics(id self) {
    struct uitable *t = TB(self); id ds = t->dataSource, d = t->delegate;
    free(t->nrows); free(t->rowY); free(t->rowH); free(t->sectY);
    t->nsect = ds && rt_responds(ds, rt_sel("numberOfSectionsInTableView:")) ? RT_SEND1(long, ds, "numberOfSectionsInTableView:", id, self) : 1;
    if (t->nsect < 0) t->nsect = 0;
    t->nrows = calloc((size_t)t->nsect + 1, sizeof(long)); t->sectY = calloc((size_t)t->nsect + 1, sizeof(double));
    long total = 0;
    for (long sct = 0; sct < t->nsect; sct++) { t->nrows[sct] = ds ? RT_SEND2(long, ds, "tableView:numberOfRowsInSection:", id, self, long, sct) : 0; if (t->nrows[sct] < 0) t->nrows[sct] = 0; total += t->nrows[sct]; }
    t->nflat = total; t->rowY = calloc((size_t)total + 1, sizeof(double)); t->rowH = calloc((size_t)total + 1, sizeof(double));
    double y = t->header ? rt_view_frame(t->header).size.height : 0; long k = 0;
    int dyn = d && rt_responds(d, rt_sel("tableView:heightForRowAtIndexPath:"));
    for (long sct = 0; sct < t->nsect; sct++) {
        t->sectY[sct] = y; y += section_header_h(self, sct);
        for (long r = 0; r < t->nrows[sct]; r++, k++) {
            double h = dyn ? ((double (*)(id, SEL, id, id))objc_msgSend)(d, rt_sel("tableView:heightForRowAtIndexPath:"), self, ip_make(sct, r)) : t->rowHeight;
            if (h < 0) h = 44;
            t->rowY[k] = y; t->rowH[k] = h; y += h;
        }
        if (d && rt_responds(d, rt_sel("tableView:heightForFooterInSection:"))) y += ((double (*)(id, SEL, id, long))objc_msgSend)(d, rt_sel("tableView:heightForFooterInSection:"), self, sct);
    }
    t->sectY[t->nsect] = y;
    if (t->footer) { CGRect f = rt_view_frame(t->footer); f.origin.y = y; f.size.width = rt_view_bounds(self).size.width; rt_view_set_frame(t->footer, f); if (V(t->footer)->superview != self) rt_view_add_subview(self, t->footer); y += f.size.height; }
    if (t->header) { CGRect f = rt_view_frame(t->header); f.origin = (CGPoint){ 0, 0 }; f.size.width = rt_view_bounds(self).size.width; rt_view_set_frame(t->header, f); if (V(t->header)->superview != self) rt_view_add_subview(self, t->header); }
    t->s.contentSize = (CGSize){ rt_view_bounds(self).size.width, y };
    t->loaded = 1;
}
static long flat_index(id self, long sec, long row) { struct uitable *t = TB(self); long k = 0; for (long s2 = 0; s2 < sec && s2 < t->nsect; s2++) k += t->nrows[s2]; return k + row; }
static id tv_initWithFrameStyle(id self, SEL s, CGRect fr, long style) {
    (void)s; self = supercall_frame(self, RT_CLASS(UITableView), fr);
    struct uitable *t = TB(self);
    t->style = style; t->rowHeight = 44; t->sepStyle = 1; t->visible = mkarray(); t->allowsSelection = 1;
    t->reuse = RT_SEND0(id, RT_SEND0(id, (id)rt_class_named("NSMutableDictionary"), "alloc"), "init");
    t->registered = RT_SEND0(id, RT_SEND0(id, (id)rt_class_named("NSMutableDictionary"), "alloc"), "init");
    t->s.alwaysBounceV = 1;
    RT_SEND1(void, self, "setBackgroundColor:", id, style == 1 ? rt_color_make(0.937, 0.937, 0.957, 1) : rt_color_make(1, 1, 1, 1));
    return self;
}
static id tv_initWithFrame2(id self, SEL s, CGRect fr) { return tv_initWithFrameStyle(self, s, fr, 0); }
#define TBID(get, set, field) static id get(id self, SEL s) { (void)s; return TB(self)->field; } static void set(id self, SEL s, id v) { (void)s; TB(self)->field = v; RT_SEND0(void, self, "setNeedsLayout"); }
TBID(tv_dataSource, tv_setDataSource, dataSource)
static id tv_delegate2(id self, SEL s) { (void)s; return TB(self)->delegate; }
static void tv_setDelegate2(id self, SEL s, id v) { (void)s; TB(self)->delegate = v; ((struct uiscroll *)self)->delegate = v; RT_SEND0(void, self, "setNeedsLayout"); }
static double tv_rowHeight(id self, SEL s) { (void)s; return TB(self)->rowHeight; }
static void tv_setRowHeight(id self, SEL s, double h) { (void)s; TB(self)->rowHeight = h; }
static void tv_setSepStyle(id self, SEL s, long v) { (void)s; TB(self)->sepStyle = v; rt_ui_mark_dirty(); }
static long tv_sepStyle(id self, SEL s) { (void)s; return TB(self)->sepStyle; }
static void tv_setSepColor(id self, SEL s, id v) { (void)s; objc_retain(v); objc_release(TB(self)->sepColor); TB(self)->sepColor = v; }
static id tv_header(id self, SEL s) { (void)s; return TB(self)->header; }
static void tv_setHeader(id self, SEL s, id v) { (void)s; if (TB(self)->header) rt_view_remove(TB(self)->header); objc_retain(v); objc_release(TB(self)->header); TB(self)->header = v; TB(self)->loaded = 0; RT_SEND0(void, self, "setNeedsLayout"); }
static id tv_footer(id self, SEL s) { (void)s; return TB(self)->footer; }
static void tv_setFooter(id self, SEL s, id v) { (void)s; if (TB(self)->footer) rt_view_remove(TB(self)->footer); objc_retain(v); objc_release(TB(self)->footer); TB(self)->footer = v; TB(self)->loaded = 0; RT_SEND0(void, self, "setNeedsLayout"); }
static void tv_setBgView(id self, SEL s, id v) { (void)s; objc_retain(v); objc_release(TB(self)->bgView); TB(self)->bgView = v; }
static void enqueue_cell(id self, id cell) {
    struct uitable *t = TB(self); id rid = CELL(cell)->reuseId;
    rt_view_remove(cell);
    if (!rid) return;
    id q = rt_dict_get(t->reuse, rid);
    if (!q) { q = mkarray(); RT_SEND2(void, t->reuse, "setObject:forKey:", id, q, id, rid); objc_release(q); }
    RT_SEND1(void, q, "addObject:", id, cell);
}
static id tv_dequeue(id self, SEL s, id rid) {
    (void)s; struct uitable *t = TB(self);
    id q = rid ? rt_dict_get(t->reuse, rid) : NULL;
    if (q && rt_array_count(q)) {
        id c = objc_autorelease(objc_retain(rt_array_at(q, rt_array_count(q) - 1)));
        RT_SEND0(void, q, "removeLastObject");
        RT_SEND0(void, c, "prepareForReuse");
        return c;
    }
    id reg = rid ? rt_dict_get(t->registered, rid) : NULL;
    if (reg) {
        if (rt_is_kind_of(reg, (Class)rt_class_named("UINib"))) {
            id objs = RT_SEND2(id, reg, "instantiateWithOwner:options:", id, NULL, id, NULL);
            for (size_t i = 0; objs && i < rt_array_count(objs); i++) if (rt_is_kind_of(rt_array_at(objs, i), RT_CLASS(UITableViewCell))) { id c = rt_array_at(objs, i); if (!CELL(c)->reuseId) CELL(c)->reuseId = objc_retain(rid); return c; }
        } else return objc_autorelease(RT_SEND2(id, RT_SEND0(id, reg, "alloc"), "initWithStyle:reuseIdentifier:", long, 0, id, rid));
    }
    return NULL;
}
static id tv_dequeueIP(id self, SEL s, id rid, id ip) { (void)ip; id c = tv_dequeue(self, s, rid); if (!c) c = objc_autorelease(RT_SEND2(id, RT_SEND0(id, (id)RT_CLASS(UITableViewCell), "alloc"), "initWithStyle:reuseIdentifier:", long, 0, id, rid)); return c; }
static void tv_registerClass(id self, SEL s, id cls, id rid) { (void)s; if (cls && rid) RT_SEND2(void, TB(self)->registered, "setObject:forKey:", id, cls, id, rid); }
static void tv_registerNib(id self, SEL s, id nib, id rid) { tv_registerClass(self, s, nib, rid); }
static void tv_reloadData(id self, SEL s) {
    (void)s; struct uitable *t = TB(self);
    while (rt_array_count(t->visible)) { id c = objc_retain(rt_array_at(t->visible, 0)); RT_SEND1(void, t->visible, "removeObjectAtIndex:", unsigned long, 0); enqueue_cell(self, c); objc_release(c); }
    t->loaded = 0; RT_SEND0(void, self, "setNeedsLayout"); rt_ui_mark_dirty();
}
static void tv_layoutSubviews(id self, SEL s) {
    (void)s; struct uitable *t = TB(self);
    if (!t->loaded) table_metrics(self);
    CGRect b = rt_view_bounds(self);
    double y0 = b.origin.y, y1 = b.origin.y + b.size.height;
    /* drop cells that scrolled out */
    for (size_t i = rt_array_count(t->visible); i-- > 0;) {
        id c = rt_array_at(t->visible, i); CGRect f = rt_view_frame(c);
        if (f.origin.y + f.size.height < y0 || f.origin.y > y1) { objc_retain(c); RT_SEND1(void, t->visible, "removeObjectAtIndex:", unsigned long, (unsigned long)i); enqueue_cell(self, c); objc_release(c); }
    }
    long k = 0;
    for (long sec = 0; sec < t->nsect; sec++)
        for (long r = 0; r < t->nrows[sec]; r++, k++) {
            if (t->rowY[k] + t->rowH[k] < y0 || t->rowY[k] > y1) continue;
            int have = 0;
            for (size_t i = 0; i < rt_array_count(t->visible); i++) { id c = rt_array_at(t->visible, i); id ip = CELL(c)->indexPath; if (ip && ip_section(ip) == sec && ip_row(ip) == r) { have = 1; break; } }
            if (have) continue;
            id ip = ip_make(sec, r);
            id cell = RT_SEND2(id, t->dataSource, "tableView:cellForRowAtIndexPath:", id, self, id, ip);
            if (!cell) continue;
            objc_retain(ip); objc_release(CELL(cell)->indexPath); CELL(cell)->indexPath = ip;
            rt_view_set_frame(cell, (CGRect){ { 0, t->rowY[k] }, { b.size.width, t->rowH[k] } });
            id d = t->delegate;
            if (d && rt_responds(d, rt_sel("tableView:willDisplayCell:forRowAtIndexPath:"))) RT_SEND3(void, d, "tableView:willDisplayCell:forRowAtIndexPath:", id, self, id, cell, id, ip);
            if (t->selected && RT_SEND1(long, t->selected, "isEqual:", id, ip)) CELL(cell)->selected = 1;
            RT_SEND2(void, self, "insertSubview:atIndex:", id, cell, long, 0);
            RT_SEND1(void, t->visible, "addObject:", id, cell);
        }
}
static id tv_cellForRow(id self, SEL s, id ip) {
    (void)s; struct uitable *t = TB(self);
    for (size_t i = 0; i < rt_array_count(t->visible); i++) { id c = rt_array_at(t->visible, i); id x = CELL(c)->indexPath; if (x && ip_section(x) == ip_section(ip) && ip_row(x) == ip_row(ip)) return c; }
    return NULL;
}
static id tv_visibleCells(id self, SEL s) { (void)s; return objc_autorelease(RT_SEND0(id, TB(self)->visible, "copy")); }
static id tv_visibleIPs(id self, SEL s) { (void)s; id out = mkarray(); for (size_t i = 0; i < rt_array_count(TB(self)->visible); i++) { id ip = CELL(rt_array_at(TB(self)->visible, i))->indexPath; if (ip) RT_SEND1(void, out, "addObject:", id, ip); } return objc_autorelease(out); }
static id tv_ipForCell(id self, SEL s, id cell) { (void)self; (void)s; return cell ? CELL(cell)->indexPath : NULL; }
static id tv_ipForPoint(id self, SEL s, CGPoint p) {
    (void)s; struct uitable *t = TB(self); if (!t->loaded) table_metrics(self);
    long k = 0;
    for (long sec = 0; sec < t->nsect; sec++) for (long r = 0; r < t->nrows[sec]; r++, k++) if (p.y >= t->rowY[k] && p.y < t->rowY[k] + t->rowH[k]) return ip_make(sec, r);
    return NULL;
}
static CGRect tv_rectForRow(id self, SEL s, id ip) {
    (void)s; struct uitable *t = TB(self); if (!t->loaded) table_metrics(self);
    long k = flat_index(self, ip_section(ip), ip_row(ip));
    if (k < 0 || k >= t->nflat) return (CGRect){ { 0, 0 }, { 0, 0 } };
    return (CGRect){ { 0, t->rowY[k] }, { rt_view_bounds(self).size.width, t->rowH[k] } };
}
static long tv_numSections(id self, SEL s) { (void)s; if (!TB(self)->loaded) table_metrics(self); return TB(self)->nsect; }
static long tv_numRows(id self, SEL s, long sec) { (void)s; if (!TB(self)->loaded) table_metrics(self); return sec >= 0 && sec < TB(self)->nsect ? TB(self)->nrows[sec] : 0; }
static void tv_scrollToRow(id self, SEL s, id ip, long pos, long a) {
    (void)a; CGRect r = tv_rectForRow(self, s, ip), b = rt_view_bounds(self);
    double y = pos == 2 ? r.origin.y + r.size.height / 2 - b.size.height / 2 : pos == 3 ? r.origin.y + r.size.height - b.size.height : r.origin.y;
    double maxy = fmax(0, TB(self)->s.contentSize.height - b.size.height);
    RT_SEND1(void, self, "setContentOffset:", CGPoint, ((CGPoint){ b.origin.x, fmax(0, fmin(y, maxy)) }));
}
static void tv_select(id self, SEL s, id ip, long a, long pos) {
    (void)s; (void)a; (void)pos; struct uitable *t = TB(self);
    if (t->selected) { id c = tv_cellForRow(self, NULL, t->selected); if (c) RT_SEND1(void, c, "setSelected:", long, 0); }
    objc_retain(ip); objc_release(t->selected); t->selected = ip;
    id c = ip ? tv_cellForRow(self, NULL, ip) : NULL; if (c) RT_SEND1(void, c, "setSelected:", long, 1);
}
static void tv_deselect(id self, SEL s, id ip, long a) {
    (void)s; (void)a; struct uitable *t = TB(self);
    id c = ip ? tv_cellForRow(self, NULL, ip) : NULL; if (c) RT_SEND1(void, c, "setSelected:", long, 0);
    if (t->selected && ip && RT_SEND1(long, t->selected, "isEqual:", id, ip)) { objc_release(t->selected); t->selected = NULL; }
}
static id tv_selectedIP(id self, SEL s) { (void)s; return TB(self)->selected; }
static void tv_updates(id self, SEL s, id ips, long anim) { (void)ips; (void)anim; tv_reloadData(self, s); }
static void tv_beginUpdates(id self, SEL s) { (void)self; (void)s; }
static void tv_endUpdates(id self, SEL s) { tv_reloadData(self, s); }
static void tv_setEditing(id self, SEL s, long e, long a) { (void)s; (void)a; TB(self)->editing = e & 0xff; }
static void tv_setEditing1(id self, SEL s, long e) { tv_setEditing(self, s, e, 0); }
static rt_bool tv_isEditing(id self, SEL s) { (void)s; return TB(self)->editing != 0; }
static void tv_setAllowsSelection(id self, SEL s, long v) { (void)s; TB(self)->allowsSelection = v & 0xff; }
static void tv_touchesEnded(id self, SEL s, id touches, id ev) {
    (void)s; (void)ev; struct uitable *t = TB(self);
    if (!t->allowsSelection || t->s.dragging) return;
    CGPoint p = RT_SEND1(CGPoint, RT_SEND0(id, touches, "anyObject"), "locationInView:", id, self);
    id ip = tv_ipForPoint(self, NULL, p);
    if (!ip) return;
    id d = t->delegate;
    if (d && rt_responds(d, rt_sel("tableView:willSelectRowAtIndexPath:"))) { ip = RT_SEND2(id, d, "tableView:willSelectRowAtIndexPath:", id, self, id, ip); if (!ip) return; }
    tv_select(self, NULL, ip, 0, 0);
    if (d && rt_responds(d, rt_sel("tableView:didSelectRowAtIndexPath:"))) RT_SEND2(void, d, "tableView:didSelectRowAtIndexPath:", id, self, id, ip);
}
static void draw_table(id view, struct cgctx *c, CGRect b) {
    struct uitable *t = TB(view);
    if (!t->loaded) return;
    id ds = t->dataSource; (void)b;
    for (long sec = 0; sec < t->nsect; sec++) {
        if (ds && rt_responds(ds, rt_sel("tableView:titleForHeaderInSection:"))) {
            id title = RT_SEND2(id, ds, "tableView:titleForHeaderInSection:", id, view, long, sec);
            if (title) {
                float col[4] = { 0.43f, 0.43f, 0.45f, 1 };
                rt_text_draw(c, rt_str_utf8(title), rt_font_make(13, 0, NULL), col, (CGRect){ { 15, t->sectY[sec] }, { rt_view_bounds(view).size.width - 30, 28 } }, 0, 1, 1);
            }
        }
    }
    if (t->sepStyle) {
        iosrt_CGContextSetFillColorWithColor(c, t->sepColor ? t->sepColor : rt_color_make(0.78, 0.78, 0.8, 1));
        for (long k = 0; k < t->nflat; k++) iosrt_CGContextFillRect(c, (CGRect){ { 15, t->rowY[k] + t->rowH[k] - 0.5 }, { rt_view_bounds(view).size.width - 15, 0.5 } });
    }
}
static id cell_initStyle(id self, SEL s, long style, id rid) {
    (void)s; self = ((id (*)(id, SEL, CGRect))objc_msgSend)(self, rt_sel("initWithFrame:"), (CGRect){ { 0, 0 }, { rt_scr_w, 44 } });
    struct uicell *x = CELL(self); x->style = style; x->reuseId = rid ? RT_SEND0(id, rid, "copy") : NULL; x->selectionStyle = 3;
    x->contentView = rt_view_new(RT_CLASS(UIView), (CGRect){ { 0, 0 }, { rt_scr_w, 44 } }); V(x->contentView)->autoresizing = 2 | 16;
    rt_view_add_subview(self, x->contentView);
    RT_SEND1(void, self, "setBackgroundColor:", id, rt_color_make(1, 1, 1, 1));
    return self;
}
static id cell_initWithFrame(id self, SEL s, CGRect fr) {
    (void)s; self = supercall_frame(self, RT_CLASS(UITableViewCell), fr);
    struct uicell *x = CELL(self);
    if (!x->contentView) { x->contentView = rt_view_new(RT_CLASS(UIView), (CGRect){ { 0, 0 }, fr.size }); V(x->contentView)->autoresizing = 2 | 16; rt_view_add_subview(self, x->contentView); }
    return self;
}
static id cell_initWithCoder(id self, SEL s, id coder) {
    struct { id r; Class c; } sup = { self, RT_CLASS(UITableViewCell) };
    self = ((id (*)(void *, SEL, id))objc_msgSendSuper2)(&sup, s, coder);
    struct uicell *x = CELL(self);
    x->reuseId = objc_retain(RT_SEND1(id, coder, "decodeObjectForKey:", id, rt_str("UIReuseIdentifier")));
    id cv = RT_SEND1(id, coder, "decodeObjectForKey:", id, rt_str("UIContentView"));
    if (cv) x->contentView = objc_retain(cv);
    x->selectionStyle = RT_SEND1(long, coder, "containsValueForKey:", id, rt_str("UISelectionStyle")) & 0xff ? RT_SEND1(long, coder, "decodeIntegerForKey:", id, rt_str("UISelectionStyle")) : 3;
    return self;
}
static id cell_label(id self, int which) {
    struct uicell *x = CELL(self); id *slot = which ? &x->detailLabel : &x->textLabel;
    if (!*slot) {
        *slot = rt_view_new(RT_CLASS(UILabel), (CGRect){ { 0, 0 }, { 0, 0 } });
        if (which) { RT_SEND1(void, *slot, "setFont:", id, rt_font_make(x->style == 3 ? 12 : 17, 0, NULL)); RT_SEND1(void, *slot, "setTextColor:", id, rt_color_make(0.56, 0.56, 0.58, 1)); }
        RT_SEND1(void, *slot, "setBackgroundColor:", id, rt_color_make(0, 0, 0, 0));
        rt_view_add_subview(x->contentView, *slot);
        RT_SEND0(void, self, "setNeedsLayout");
    }
    return *slot;
}
static id cell_textLabel(id self, SEL s) { (void)s; return cell_label(self, 0); }
static id cell_detailLabel(id self, SEL s) { (void)s; return CELL(self)->style ? cell_label(self, 1) : NULL; }
static id cell_imageView(id self, SEL s) { (void)s; struct uicell *x = CELL(self); if (!x->imageView) { x->imageView = rt_view_new(RT_CLASS(UIImageView), (CGRect){ { 0, 0 }, { 0, 0 } }); rt_view_add_subview(x->contentView, x->imageView); } return x->imageView; }
static id cell_contentView(id self, SEL s) { (void)s; return CELL(self)->contentView; }
static id cell_reuseId(id self, SEL s) { (void)s; return CELL(self)->reuseId; }
static void cell_layoutSubviews(id self, SEL s) {
    (void)s; struct uicell *x = CELL(self); CGRect b = rt_view_bounds(self);
    double right = b.size.width - (x->accessory || x->accessoryView ? 33 : 0);
    if (x->accessoryView) { CGRect f = rt_view_frame(x->accessoryView); f.origin.x = b.size.width - f.size.width - 15; f.origin.y = (b.size.height - f.size.height) / 2; rt_view_set_frame(x->accessoryView, f); if (V(x->accessoryView)->superview != self) rt_view_add_subview(self, x->accessoryView); right = f.origin.x - 8; }
    rt_view_set_frame(x->contentView, (CGRect){ { 0, 0 }, { right, b.size.height } });
    double xx = 15;
    if (x->imageView && ((struct uiimageview *)x->imageView)->image) { CGSize is = rt_uiimage_size(((struct uiimageview *)x->imageView)->image); rt_view_set_frame(x->imageView, (CGRect){ { 15, (b.size.height - is.height) / 2 }, is }); xx = 15 + is.width + 15; }
    if (x->textLabel) {
        if (x->style == 3 && x->detailLabel) { rt_view_set_frame(x->textLabel, (CGRect){ { xx, 4 }, { right - xx - 15, b.size.height / 2 } }); rt_view_set_frame(x->detailLabel, (CGRect){ { xx, b.size.height / 2 }, { right - xx - 15, b.size.height / 2 - 4 } }); }
        else rt_view_set_frame(x->textLabel, (CGRect){ { xx, 0 }, { right - xx - 15, b.size.height } });
    }
    if (x->detailLabel && x->style != 3) { rt_view_set_frame(x->detailLabel, (CGRect){ { xx, 0 }, { right - xx - 15, b.size.height } }); RT_SEND1(void, x->detailLabel, "setTextAlignment:", long, x->style == 1 ? 2 : 0); }
}
static void cell_setSelected(id self, SEL s, long v) { (void)s; CELL(self)->selected = v & 0xff; rt_ui_mark_dirty(); }
static void cell_setSelectedAnim(id self, SEL s, long v, long a) { (void)a; RT_SEND1(void, self, "setSelected:", long, v); (void)s; }
static rt_bool cell_selected(id self, SEL s) { (void)s; return CELL(self)->selected != 0; }
static void cell_setHighlighted(id self, SEL s, long v) { (void)s; CELL(self)->highlighted = v & 0xff; rt_ui_mark_dirty(); }
static void cell_setHighlightedAnim(id self, SEL s, long v, long a) { (void)a; cell_setHighlighted(self, s, v); }
static rt_bool cell_highlighted(id self, SEL s) { (void)s; return CELL(self)->highlighted != 0; }
static long cell_accessory(id self, SEL s) { (void)s; return CELL(self)->accessory; }
static void cell_setAccessory(id self, SEL s, long v) { (void)s; CELL(self)->accessory = v; RT_SEND0(void, self, "setNeedsLayout"); rt_ui_mark_dirty(); }
static id cell_accessoryView(id self, SEL s) { (void)s; return CELL(self)->accessoryView; }
static void cell_setAccessoryView(id self, SEL s, id v) { (void)s; struct uicell *x = CELL(self); if (x->accessoryView) rt_view_remove(x->accessoryView); objc_retain(v); objc_release(x->accessoryView); x->accessoryView = v; RT_SEND0(void, self, "setNeedsLayout"); }
static void cell_setSelectionStyle(id self, SEL s, long v) { (void)s; CELL(self)->selectionStyle = v; }
static long cell_selectionStyle(id self, SEL s) { (void)s; return CELL(self)->selectionStyle; }
static void cell_setBgView(id self, SEL s, id v) { (void)s; struct uicell *x = CELL(self); if (x->bgView) rt_view_remove(x->bgView); objc_retain(v); objc_release(x->bgView); x->bgView = v; if (v) { rt_view_set_frame(v, rt_view_bounds(self)); V(v)->autoresizing = 2 | 16; RT_SEND2(void, self, "insertSubview:atIndex:", id, v, long, 0); } }
static id cell_bgView(id self, SEL s) { (void)s; return CELL(self)->bgView; }
static void cell_setSelBgView(id self, SEL s, id v) { (void)s; objc_retain(v); objc_release(CELL(self)->selBgView); CELL(self)->selBgView = v; }
static void draw_cell(id view, struct cgctx *c, CGRect b) {
    struct uicell *x = CELL(view);
    if ((x->selected || x->highlighted) && x->selectionStyle) {
        if (x->selBgView) { id sb = x->selBgView; if (V(sb)->bg) { iosrt_CGContextSetFillColorWithColor(c, V(sb)->bg); iosrt_CGContextFillRect(c, b); } }
        else { iosrt_CGContextSetFillColorWithColor(c, rt_color_make(0.85, 0.85, 0.85, 1)); iosrt_CGContextFillRect(c, b); }
    }
    if (x->accessory && !x->accessoryView) {
        float col[4]; rt_color_rgba(x->accessory == 3 ? rt_view_tint(view) : rt_color_make(0.78, 0.78, 0.8, 1), col);
        const char *g = x->accessory == 3 ? "\xe2\x9c\x93" : "\xe2\x80\xba";
        rt_text_draw(c, g, rt_font_make(x->accessory == 3 ? 17 : 24, 0, NULL), col, (CGRect){ { b.origin.x + b.size.width - 33, b.origin.y }, { 20, b.size.height } }, 1, 1, 1);
    }
}

/* ========================================================== alerts & sheets */
struct uialert { Class isa; id title, message, delegate, buttons, window, fields; long cancel, firstOther, destructive, style, tag, visible, sheet; id controller; };
#define AL(x) ((struct uialert *)(x))
static id alert_init_common(id self, id title, id msg, id delegate) {
    struct uialert *a = AL(self);
    a->title = title ? RT_SEND0(id, title, "copy") : NULL; a->message = msg ? RT_SEND0(id, msg, "copy") : NULL; a->delegate = delegate;
    a->buttons = mkarray(); a->cancel = -1; a->firstOther = -1; a->destructive = -1; a->fields = mkarray();
    return self;
}
static long alert_add(id self, id title) { RT_SEND1(void, AL(self)->buttons, "addObject:", id, title ? title : rt_str("")); return (long)rt_array_count(AL(self)->buttons) - 1; }
static void add_varargs(id self, id first, const uint64_t *rest) {
    if (!first) return;
    long i = alert_add(self, first); if (AL(self)->firstOther < 0) AL(self)->firstOther = i;
    for (int k = 0; rest && rest[k] && k < 32; k++) alert_add(self, (id)rest[k]);
}
id rt_UIAlertView_init(id self, SEL s, id title, id msg, id delegate, id cancel, id first, const uint64_t *rest) {
    (void)s; alert_init_common(self, title, msg, delegate);
    if (cancel) AL(self)->cancel = alert_add(self, cancel);
    add_varargs(self, first, rest);
    return self;
}
id rt_UIActionSheet_init(id self, SEL s, id title, id delegate, id cancel, id destructive, id first, const uint64_t *rest) {
    (void)s; alert_init_common(self, title, NULL, delegate); AL(self)->sheet = 1;
    if (destructive) AL(self)->destructive = alert_add(self, destructive);
    add_varargs(self, first, rest);
    if (cancel) AL(self)->cancel = alert_add(self, cancel);
    return self;
}
static id al_init(id self, SEL s) { (void)s; return alert_init_common(self, NULL, NULL, NULL); }
static long al_addButton(id self, SEL s, id t) { (void)s; long i = alert_add(self, t); if (AL(self)->firstOther < 0 && i != AL(self)->cancel) AL(self)->firstOther = i; return i; }
static id al_buttonTitle(id self, SEL s, long i) { (void)s; return i >= 0 && (size_t)i < rt_array_count(AL(self)->buttons) ? rt_array_at(AL(self)->buttons, (size_t)i) : NULL; }
static long al_numButtons(id self, SEL s) { (void)s; return (long)rt_array_count(AL(self)->buttons); }
static long al_cancelIndex(id self, SEL s) { (void)s; return AL(self)->cancel; }
static void al_setCancelIndex(id self, SEL s, long i) { (void)s; AL(self)->cancel = i; }
static long al_firstOther(id self, SEL s) { (void)s; return AL(self)->firstOther; }
static long al_destructive(id self, SEL s) { (void)s; return AL(self)->destructive; }
static void al_setDestructive(id self, SEL s, long i) { (void)s; AL(self)->destructive = i; }
#define ALID(get, set, field) static id get(id self, SEL s) { (void)s; return AL(self)->field; } static void set(id self, SEL s, id v) { (void)s; objc_retain(v); objc_release(AL(self)->field); AL(self)->field = v; }
ALID(al_title, al_setTitle, title) ALID(al_message, al_setMessage, message)
static id al_delegate(id self, SEL s) { (void)s; return AL(self)->delegate; }
static void al_setDelegate(id self, SEL s, id d) { (void)s; AL(self)->delegate = d; }
static long al_style(id self, SEL s) { (void)s; return AL(self)->style; }
static void al_setStyle(id self, SEL s, long v) {
    (void)s; AL(self)->style = v; RT_SEND0(void, AL(self)->fields, "removeAllObjects");
    int n = v == 3 ? 2 : v ? 1 : 0;
    for (int i = 0; i < n; i++) { id tf = rt_view_new(RT_CLASS(UITextField), (CGRect){ { 0, 0 }, { 240, 28 } }); RT_SEND1(void, tf, "setBorderStyle:", long, 3); if (v == 1 || (v == 3 && i == 1)) RT_SEND1(void, tf, "setSecureTextEntry:", long, 1); RT_SEND1(void, AL(self)->fields, "addObject:", id, tf); objc_release(tf); }
}
static id al_textField(id self, SEL s, long i) { (void)s; return i >= 0 && (size_t)i < rt_array_count(AL(self)->fields) ? rt_array_at(AL(self)->fields, (size_t)i) : NULL; }
static long al_tag(id self, SEL s) { (void)s; return AL(self)->tag; }
static void al_setTag(id self, SEL s, long v) { (void)s; AL(self)->tag = v; }
static rt_bool al_visible(id self, SEL s) { (void)s; return AL(self)->visible != 0; }
static void al_dealloc(id self, SEL s) {
    (void)s; struct uialert *a = AL(self);
    objc_release(a->title); objc_release(a->message); objc_release(a->buttons); objc_release(a->window); objc_release(a->fields);
    rt_dealloc_instance(self);
}
/* overlay UI shared by alert views, action sheets and alert controllers */
static id build_overlay(id target, id title, id msg, id buttons, long cancel, long destructive, int sheet, id fields) {
    CGRect sb = { { 0, 0 }, { rt_scr_w, rt_scr_h } };
    id win = ((id (*)(id, SEL, CGRect))objc_msgSend)(rt_alloc_instance(RT_CLASS(UIWindow), 0), rt_sel("initWithFrame:"), sb);
    RT_SEND1(void, win, "setWindowLevel:", double, 2000);
    RT_SEND1(void, win, "setBackgroundColor:", id, rt_color_make(0, 0, 0, 0.4));
    size_t nb = buttons ? rt_array_count(buttons) : 0, nf = fields ? rt_array_count(fields) : 0;
    double w = sheet ? rt_scr_w - 16 : 270, pad = 16, y = pad;
    id titleF = rt_font_make(17, 1, NULL), msgF = rt_font_make(13, 0, NULL);
    CGSize ts = title ? rt_text_measure(rt_str_utf8(title), titleF, w - 2 * pad, 0) : (CGSize){ 0, 0 };
    CGSize ms = msg ? rt_text_measure(rt_str_utf8(msg), msgF, w - 2 * pad, 0) : (CGSize){ 0, 0 };
    double bh = 44, h = pad + ts.height + (msg ? 4 + ms.height : 0) + pad + nf * 34 + (sheet || nb != 2 ? nb * bh : bh);
    if (!title && !msg) h -= pad * 2;
    double bx = (rt_scr_w - w) / 2, by = sheet ? rt_scr_h - h - 8 : (rt_scr_h - h) / 2;
    id box = rt_view_new(RT_CLASS(UIView), (CGRect){ { bx, by }, { w, h } });
    RT_SEND1(void, box, "setBackgroundColor:", id, rt_color_make(0.97, 0.97, 0.97, 1));
    RT_SEND1(void, RT_SEND0(id, box, "layer"), "setCornerRadius:", double, sheet ? 13 : 13);
    RT_SEND1(void, box, "setClipsToBounds:", long, 1);
    rt_view_add_subview(win, box); objc_release(box);
    if (title) {
        id l = rt_view_new(RT_CLASS(UILabel), (CGRect){ { pad, y }, { w - 2 * pad, ts.height } });
        RT_SEND1(void, l, "setText:", id, title); RT_SEND1(void, l, "setFont:", id, titleF); RT_SEND1(void, l, "setTextAlignment:", long, 1); RT_SEND1(void, l, "setNumberOfLines:", long, 0);
        rt_view_add_subview(box, l); objc_release(l); y += ts.height;
    }
    if (msg) {
        y += 4;
        id l = rt_view_new(RT_CLASS(UILabel), (CGRect){ { pad, y }, { w - 2 * pad, ms.height } });
        RT_SEND1(void, l, "setText:", id, msg); RT_SEND1(void, l, "setFont:", id, msgF); RT_SEND1(void, l, "setTextAlignment:", long, 1); RT_SEND1(void, l, "setNumberOfLines:", long, 0);
        rt_view_add_subview(box, l); objc_release(l); y += ms.height;
    }
    if (title || msg) y += pad;
    for (size_t i = 0; i < nf; i++) { id tf = rt_array_at(fields, i); rt_view_set_frame(tf, (CGRect){ { pad, y }, { w - 2 * pad, 28 } }); rt_view_add_subview(box, tf); y += 34; }
    int side = !sheet && nb == 2;
    for (size_t i = 0; i < nb; i++) {
        CGRect r = side ? (CGRect){ { i * w / 2, y }, { w / 2, bh } } : (CGRect){ { 0, y + i * bh }, { w, bh } };
        id sep = rt_view_new(RT_CLASS(UIView), (CGRect){ { r.origin.x, r.origin.y }, { side ? w / 2 : w, 0.5 } });
        RT_SEND1(void, sep, "setBackgroundColor:", id, rt_color_make(0.8, 0.8, 0.8, 1)); rt_view_add_subview(box, sep); objc_release(sep);
        id b = RT_SEND1(id, (id)RT_CLASS(UIButton), "buttonWithType:", long, 1);
        rt_view_set_frame(b, r);
        RT_SEND2(void, b, "setTitle:forState:", id, rt_array_at(buttons, i), unsigned long, 0);
        if ((long)i == destructive) RT_SEND2(void, b, "setTitleColor:forState:", id, rt_color_make(1, 0.23, 0.19, 1), unsigned long, 0);
        if ((long)i == cancel) RT_SEND1(void, b, "setFont:", id, rt_font_make(17, 1, NULL)); else RT_SEND1(void, b, "setFont:", id, rt_font_make(17, 0, NULL));
        RT_SEND1(void, b, "setTag:", long, (long)i);
        RT_SEND3(void, b, "addTarget:action:forControlEvents:", id, target, SEL, rt_sel("iosrt_alertButton:"), unsigned long, 64);
        rt_view_add_subview(box, b);
    }
    RT_SEND0(void, win, "makeKeyAndVisible");
    return win;
}
static id g_prev_key;
static void al_show(id self, SEL s) {
    (void)s; struct uialert *a = AL(self);
    if (a->visible) return;
    id d = a->delegate;
    if (d && rt_responds(d, rt_sel(a->sheet ? "willPresentActionSheet:" : "willPresentAlertView:"))) RT_SEND1(void, d, a->sheet ? "willPresentActionSheet:" : "willPresentAlertView:", id, self);
    g_prev_key = rt_app_key_window();
    objc_retain(self);   /* a shown alert keeps itself alive until dismissed */
    a->window = build_overlay(self, a->title, a->message, a->buttons, a->cancel, a->destructive, (int)a->sheet, a->fields);
    a->visible = 1;
    if (d && rt_responds(d, rt_sel(a->sheet ? "didPresentActionSheet:" : "didPresentAlertView:"))) RT_SEND1(void, d, a->sheet ? "didPresentActionSheet:" : "didPresentAlertView:", id, self);
}
static void al_showInView(id self, SEL s, id v) { (void)v; al_show(self, s); }
static void al_showFrom(id self, SEL s, CGRect r, id v, long a) { (void)r; (void)v; (void)a; al_show(self, s); }
static void al_showFromItem(id self, SEL s, id item, long a) { (void)item; (void)a; al_show(self, s); }
static void close_overlay(id *win) {
    if (!*win) return;
    RT_SEND1(void, *win, "setHidden:", long, 1);
    ((struct uiwindow *)*win)->hiddenWin = 1;
    id app = RT_SEND0(id, (id)rt_class_named("UIApplication"), "sharedApplication");
    RT_SEND1(void, RT_SEND0(id, app, "windows"), "removeObjectIdenticalTo:", id, *win);
    if (g_prev_key) RT_SEND0(void, g_prev_key, "makeKeyWindow");
    objc_release(*win); *win = NULL; rt_ui_mark_dirty();
}
static void al_dismissWith(id self, SEL s, long idx, long anim) {
    (void)s; (void)anim; struct uialert *a = AL(self);
    if (!a->visible) return;
    id d = a->delegate; const char *p = a->sheet ? "actionSheet" : "alertView";
    char sel[96];
    snprintf(sel, sizeof sel, "%s:willDismissWithButtonIndex:", p);
    if (d && rt_responds(d, rt_sel(sel))) ((void (*)(id, SEL, id, long))objc_msgSend)(d, rt_sel(sel), self, idx);
    close_overlay(&a->window); a->visible = 0;
    snprintf(sel, sizeof sel, "%s:didDismissWithButtonIndex:", p);
    if (d && rt_responds(d, rt_sel(sel))) ((void (*)(id, SEL, id, long))objc_msgSend)(d, rt_sel(sel), self, idx);
    objc_autorelease(self);
}
static void al_buttonTapped(id self, SEL s, id button) {
    (void)s; struct uialert *a = AL(self);
    long idx = RT_SEND0(long, button, "tag");
    id d = a->delegate; char sel[96];
    objc_retain(self);
    snprintf(sel, sizeof sel, "%s:clickedButtonAtIndex:", a->sheet ? "actionSheet" : "alertView");
    if (d && rt_responds(d, rt_sel(sel))) ((void (*)(id, SEL, id, long))objc_msgSend)(d, rt_sel(sel), self, idx);
    al_dismissWith(self, NULL, idx, 1);
    objc_release(self);
}
/* UIAlertController */
struct uiaction { Class isa; id title; long style, enabled; void *handler; };
#define ACT(x) ((struct uiaction *)(x))
static id act_make(Class c, SEL s, id title, long style, void *handler) {
    (void)s; id a = rt_alloc_instance(c, 0); struct uiaction *x = ACT(a);
    x->title = title ? RT_SEND0(id, title, "copy") : NULL; x->style = style; x->enabled = 1; x->handler = handler ? _Block_copy(handler) : NULL;
    return objc_autorelease(a);
}
static void act_dealloc(id self, SEL s) { (void)s; objc_release(ACT(self)->title); if (ACT(self)->handler) _Block_release(ACT(self)->handler); rt_dealloc_instance(self); }
static id act_title(id self, SEL s) { (void)s; return ACT(self)->title; }
static long act_style(id self, SEL s) { (void)s; return ACT(self)->style; }
static rt_bool act_enabled(id self, SEL s) { (void)s; return ACT(self)->enabled != 0; }
static void act_setEnabled(id self, SEL s, long v) { (void)s; ACT(self)->enabled = v & 0xff; }
struct uialertc { struct uivc vc; id title, message, actions, fields, window, preferred; long style; };
#define AC(x) ((struct uialertc *)(x))
static id ac_make(Class c, SEL s, id title, id msg, long style) {
    (void)s; id a = RT_SEND2(id, rt_alloc_instance(c, 0), "initWithNibName:bundle:", id, NULL, id, NULL);
    struct uialertc *x = AC(a);
    x->title = title ? RT_SEND0(id, title, "copy") : NULL; x->message = msg ? RT_SEND0(id, msg, "copy") : NULL; x->style = style;
    x->actions = mkarray(); x->fields = mkarray();
    return objc_autorelease(a);
}
static void ac_addAction(id self, SEL s, id a) { (void)s; if (a) RT_SEND1(void, AC(self)->actions, "addObject:", id, a); }
static id ac_actions(id self, SEL s) { (void)s; return AC(self)->actions; }
static void ac_addTextField(id self, SEL s, void *cfg) {
    (void)s; id tf = rt_view_new(RT_CLASS(UITextField), (CGRect){ { 0, 0 }, { 240, 28 } });
    RT_SEND1(void, tf, "setBorderStyle:", long, 3);
    RT_SEND1(void, AC(self)->fields, "addObject:", id, tf);
    if (cfg) ((void (*)(void *, id))((struct rt_block *)cfg)->invoke)(cfg, tf);
    objc_release(tf);
}
static id ac_fields(id self, SEL s) { (void)s; return AC(self)->fields; }
#define ACID(get, set, field) static id get(id self, SEL s) { (void)s; return AC(self)->field; } static void set(id self, SEL s, id v) { (void)s; objc_retain(v); objc_release(AC(self)->field); AC(self)->field = v; }
ACID(ac_title, ac_setTitle, title) ACID(ac_message, ac_setMessage, message) ACID(ac_preferred, ac_setPreferred, preferred)
static long ac_style(id self, SEL s) { (void)s; return AC(self)->style; }
static void ac_loadView(id self, SEL s) {
    (void)s; id v = rt_view_new(RT_CLASS(UIView), screen_bounds()); RT_SEND1(void, v, "setBackgroundColor:", id, rt_color_make(0, 0, 0, 0));
    vc_setView(self, NULL, v); objc_release(v);
}
static void ac_viewDidAppear(id self, SEL s, long a) {
    (void)s; (void)a; struct uialertc *x = AC(self);
    if (x->window) return;
    id titles = mkarray(); long cancel = -1, destr = -1;
    for (size_t i = 0; i < rt_array_count(x->actions); i++) {
        id act = rt_array_at(x->actions, i);
        RT_SEND1(void, titles, "addObject:", id, ACT(act)->title ? ACT(act)->title : rt_str(""));
        if (ACT(act)->style == 1) cancel = (long)i; else if (ACT(act)->style == 2) destr = (long)i;
    }
    g_prev_key = rt_app_key_window();
    x->window = build_overlay(self, x->title, x->message, titles, cancel, destr, x->style == 0, x->fields);
    objc_release(titles);
}
struct ac_fire { id ctl; id action; };
static void ac_fire_handler(void *p) {
    struct ac_fire *f = p;
    void *h = ACT(f->action)->handler;
    if (h) ((void (*)(void *, id))((struct rt_block *)h)->invoke)(h, f->action);
    objc_release(f->ctl); objc_release(f->action); free(f);
}
static void ac_buttonTapped(id self, SEL s, id button) {
    (void)s; struct uialertc *x = AC(self);
    long idx = RT_SEND0(long, button, "tag");
    id act = idx >= 0 && (size_t)idx < rt_array_count(x->actions) ? rt_array_at(x->actions, (size_t)idx) : NULL;
    close_overlay(&x->window);
    struct ac_fire *f = malloc(sizeof *f); f->ctl = objc_retain(self); f->action = objc_retain(act);
    RT_SEND2(void, self, "dismissViewControllerAnimated:completion:", long, 1, void *, NULL);
    if (act) rt_after(0, ac_fire_handler, f); else { objc_release(f->ctl); free(f); }
}
static void ac_dealloc(id self, SEL s) {
    struct uialertc *x = AC(self);
    close_overlay(&x->window);
    objc_release(x->title); objc_release(x->message); objc_release(x->actions); objc_release(x->fields);
    vc_dealloc(self, s);
}

/* ===================================================== assorted other classes */
/* UIPasteboard: in-memory */
struct uipb { Class isa; id name; id items; };
static id g_general;
static id pb_general(Class c, SEL s) { (void)s; if (!g_general) { g_general = rt_alloc_instance(c, 0); ((struct uipb *)g_general)->items = RT_SEND0(id, RT_SEND0(id, (id)rt_class_named("NSMutableDictionary"), "alloc"), "init"); } return g_general; }
static id pb_named(Class c, SEL s, id name, long create) { (void)name; (void)create; return pb_general(c, s); }
static id pb_string(id self, SEL s) { (void)s; return rt_dict_get(((struct uipb *)self)->items, rt_str("public.utf8-plain-text")); }
static void pb_setString(id self, SEL s, id v) { (void)s; if (v) RT_SEND2(void, ((struct uipb *)self)->items, "setObject:forKey:", id, v, id, rt_str("public.utf8-plain-text")); }
static id pb_dataForType(id self, SEL s, id type) { (void)s; return type ? rt_dict_get(((struct uipb *)self)->items, type) : NULL; }
static void pb_setData(id self, SEL s, id data, id type) { (void)s; if (data && type) RT_SEND2(void, ((struct uipb *)self)->items, "setObject:forKey:", id, data, id, type); }
static id pb_image(id self, SEL s) { (void)s; return rt_dict_get(((struct uipb *)self)->items, rt_str("public.image")); }
static void pb_setImage(id self, SEL s, id v) { (void)s; if (v) RT_SEND2(void, ((struct uipb *)self)->items, "setObject:forKey:", id, v, id, rt_str("public.image")); }
static id pb_types(id self, SEL s) { (void)s; return RT_SEND0(id, ((struct uipb *)self)->items, "allKeys"); }
static rt_bool pb_containsTypes(id self, SEL s, id types) { (void)s; for (size_t i = 0; types && i < rt_array_count(types); i++) if (rt_dict_get(((struct uipb *)self)->items, rt_array_at(types, i))) return 1; return 0; }
static long pb_numItems(id self, SEL s) { (void)s; return RT_SEND0(long, ((struct uipb *)self)->items, "count") ? 1 : 0; }
static long pb_changeCount(id self, SEL s) { (void)self; (void)s; return 1; }
/* UIMenuController / UIMenuItem */
static id g_menu;
static id menu_shared(Class c, SEL s) { (void)s; if (!g_menu) g_menu = rt_alloc_instance(c, 0); return g_menu; }
static void menu_setVisible(id self, SEL s, long v, long a) { (void)self; (void)s; (void)v; (void)a; }
static void menu_setTarget(id self, SEL s, CGRect r, id v) { (void)self; (void)s; (void)r; (void)v; }
struct uimenuitem { Class isa; id title; SEL action; };
static id mi_init(id self, SEL s, id title, SEL action) { (void)s; ((struct uimenuitem *)self)->title = objc_retain(title); ((struct uimenuitem *)self)->action = action; return self; }
/* activity view controller: present = immediate cancel */
struct uiactvc { struct uivc vc; void *completion, *completion2; id items; };
static id avc_init(id self, SEL s, id items, id acts) { (void)acts; (void)s; self = vc_initWithNib(self, NULL, NULL, NULL); ((struct uiactvc *)self)->items = objc_retain(items); return self; }
static void avc_setCompletion(id self, SEL s, void *b) { (void)s; ((struct uiactvc *)self)->completion = b ? _Block_copy(b) : NULL; }
static void avc_setCompletion2(id self, SEL s, void *b) { (void)s; ((struct uiactvc *)self)->completion2 = b ? _Block_copy(b) : NULL; }
static void avc_viewDidAppear(id self, SEL s, long a) {
    (void)s; (void)a; struct uiactvc *x = (struct uiactvc *)self;
    rt_log("UIActivityViewController: sharing is not available; reporting cancel");
    RT_SEND2(void, self, "dismissViewControllerAnimated:completion:", long, 0, void *, NULL);
    if (x->completion2) ((void (*)(void *, id, long, id, id))((struct rt_block *)x->completion2)->invoke)(x->completion2, NULL, 0, NULL, NULL);
    else if (x->completion) ((void (*)(void *, id, long))((struct rt_block *)x->completion)->invoke)(x->completion, NULL, 0);
}
static id act_init(id self, SEL s) { (void)s; return self; }
static id aip_initPlaceholder(id self, SEL s, id ph) { (void)s; objc_retain(ph); return self; }
static id actp_type(id self, SEL s) { (void)self; (void)s; return NULL; }
/* popover controller: shows content modally */
struct uipop { Class isa; id content; id delegate; long visible; };
static id pop_init(id self, SEL s, id content) { (void)s; ((struct uipop *)self)->content = objc_retain(content); return self; }
static void pop_present(id self, SEL s, CGRect r, id view, unsigned long dirs, long a) {
    (void)s; (void)r; (void)dirs; (void)a; struct uipop *p = (struct uipop *)self;
    id win = view ? rt_view_window(view) : rt_app_key_window(); id root = win ? ((struct uiwindow *)win)->root : NULL;
    if (root && p->content) { RT_SEND3(void, root, "presentViewController:animated:completion:", id, p->content, long, 0, void *, NULL); p->visible = 1; }
}
static void pop_presentFromItem(id self, SEL s, id item, unsigned long dirs, long a) { (void)item; pop_present(self, s, (CGRect){ { 0, 0 }, { 0, 0 } }, NULL, dirs, a); }
static void pop_dismiss(id self, SEL s, long a) { (void)s; (void)a; struct uipop *p = (struct uipop *)self; if (p->visible && p->content) RT_SEND2(void, p->content, "dismissViewControllerAnimated:completion:", long, 0, void *, NULL); p->visible = 0; }
static rt_bool pop_visible(id self, SEL s) { (void)s; return ((struct uipop *)self)->visible != 0; }
static id pop_content(id self, SEL s) { (void)s; return ((struct uipop *)self)->content; }
static void pop_setDelegate(id self, SEL s, id d) { (void)s; ((struct uipop *)self)->delegate = d; }
/* document interaction / image picker */
static id doc_withURL(Class c, SEL s, id url) { (void)s; (void)url; return objc_autorelease(rt_alloc_instance(c, 0)); }
static rt_bool doc_present(id self, SEL s, CGRect r, id v, long a) { (void)self; (void)s; (void)r; (void)v; (void)a; return 0; }
static rt_bool ip_available(Class c, SEL s, long type) { (void)c; (void)s; return type == 0 ? 0 : 0; }
static id ip_mediaTypes(Class c, SEL s, long type) { (void)c; (void)s; (void)type; return NULL; }
/* web view */
struct uiweb { struct uiview v; id delegate; };
static void web_load(id self, SEL s, id req) {
    (void)s; (void)req; id d = ((struct uiweb *)self)->delegate;
    if (rt_first_time("UIWebView")) rt_log("UIWebView: web content is not supported (load fails)");
    id err = RT_SEND3(id, (id)rt_class_named("NSError"), "errorWithDomain:code:userInfo:", id, rt_str("NSURLErrorDomain"), long, -1009, id, NULL);
    if (d && rt_responds(d, rt_sel("webView:didFailLoadWithError:"))) RT_SEND2(void, d, "webView:didFailLoadWithError:", id, self, id, err);
}
static void web_loadHTML(id self, SEL s, id html, id base) { (void)html; (void)base; web_load(self, s, NULL); }
static void web_setDelegate(id self, SEL s, id d) { (void)s; ((struct uiweb *)self)->delegate = d; }
static id web_delegate(id self, SEL s) { (void)s; return ((struct uiweb *)self)->delegate; }
static id web_eval(id self, SEL s, id js) { (void)self; (void)s; (void)js; return rt_str(""); }
/* pickers */
struct uipicker { struct uiview v; id dataSource, delegate; long sel[8]; };
static void pk_setDS(id self, SEL s, id d) { (void)s; ((struct uipicker *)self)->dataSource = d; }
static void pk_setDelegate(id self, SEL s, id d) { (void)s; ((struct uipicker *)self)->delegate = d; }
static void pk_select(id self, SEL s, long row, long comp, long a) { (void)s; (void)a; if (comp >= 0 && comp < 8) ((struct uipicker *)self)->sel[comp] = row; }
static long pk_selected(id self, SEL s, long comp) { (void)s; return comp >= 0 && comp < 8 ? ((struct uipicker *)self)->sel[comp] : 0; }
struct uidate { struct uicontrol c; id date, minDate, maxDate; long mode; double countdown; };
static id dp_date(id self, SEL s) { (void)s; struct uidate *d = (struct uidate *)self; return d->date ? d->date : RT_SEND0(id, (id)rt_class_named("NSDate"), "date"); }
static void dp_setDate(id self, SEL s, id v) { (void)s; struct uidate *d = (struct uidate *)self; objc_retain(v); objc_release(d->date); d->date = v; }
static void dp_setDateAnim(id self, SEL s, id v, long a) { (void)a; dp_setDate(self, s, v); }
static void dp_setMode(id self, SEL s, long v) { (void)s; ((struct uidate *)self)->mode = v; }
static double dp_countdown(id self, SEL s) { (void)s; return ((struct uidate *)self)->countdown; }
static void dp_setCountdown(id self, SEL s, double v) { (void)s; ((struct uidate *)self)->countdown = v; }
/* accessibility element / constraints / effects / storyboard / traits */
static id ae_init(id self, SEL s, id container) { (void)s; (void)container; return self; }
struct nslc { Class isa; double constant, multiplier; long active, priority; id first, second; long a1, a2, rel; };
static id lc_make(Class c, SEL s, id item, long a1, long rel, id item2, long a2, double mul, double k) {
    (void)s; id o = rt_alloc_instance(c, 0); struct nslc *x = (struct nslc *)o;
    x->first = item; x->second = item2; x->a1 = a1; x->a2 = a2; x->rel = rel; x->multiplier = mul; x->constant = k; x->priority = 1000;
    return objc_autorelease(o);
}
static id lc_visual(Class c, SEL s, id fmt, long opts, id metrics, id views) { (void)c; (void)s; (void)fmt; (void)opts; (void)metrics; (void)views; return objc_autorelease(mkarray()); }
static double lc_constant(id self, SEL s) { (void)s; return ((struct nslc *)self)->constant; }
static void lc_setConstant(id self, SEL s, double v) { (void)s; ((struct nslc *)self)->constant = v; }
static void lc_activate(Class c, SEL s, id arr) { (void)c; (void)s; (void)arr; }
static id ve_initWithEffect(id self, SEL s, id effect) { (void)effect; (void)s; self = ((id (*)(id, SEL, CGRect))objc_msgSend)(self, rt_sel("initWithFrame:"), (CGRect){ { 0, 0 }, { 0, 0 } }); RT_SEND1(void, self, "setBackgroundColor:", id, rt_color_make(0.95, 0.95, 0.95, 0.85)); return self; }
static id ve_contentView(id self, SEL s) { (void)s; return self; }
static id blur_effect(Class c, SEL s, long style) { (void)s; (void)style; return objc_autorelease(rt_alloc_instance(c, 0)); }
static id sb_withName(Class c, SEL s, id name, id bundle) { (void)c; (void)s; (void)bundle; rt_warn("UIStoryboard storyboardWithName:%s is not supported", name ? rt_str_utf8(name) : "?"); return NULL; }
static long tc_idiom(id self, SEL s) { (void)self; (void)s; return 0; }
static long tc_sizeClassH(id self, SEL s) { (void)self; (void)s; return 1; }
static long tc_sizeClassV(id self, SEL s) { (void)self; (void)s; return 2; }
static double tc_scale(id self, SEL s) { (void)self; (void)s; return rt_scr_scale; }

/* draw dispatcher for this file's classes */
void rt_controls2_draw(id view, struct cgctx *c, CGRect b) {
    if (rt_is_kind_of(view, RT_CLASS(UINavigationBar))) draw_navbar(view, c, b);
    else if (rt_is_kind_of(view, RT_CLASS(UITabBar))) draw_tabbar(view, c, b);
    else if (rt_is_kind_of(view, RT_CLASS(UIToolbar))) draw_toolbar(view, c, b);
    else if (rt_is_kind_of(view, RT_CLASS(UITableView))) draw_table(view, c, b);
    else if (rt_is_kind_of(view, RT_CLASS(UITableViewCell))) draw_cell(view, c, b);
}

void rt_controls2_init(void) {
    Class o = RT_CLASS(NSObject), r = RT_CLASS(UIResponder), v = RT_CLASS(UIView);
    NEWCLASS(UIViewController, r, sizeof(struct uivc) - sizeof(void *));
    Class vc = RT_CLASS(UIViewController);
    IM(vc, "init", vc_init); IM(vc, "initWithNibName:bundle:", vc_initWithNib); IM(vc, "initWithCoder:", vc_initWithCoder); IM(vc, "dealloc", vc_dealloc);
    IM(vc, "nibName", vc_nibName); IM(vc, "nibBundle", vc_nibBundle); IM(vc, "loadView", vc_loadView); IM(vc, "loadViewIfNeeded", vc_loadViewIfNeeded);
    IM(vc, "view", vc_view); IM(vc, "setView:", vc_setView); IM(vc, "viewIfLoaded", vc_viewIfLoaded); IM(vc, "isViewLoaded", vc_isViewLoaded);
    IM(vc, "viewDidLoad", nop); IM(vc, "viewWillAppear:", nopL); IM(vc, "viewDidAppear:", nopL); IM(vc, "viewWillDisappear:", nopL); IM(vc, "viewDidDisappear:", nopL);
    IM(vc, "viewWillLayoutSubviews", nop); IM(vc, "viewDidLayoutSubviews", nop); IM(vc, "didReceiveMemoryWarning", nop); IM(vc, "viewDidUnload", nop);
    IM(vc, "title", vc_title); IM(vc, "setTitle:", vc_setTitle); IM(vc, "nextResponder", vc_nextResponder);
    IM(vc, "supportedInterfaceOrientations", vc_orientations); IM(vc, "interfaceOrientation", vc_interfaceOrientation);
    IM(vc, "preferredInterfaceOrientationForPresentation", vc_interfaceOrientation);
    IM(vc, "shouldAutorotate", no0); IM(vc, "shouldAutorotateToInterfaceOrientation:", no0); IM(vc, "prefersStatusBarHidden", yes0);
    IM(vc, "preferredStatusBarStyle", vc_statusBarStyle); IM(vc, "setNeedsStatusBarAppearanceUpdate", vc_setNeedsStatusBarUpdate);
    IM(vc, "parentViewController", vc_parent); IM(vc, "childViewControllers", vc_children); IM(vc, "children", vc_children);
    IM(vc, "addChildViewController:", vc_addChild); IM(vc, "removeFromParentViewController", vc_removeFromParent);
    IM(vc, "willMoveToParentViewController:", nop1); IM(vc, "didMoveToParentViewController:", nop1);
    IM(vc, "navigationController", vc_navController); IM(vc, "tabBarController", vc_tabBarController); IM(vc, "navigationItem", vc_navItem);
    IM(vc, "tabBarItem", vc_tabBarItem); IM(vc, "setTabBarItem:", vc_setTabBarItem);
    IM(vc, "presentedViewController", vc_presented); IM(vc, "presentingViewController", vc_presenting);
    IM(vc, "modalPresentationStyle", vc_modalStyle); IM(vc, "setModalPresentationStyle:", vc_setModalStyle); IM(vc, "setModalTransitionStyle:", vc_setTransitionStyle);
    IM(vc, "presentViewController:animated:completion:", vc_present); IM(vc, "presentModalViewController:animated:", vc_presentModal);
    IM(vc, "dismissViewControllerAnimated:completion:", vc_dismiss); IM(vc, "dismissModalViewControllerAnimated:", vc_dismissModal);
    IM(vc, "transitionFromViewController:toViewController:duration:options:animations:completion:", vc_transition);
    IM(vc, "storyboard", vc_storyboard); IM(vc, "preferredContentSize", vc_preferredSize); IM(vc, "setPreferredContentSize:", vc_setPreferredSize);
    IM(vc, "setContentSizeForViewInPopover:", vc_setPreferredSize);
    IM(vc, "toolbarItems", vc_toolbarItems); IM(vc, "setToolbarItems:", vc_setToolbarItems); IM(vc, "setToolbarItems:animated:", vc_setToolbarItemsAnim);
    IM(vc, "traitCollection", vc_traits); IM(vc, "performSegueWithIdentifier:sender:", vc_performSegue);
    IM(vc, "popoverPresentationController", vc_popoverPC); IM(vc, "setEdgesForExtendedLayout:", nopL); IM(vc, "setExtendedLayoutIncludesOpaqueBars:", nopL);
    IM(vc, "setAutomaticallyAdjustsScrollViewInsets:", nopL); IM(vc, "setHidesBottomBarWhenPushed:", nopL); IM(vc, "setDefinesPresentationContext:", nopL);
    IM(vc, "setProvidesPresentationContextTransitionStyle:", nopL); IM(vc, "setModalPresentationCapturesStatusBarAppearance:", nopL);
    IM(vc, "setWantsFullScreenLayout:", nopL); IM(vc, "setRestorationIdentifier:", nop1); IM(vc, "setTransitioningDelegate:", nop1);
    IM(vc, "isBeingPresented", no0); IM(vc, "isBeingDismissed", no0); IM(vc, "isMovingToParentViewController", no0); IM(vc, "isMovingFromParentViewController", no0);
    IM(vc, "topLayoutGuide", vc_layoutGuide); IM(vc, "bottomLayoutGuide", vc_layoutGuide); IM(vc, "length", vc_guideLength);
    IM(vc, "editButtonItem", nil0); IM(vc, "setEditing:animated:", nop1L); IM(vc, "isEditing", no0);
    (void)vc_safeArea; (void)zeroL;

    NEWCLASS(UINavigationController, vc, sizeof(struct uinav) - sizeof(struct uivc));
    Class nav = RT_CLASS(UINavigationController);
    IM(nav, "initWithRootViewController:", nav_initWithRoot); IM(nav, "init", nav_init); IM(nav, "loadView", nav_loadView);
    IM(nav, "pushViewController:animated:", nav_push); IM(nav, "popViewControllerAnimated:", nav_pop);
    IM(nav, "popToRootViewControllerAnimated:", nav_popToRoot); IM(nav, "popToViewController:animated:", nav_popTo);
    IM(nav, "topViewController", nav_top); IM(nav, "visibleViewController", nav_top); IM(nav, "viewControllers", nav_vcs);
    IM(nav, "setViewControllers:", nav_setVcs1); IM(nav, "setViewControllers:animated:", nav_setVcs);
    IM(nav, "navigationBar", nav_bar); IM(nav, "toolbar", nav_toolbar); IM(nav, "isNavigationBarHidden", nav_barHidden);
    IM(nav, "setNavigationBarHidden:", nav_setBarHidden); IM(nav, "setNavigationBarHidden:animated:", nav_setBarHiddenAnim);
    IM(nav, "setToolbarHidden:", nav_setToolbarHidden); IM(nav, "setToolbarHidden:animated:", nav_setToolbarHiddenAnim);
    IM(nav, "delegate", nav_delegate); IM(nav, "setDelegate:", nav_setDelegate); IM(nav, "interactivePopGestureRecognizer", nil0);
    IM(nav, "viewWillAppear:", nav_viewWillAppear);
    NEWCLASS(UITabBarController, vc, sizeof(struct uitab) - sizeof(struct uivc));
    Class tab = RT_CLASS(UITabBarController);
    IM(tab, "loadView", tab_loadView); IM(tab, "viewControllers", tab_vcs); IM(tab, "setViewControllers:", tab_setVcs1); IM(tab, "setViewControllers:animated:", tab_setVcs);
    IM(tab, "selectedIndex", tab_selected); IM(tab, "setSelectedIndex:", tab_setSelected); IM(tab, "selectedViewController", tab_selectedVC);
    IM(tab, "setSelectedViewController:", tab_setSelectedVC); IM(tab, "tabBar", tab_bar); IM(tab, "delegate", tab_delegate); IM(tab, "setDelegate:", tab_setDelegate);

    NEWCLASS(UINavigationItem, o, sizeof(struct uinavitem) - sizeof(void *));
    Class ni = RT_CLASS(UINavigationItem);
    IM(ni, "initWithTitle:", ni_initWithTitle); IM(ni, "title", ni_title); IM(ni, "setTitle:", ni_setTitle); IM(ni, "titleView", ni_titleView); IM(ni, "setTitleView:", ni_setTitleView);
    IM(ni, "backBarButtonItem", ni_back); IM(ni, "setBackBarButtonItem:", ni_setBack); IM(ni, "prompt", ni_prompt); IM(ni, "setPrompt:", ni_setPrompt);
    IM(ni, "leftBarButtonItem", ni_left); IM(ni, "setLeftBarButtonItem:", ni_setLeft); IM(ni, "setLeftBarButtonItem:animated:", ni_setLeftAnim);
    IM(ni, "rightBarButtonItem", ni_right); IM(ni, "setRightBarButtonItem:", ni_setRight); IM(ni, "setRightBarButtonItem:animated:", ni_setRightAnim);
    IM(ni, "leftBarButtonItems", ni_lefts); IM(ni, "setLeftBarButtonItems:", ni_setLefts); IM(ni, "setLeftBarButtonItems:animated:", ni_setLeftsAnim);
    IM(ni, "rightBarButtonItems", ni_rights); IM(ni, "setRightBarButtonItems:", ni_setRights); IM(ni, "setRightBarButtonItems:animated:", ni_setRightsAnim);
    IM(ni, "setHidesBackButton:", ni_setHidesBack); IM(ni, "setHidesBackButton:animated:", ni_setHidesBackAnim); IM(ni, "setLeftItemsSupplementBackButton:", nopL);
    NEWCLASS(UIBarItem, o, sizeof(struct uibaritem) - sizeof(void *));
    Class bi = RT_CLASS(UIBarItem);
    IM(bi, "init", bi_init); IM(bi, "title", bi_title); IM(bi, "setTitle:", bi_setTitle); IM(bi, "image", bi_image); IM(bi, "setImage:", bi_setImage);
    IM(bi, "isEnabled", bi_enabled); IM(bi, "setEnabled:", bi_setEnabled); IM(bi, "tag", bi_tag); IM(bi, "setTag:", bi_setTag);
    IM(bi, "setTitleTextAttributes:forState:", nop1L); IM(bi, "setImageInsets:", nop1); IM(bi, "setAccessibilityLabel:", nop1);
    NEWCLASS(UIBarButtonItem, bi, 0);
    Class bb = RT_CLASS(UIBarButtonItem);
    IM(bb, "initWithTitle:style:target:action:", bi_initTitle); IM(bb, "initWithImage:style:target:action:", bi_initImage);
    IM(bb, "initWithImage:landscapeImagePhone:style:target:action:", bi_initImage2); IM(bb, "initWithBarButtonSystemItem:target:action:", bi_initSystem);
    IM(bb, "initWithCustomView:", bi_initCustom); IM(bb, "customView", bi_customView); IM(bb, "setCustomView:", bi_setCustomView);
    IM(bb, "target", bi_target); IM(bb, "setTarget:", bi_setTarget); IM(bb, "action", bi_action); IM(bb, "setAction:", bi_setAction);
    IM(bb, "style", bi_style); IM(bb, "setStyle:", bi_setStyle); IM(bb, "width", bi_width); IM(bb, "setWidth:", bi_setWidth);
    IM(bb, "tintColor", bi_tint); IM(bb, "setTintColor:", bi_setTint); IM(bb, "setBackgroundImage:forState:barMetrics:", nop1L);
    NEWCLASS(UITabBarItem, bi, 0);
    IM(RT_CLASS(UITabBarItem), "initWithTitle:image:tag:", bi_initTitle); IM(RT_CLASS(UITabBarItem), "setBadgeValue:", nop1); IM(RT_CLASS(UITabBarItem), "setSelectedImage:", nop1);

    NEWCLASS(UIToolbar, v, sizeof(struct uibar) - sizeof(struct uiview));
    Class tb = RT_CLASS(UIToolbar);
    IM(tb, "initWithFrame:", tb_initWithFrame); IM(tb, "items", tb_items); IM(tb, "setItems:", tb_setItems); IM(tb, "setItems:animated:", tb_setItemsAnim);
    IM(tb, "barTintColor", bar_barTint); IM(tb, "setBarTintColor:", bar_setBarTint); IM(tb, "barStyle", bar_style); IM(tb, "setBarStyle:", bar_setStyle);
    IM(tb, "isTranslucent", bar_translucent); IM(tb, "setTranslucent:", bar_setTranslucent);
    IM(tb, "setBackgroundImage:forToolbarPosition:barMetrics:", bar_setBgImage); IM(tb, "setShadowImage:forToolbarPosition:", nop1L);
    IM(tb, "touchesBegan:withEvent:", bar_touchesBegan); IM(tb, "touchesEnded:withEvent:", tb_touchesEnded);
    NEWCLASS(UINavigationBar, v, sizeof(struct uibar) - sizeof(struct uiview));
    Class nb = RT_CLASS(UINavigationBar);
    IM(nb, "initWithFrame:", nb_initWithFrame); IM(nb, "items", nb_items); IM(nb, "setItems:animated:", nb_setItems); IM(nb, "setItems:", tb_setItems);
    IM(nb, "topItem", nb_topItem); IM(nb, "backItem", nb_backItem); IM(nb, "pushNavigationItem:animated:", nb_pushItem); IM(nb, "popNavigationItemAnimated:", nb_popItem);
    IM(nb, "delegate", nb_delegate); IM(nb, "setDelegate:", nb_setDelegate); IM(nb, "setTitleTextAttributes:", nb_setTitleAttrs);
    IM(nb, "barTintColor", bar_barTint); IM(nb, "setBarTintColor:", bar_setBarTint); IM(nb, "barStyle", bar_style); IM(nb, "setBarStyle:", bar_setStyle);
    IM(nb, "isTranslucent", bar_translucent); IM(nb, "setTranslucent:", bar_setTranslucent); IM(nb, "setBackgroundImage:forBarMetrics:", bar_setBgImage2);
    IM(nb, "setShadowImage:", nop1); IM(nb, "touchesBegan:withEvent:", bar_touchesBegan); IM(nb, "touchesEnded:withEvent:", nb_touchesEnded);
    NEWCLASS(UITabBar, v, sizeof(struct uibar) - sizeof(struct uiview));
    Class tbar = RT_CLASS(UITabBar);
    IM(tbar, "initWithFrame:", tab_initWithFrame); IM(tbar, "items", tb_items); IM(tbar, "setItems:", tb_setItems); IM(tbar, "setItems:animated:", tb_setItemsAnim);
    IM(tbar, "barTintColor", bar_barTint); IM(tbar, "setBarTintColor:", bar_setBarTint); IM(tbar, "setTranslucent:", bar_setTranslucent);
    IM(tbar, "touchesBegan:withEvent:", bar_touchesBegan); IM(tbar, "touchesEnded:withEvent:", tab_touchesEnded); IM(tbar, "setSelectedImageTintColor:", nop1);

    NEWCLASS(UITableView, RT_CLASS(UIScrollView), sizeof(struct uitable) - sizeof(struct uiscroll));
    Class tv = RT_CLASS(UITableView);
    IM(tv, "initWithFrame:style:", tv_initWithFrameStyle); IM(tv, "initWithFrame:", tv_initWithFrame2);
    IM(tv, "dataSource", tv_dataSource); IM(tv, "setDataSource:", tv_setDataSource); IM(tv, "delegate", tv_delegate2); IM(tv, "setDelegate:", tv_setDelegate2);
    IM(tv, "rowHeight", tv_rowHeight); IM(tv, "setRowHeight:", tv_setRowHeight); IM(tv, "setEstimatedRowHeight:", nopL);
    IM(tv, "separatorStyle", tv_sepStyle); IM(tv, "setSeparatorStyle:", tv_setSepStyle); IM(tv, "setSeparatorColor:", tv_setSepColor);
    IM(tv, "tableHeaderView", tv_header); IM(tv, "setTableHeaderView:", tv_setHeader); IM(tv, "tableFooterView", tv_footer); IM(tv, "setTableFooterView:", tv_setFooter);
    IM(tv, "setBackgroundView:", tv_setBgView); IM(tv, "dequeueReusableCellWithIdentifier:", tv_dequeue);
    IM(tv, "dequeueReusableCellWithIdentifier:forIndexPath:", tv_dequeueIP); IM(tv, "registerClass:forCellReuseIdentifier:", tv_registerClass);
    IM(tv, "registerNib:forCellReuseIdentifier:", tv_registerNib); IM(tv, "reloadData", tv_reloadData); IM(tv, "layoutSubviews", tv_layoutSubviews);
    IM(tv, "cellForRowAtIndexPath:", tv_cellForRow); IM(tv, "visibleCells", tv_visibleCells); IM(tv, "indexPathsForVisibleRows", tv_visibleIPs);
    IM(tv, "indexPathForCell:", tv_ipForCell); IM(tv, "indexPathForRowAtPoint:", tv_ipForPoint); IM(tv, "rectForRowAtIndexPath:", tv_rectForRow);
    IM(tv, "numberOfSections", tv_numSections); IM(tv, "numberOfRowsInSection:", tv_numRows);
    IM(tv, "scrollToRowAtIndexPath:atScrollPosition:animated:", tv_scrollToRow);
    IM(tv, "selectRowAtIndexPath:animated:scrollPosition:", tv_select); IM(tv, "deselectRowAtIndexPath:animated:", tv_deselect);
    IM(tv, "indexPathForSelectedRow", tv_selectedIP);
    IM(tv, "insertRowsAtIndexPaths:withRowAnimation:", tv_updates); IM(tv, "deleteRowsAtIndexPaths:withRowAnimation:", tv_updates);
    IM(tv, "reloadRowsAtIndexPaths:withRowAnimation:", tv_updates); IM(tv, "insertSections:withRowAnimation:", tv_updates);
    IM(tv, "deleteSections:withRowAnimation:", tv_updates); IM(tv, "reloadSections:withRowAnimation:", tv_updates);
    IM(tv, "beginUpdates", tv_beginUpdates); IM(tv, "endUpdates", tv_endUpdates);
    IM(tv, "setEditing:animated:", tv_setEditing); IM(tv, "setEditing:", tv_setEditing1); IM(tv, "isEditing", tv_isEditing);
    IM(tv, "setAllowsSelection:", tv_setAllowsSelection); IM(tv, "setAllowsSelectionDuringEditing:", nopL); IM(tv, "setAllowsMultipleSelection:", nopL);
    IM(tv, "touchesEnded:withEvent:", tv_touchesEnded); IM(tv, "setSeparatorInset:", nop1); IM(tv, "setSectionHeaderHeight:", nopL);
    IM(tv, "setSectionFooterHeight:", nopL); IM(tv, "setSectionIndexColor:", nop1); IM(tv, "setCellLayoutMarginsFollowReadableWidth:", nopL);
    NEWCLASS(UITableViewCell, v, sizeof(struct uicell) - sizeof(struct uiview));
    Class cl = RT_CLASS(UITableViewCell);
    IM(cl, "initWithStyle:reuseIdentifier:", cell_initStyle); IM(cl, "initWithFrame:", cell_initWithFrame); IM(cl, "initWithCoder:", cell_initWithCoder);
    IM(cl, "textLabel", cell_textLabel); IM(cl, "detailTextLabel", cell_detailLabel); IM(cl, "imageView", cell_imageView);
    IM(cl, "contentView", cell_contentView); IM(cl, "reuseIdentifier", cell_reuseId); IM(cl, "layoutSubviews", cell_layoutSubviews);
    IM(cl, "setSelected:", cell_setSelected); IM(cl, "setSelected:animated:", cell_setSelectedAnim); IM(cl, "isSelected", cell_selected);
    IM(cl, "setHighlighted:", cell_setHighlighted); IM(cl, "setHighlighted:animated:", cell_setHighlightedAnim); IM(cl, "isHighlighted", cell_highlighted);
    IM(cl, "accessoryType", cell_accessory); IM(cl, "setAccessoryType:", cell_setAccessory); IM(cl, "accessoryView", cell_accessoryView);
    IM(cl, "setAccessoryView:", cell_setAccessoryView); IM(cl, "selectionStyle", cell_selectionStyle); IM(cl, "setSelectionStyle:", cell_setSelectionStyle);
    IM(cl, "backgroundView", cell_bgView); IM(cl, "setBackgroundView:", cell_setBgView); IM(cl, "setSelectedBackgroundView:", cell_setSelBgView);
    IM(cl, "prepareForReuse", nop); IM(cl, "setEditingAccessoryType:", nopL); IM(cl, "setShowsReorderControl:", nopL); IM(cl, "setIndentationLevel:", nopL);
    IM(cl, "setSeparatorInset:", nop1);
    NEWCLASS(UITableViewController, vc, 0);

    rt_native_class(RT_CLASS(UIAlertView), &iosrt_meta_UIAlertView, "UIAlertView", o, sizeof(struct uialert) - sizeof(void *));
    rt_native_class(RT_CLASS(UIActionSheet), &iosrt_meta_UIActionSheet, "UIActionSheet", o, sizeof(struct uialert) - sizeof(void *));
    Class classes[2] = { RT_CLASS(UIAlertView), RT_CLASS(UIActionSheet) };
    extern char iosrt_imp_UIAlertView_init[], iosrt_imp_UIActionSheet_init[];
    IM(RT_CLASS(UIAlertView), "initWithTitle:message:delegate:cancelButtonTitle:otherButtonTitles:", iosrt_imp_UIAlertView_init);
    IM(RT_CLASS(UIActionSheet), "initWithTitle:delegate:cancelButtonTitle:destructiveButtonTitle:otherButtonTitles:", iosrt_imp_UIActionSheet_init);
    for (int i = 0; i < 2; i++) {
        Class a = classes[i];
        IM(a, "init", al_init); IM(a, "dealloc", al_dealloc); IM(a, "addButtonWithTitle:", al_addButton); IM(a, "buttonTitleAtIndex:", al_buttonTitle);
        IM(a, "numberOfButtons", al_numButtons); IM(a, "cancelButtonIndex", al_cancelIndex); IM(a, "setCancelButtonIndex:", al_setCancelIndex);
        IM(a, "firstOtherButtonIndex", al_firstOther); IM(a, "destructiveButtonIndex", al_destructive); IM(a, "setDestructiveButtonIndex:", al_setDestructive);
        IM(a, "title", al_title); IM(a, "setTitle:", al_setTitle); IM(a, "message", al_message); IM(a, "setMessage:", al_setMessage);
        IM(a, "delegate", al_delegate); IM(a, "setDelegate:", al_setDelegate); IM(a, "alertViewStyle", al_style); IM(a, "setAlertViewStyle:", al_setStyle);
        IM(a, "textFieldAtIndex:", al_textField); IM(a, "tag", al_tag); IM(a, "setTag:", al_setTag); IM(a, "isVisible", al_visible);
        IM(a, "show", al_show); IM(a, "showInView:", al_showInView); IM(a, "showFromRect:inView:animated:", al_showFrom);
        IM(a, "showFromBarButtonItem:animated:", al_showFromItem); IM(a, "showFromToolbar:", al_showInView); IM(a, "showFromTabBar:", al_showInView);
        IM(a, "dismissWithClickedButtonIndex:animated:", al_dismissWith); IM(a, "iosrt_alertButton:", al_buttonTapped);
        IM(a, "setActionSheetStyle:", nopL);
    }
    NEWCLASS(UIAlertAction, o, sizeof(struct uiaction) - sizeof(void *));
    Class aa = RT_CLASS(UIAlertAction);
    CM(aa, "actionWithTitle:style:handler:", act_make); IM(aa, "dealloc", act_dealloc); IM(aa, "title", act_title); IM(aa, "style", act_style);
    IM(aa, "isEnabled", act_enabled); IM(aa, "setEnabled:", act_setEnabled);
    NEWCLASS(UIAlertController, vc, sizeof(struct uialertc) - sizeof(struct uivc));
    Class ac = RT_CLASS(UIAlertController);
    CM(ac, "alertControllerWithTitle:message:preferredStyle:", ac_make); IM(ac, "addAction:", ac_addAction); IM(ac, "actions", ac_actions);
    IM(ac, "addTextFieldWithConfigurationHandler:", ac_addTextField); IM(ac, "textFields", ac_fields);
    IM(ac, "title", ac_title); IM(ac, "setTitle:", ac_setTitle); IM(ac, "message", ac_message); IM(ac, "setMessage:", ac_setMessage);
    IM(ac, "preferredAction", ac_preferred); IM(ac, "setPreferredAction:", ac_setPreferred); IM(ac, "preferredStyle", ac_style);
    IM(ac, "loadView", ac_loadView); IM(ac, "viewDidAppear:", ac_viewDidAppear); IM(ac, "iosrt_alertButton:", ac_buttonTapped); IM(ac, "dealloc", ac_dealloc);

    NEWCLASS(UIPasteboard, o, sizeof(struct uipb) - sizeof(void *));
    Class pb = RT_CLASS(UIPasteboard);
    CM(pb, "generalPasteboard", pb_general); CM(pb, "pasteboardWithName:create:", pb_named); CM(pb, "pasteboardWithUniqueName", pb_general);
    IM(pb, "string", pb_string); IM(pb, "setString:", pb_setString); IM(pb, "dataForPasteboardType:", pb_dataForType);
    IM(pb, "setData:forPasteboardType:", pb_setData); IM(pb, "valueForPasteboardType:", pb_dataForType); IM(pb, "setValue:forPasteboardType:", pb_setData);
    IM(pb, "image", pb_image); IM(pb, "setImage:", pb_setImage); IM(pb, "pasteboardTypes", pb_types); IM(pb, "containsPasteboardTypes:", pb_containsTypes);
    IM(pb, "numberOfItems", pb_numItems); IM(pb, "changeCount", pb_changeCount); IM(pb, "setPersistent:", nopL); IM(pb, "setItems:", nop1);
    NEWCLASS(UIMenuController, o, 0);
    Class mc = RT_CLASS(UIMenuController);
    CM(mc, "sharedMenuController", menu_shared); IM(mc, "setMenuVisible:animated:", menu_setVisible); IM(mc, "setMenuVisible:", nopL);
    IM(mc, "setTargetRect:inView:", menu_setTarget); IM(mc, "setMenuItems:", nop1); IM(mc, "menuItems", nil0); IM(mc, "isMenuVisible", no0); IM(mc, "update", nop);
    NEWCLASS(UIMenuItem, o, sizeof(struct uimenuitem) - sizeof(void *));
    IM(RT_CLASS(UIMenuItem), "initWithTitle:action:", mi_init);
    NEWCLASS(UIActivity, o, 0);
    IM(RT_CLASS(UIActivity), "init", act_init); IM(RT_CLASS(UIActivity), "activityType", actp_type); IM(RT_CLASS(UIActivity), "activityDidFinish:", nopL);
    NEWCLASS(UIActivityItemProvider, RT_CLASS(NSObject), sizeof(void *));
    IM(RT_CLASS(UIActivityItemProvider), "initWithPlaceholderItem:", aip_initPlaceholder); IM(RT_CLASS(UIActivityItemProvider), "activityType", actp_type);
    NEWCLASS(UIActivityViewController, vc, sizeof(struct uiactvc) - sizeof(struct uivc));
    Class avc = RT_CLASS(UIActivityViewController);
    IM(avc, "initWithActivityItems:applicationActivities:", avc_init); IM(avc, "setCompletionHandler:", avc_setCompletion);
    IM(avc, "setCompletionWithItemsHandler:", avc_setCompletion2); IM(avc, "setExcludedActivityTypes:", nop1); IM(avc, "viewDidAppear:", avc_viewDidAppear);
    NEWCLASS(UIPopoverController, o, sizeof(struct uipop) - sizeof(void *));
    Class pop = RT_CLASS(UIPopoverController);
    IM(pop, "initWithContentViewController:", pop_init); IM(pop, "presentPopoverFromRect:inView:permittedArrowDirections:animated:", pop_present);
    IM(pop, "presentPopoverFromBarButtonItem:permittedArrowDirections:animated:", pop_presentFromItem); IM(pop, "dismissPopoverAnimated:", pop_dismiss);
    IM(pop, "isPopoverVisible", pop_visible); IM(pop, "contentViewController", pop_content); IM(pop, "setDelegate:", pop_setDelegate);
    IM(pop, "setPopoverContentSize:", nop1); IM(pop, "setPopoverContentSize:animated:", nop1L); IM(pop, "setPassthroughViews:", nop1);
    NEWCLASS(UIPopoverPresentationController, o, 0);
    NEWCLASS(UIDocumentInteractionController, o, sizeof(void *) * 2);
    Class dic = RT_CLASS(UIDocumentInteractionController);
    CM(dic, "interactionControllerWithURL:", doc_withURL); IM(dic, "presentOpenInMenuFromRect:inView:animated:", doc_present);
    IM(dic, "presentOptionsMenuFromRect:inView:animated:", doc_present); IM(dic, "setDelegate:", nop1); IM(dic, "setUTI:", nop1); IM(dic, "setName:", nop1);
    IM(dic, "presentPreviewAnimated:", no0); IM(dic, "dismissMenuAnimated:", nopL);
    NEWCLASS(UIImagePickerController, RT_CLASS(UINavigationController), sizeof(void *) * 2);
    Class ipc = RT_CLASS(UIImagePickerController);
    CM(ipc, "isSourceTypeAvailable:", ip_available); CM(ipc, "availableMediaTypesForSourceType:", ip_mediaTypes);
    IM(ipc, "setSourceType:", nopL); IM(ipc, "setDelegate:", nop1); IM(ipc, "setMediaTypes:", nop1); IM(ipc, "setAllowsEditing:", nopL);
    NEWCLASS(UIWebView, v, sizeof(struct uiweb) - sizeof(struct uiview));
    Class web = RT_CLASS(UIWebView);
    IM(web, "loadRequest:", web_load); IM(web, "loadHTMLString:baseURL:", web_loadHTML); IM(web, "setDelegate:", web_setDelegate);
    IM(web, "delegate", web_delegate); IM(web, "stringByEvaluatingJavaScriptFromString:", web_eval); IM(web, "stopLoading", nop);
    IM(web, "isLoading", no0); IM(web, "setScalesPageToFit:", nopL); IM(web, "scrollView", self0); IM(web, "reload", nop); IM(web, "goBack", nop);
    NEWCLASS(UIPickerView, v, sizeof(struct uipicker) - sizeof(struct uiview));
    Class pk = RT_CLASS(UIPickerView);
    IM(pk, "setDataSource:", pk_setDS); IM(pk, "setDelegate:", pk_setDelegate); IM(pk, "selectRow:inComponent:animated:", pk_select);
    IM(pk, "selectedRowInComponent:", pk_selected); IM(pk, "reloadAllComponents", nop); IM(pk, "reloadComponent:", nopL); IM(pk, "setShowsSelectionIndicator:", nopL);
    NEWCLASS(UIDatePicker, RT_CLASS(UIControl), sizeof(struct uidate) - sizeof(struct uicontrol));
    Class dp = RT_CLASS(UIDatePicker);
    IM(dp, "date", dp_date); IM(dp, "setDate:", dp_setDate); IM(dp, "setDate:animated:", dp_setDateAnim); IM(dp, "setDatePickerMode:", dp_setMode);
    IM(dp, "countDownDuration", dp_countdown); IM(dp, "setCountDownDuration:", dp_setCountdown); IM(dp, "setMinimumDate:", nop1);
    IM(dp, "setMaximumDate:", nop1); IM(dp, "setMinuteInterval:", nopL); IM(dp, "setLocale:", nop1); IM(dp, "setTimeZone:", nop1);
    NEWCLASS(UIAccessibilityElement, o, sizeof(void *) * 4);
    Class ae = RT_CLASS(UIAccessibilityElement);
    IM(ae, "initWithAccessibilityContainer:", ae_init); IM(ae, "setAccessibilityFrame:", nop1); IM(ae, "setAccessibilityLabel:", nop1);
    IM(ae, "setAccessibilityTraits:", nopL); IM(ae, "setAccessibilityValue:", nop1); IM(ae, "setAccessibilityHint:", nop1); IM(ae, "setIsAccessibilityElement:", nopL);
    NEWCLASS(NSLayoutConstraint, o, sizeof(struct nslc) - sizeof(void *));
    Class lc = RT_CLASS(NSLayoutConstraint);
    CM(lc, "constraintWithItem:attribute:relatedBy:toItem:attribute:multiplier:constant:", lc_make);
    CM(lc, "constraintsWithVisualFormat:options:metrics:views:", lc_visual); CM(lc, "activateConstraints:", lc_activate); CM(lc, "deactivateConstraints:", lc_activate);
    IM(lc, "constant", lc_constant); IM(lc, "setConstant:", lc_setConstant); IM(lc, "setActive:", nopL); IM(lc, "setPriority:", nopL); IM(lc, "setIdentifier:", nop1);
    NEWCLASS(UIVisualEffectView, v, 0);
    IM(RT_CLASS(UIVisualEffectView), "initWithEffect:", ve_initWithEffect); IM(RT_CLASS(UIVisualEffectView), "contentView", ve_contentView);
    NEWCLASS(UIBlurEffect, o, 0); CM(RT_CLASS(UIBlurEffect), "effectWithStyle:", blur_effect);
    NEWCLASS(UIStoryboard, o, 0); CM(RT_CLASS(UIStoryboard), "storyboardWithName:bundle:", sb_withName);
    NEWCLASS(UITraitCollection, o, 0);
    Class tc = RT_CLASS(UITraitCollection);
    IM(tc, "userInterfaceIdiom", tc_idiom); IM(tc, "horizontalSizeClass", tc_sizeClassH); IM(tc, "verticalSizeClass", tc_sizeClassV);
    IM(tc, "displayScale", tc_scale); IM(tc, "forceTouchCapability", zeroL); IM(tc, "userInterfaceStyle", zeroL);
    NEWCLASS(UIRefreshControl, RT_CLASS(UIControl), 0);
    IM(RT_CLASS(UIRefreshControl), "beginRefreshing", nop); IM(RT_CLASS(UIRefreshControl), "endRefreshing", nop);
}
