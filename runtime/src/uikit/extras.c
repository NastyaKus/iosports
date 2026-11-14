/*
 * UIKit additions found by tracing GarageBand: appearance proxies, input
 * modes, shake-to-edit, and StoreKit's payment queue (purchases are not
 * available: the queue exists, payments cannot be made).
 */
#include "uikit_internal.h"
const char *sel_getName(SEL s);
#include <stdlib.h>
#include <string.h>

RT_DEFINE_CLASS(UIAppearanceProxyImpl);
RT_DEFINE_CLASS(UITextInputMode);
RT_DEFINE_CLASS(SKPaymentQueue);
RT_DEFINE_CLASS(SKPayment);
RT_DEFINE_CLASS(SKProductsRequest);
RT_DECLARE_CLASS(UIView);
RT_DECLARE_CLASS(UIApplication);
RT_DECLARE_CLASS(UIBarItem);

/* ------------------------------------------------------- UIAppearance */
/* +appearance returns a proxy that accepts every customisation message and ignores it
 * (styling comes from the views' defaults); getters on the proxy return nil/0 */
static long ap_ignore(id self, SEL s) { (void)self; (void)s; return 0; }
static rt_bool ap_resolve(Class c, SEL s, SEL sel) {
    (void)s; rt_native_method(c, sel_getName(sel), (void *)ap_ignore, ""); return 1;
}
static rt_bool ap_responds(id self, SEL s, SEL sel) { (void)self; (void)s; (void)sel; return 1; }
static id g_appearance_proxy;
static id ap_appearance(Class c, SEL s) {
    (void)c; (void)s;
    if (!g_appearance_proxy) g_appearance_proxy = rt_alloc_instance(RT_CLASS(UIAppearanceProxyImpl), 0);
    return g_appearance_proxy;
}
static id ap_appearance1(Class c, SEL s, id a) { (void)a; return ap_appearance(c, s); }
static id ap_appearance2(Class c, SEL s, id a, id b) { (void)a; (void)b; return ap_appearance(c, s); }

/* ---------------------------------------------------- UITextInputMode */
struct tim { Class isa; };
static id tim_active(Class c, SEL s) {
    (void)c; (void)s;
    return RT_SEND0(id, (id)rt_class_named("NSArray"), "array");
}
static id tim_current(Class c, SEL s) { (void)c; (void)s; return NULL; }
static id tim_lang(id self, SEL s) { (void)self; (void)s; return rt_str("en-US"); }

/* ------------------------------------------------- UIApplication bits */
static long g_shake = 1;
static void app_setShake(id self, SEL s, long v) { (void)self; (void)s; g_shake = v & 0xff; }
static rt_bool app_shake(id self, SEL s) { (void)self; (void)s; return g_shake ? 1 : 0; }

/* -------------------------------------------------------- StoreKit */
struct skq { Class isa; id observers; };
static id g_skq;
static id skq_default(Class c, SEL s) {
    (void)s;
    if (!g_skq) { g_skq = rt_alloc_instance(c, 0); ((struct skq *)g_skq)->observers = RT_SEND0(id, RT_SEND0(id, (id)rt_class_named("NSMutableArray"), "alloc"), "init"); }
    return g_skq;
}
static rt_bool skq_canMake(Class c, SEL s) { (void)c; (void)s; return 0; }
static void skq_addObserver(id self, SEL s, id o) { (void)s; if (o) RT_SEND1(void, ((struct skq *)self)->observers, "addObject:", id, o); }
static void skq_removeObserver(id self, SEL s, id o) { (void)s; if (o) RT_SEND1(void, ((struct skq *)self)->observers, "removeObject:", id, o); }
static id skq_transactions(id self, SEL s) { (void)self; (void)s; return RT_SEND0(id, (id)rt_class_named("NSArray"), "array"); }
static void skq_noop1(id self, SEL s, id a) { (void)self; (void)s; (void)a; }
static void skq_noop(id self, SEL s) { (void)self; (void)s; }
/* products requests never answer with products: the delegate gets requestDidFinish-style failure */
struct skr { Class isa; id ids; id delegate; };
static id skr_init(id self, SEL s, id ids) { (void)s; ((struct skr *)self)->ids = objc_retain(ids); return self; }
static void skr_setDelegate(id self, SEL s, id d) { (void)s; ((struct skr *)self)->delegate = d; }
static id skr_delegate(id self, SEL s) { (void)s; return ((struct skr *)self)->delegate; }
static void skr_start(id self, SEL s) {
    (void)s; id d = ((struct skr *)self)->delegate;
    if (d && (RT_SEND1(long, d, "respondsToSelector:", SEL, rt_sel("request:didFailWithError:")) & 0xff)) {
        id err = RT_SEND3(id, (id)rt_class_named("NSError"), "errorWithDomain:code:userInfo:", id, rt_str("SKErrorDomain"), long, 0, id, NULL);
        RT_SEND2(void, d, "request:didFailWithError:", id, self, id, err);
    }
}

/* -------------------------------------------- small UIKit additions */
static long app_layoutDirection(id self, SEL s) { (void)self; (void)s; return 0; }   /* UIUserInterfaceLayoutDirectionLeftToRight */
static void iv_setContentStretch(id self, SEL s, CGRect r) { (void)self; (void)s; (void)r; }
struct cgctx *iosrt_UIGraphicsGetCurrentContext(void);
/* snapshot: the view's layer tree drawn into the current UIGraphics context, scaled to rect */
static rt_bool v_drawHierarchy(id self, SEL s, CGRect r, long after) {
    (void)s; (void)after;
    struct cgctx *c = iosrt_UIGraphicsGetCurrentContext();
    if (!c) return 0;
    CGRect b = RT_SEND0(CGRect, self, "bounds");
    iosrt_CGContextSaveGState(c);
    iosrt_CGContextTranslateCTM(c, r.origin.x, r.origin.y);
    if (b.size.width > 0 && b.size.height > 0) iosrt_CGContextScaleCTM(c, r.size.width / b.size.width, r.size.height / b.size.height);
    RT_SEND1(void, RT_SEND0(id, self, "layer"), "renderInContext:", void *, c);
    iosrt_CGContextRestoreGState(c);
    return 1;
}
/* maskView: its layer becomes the view layer's mask */
static const char k_maskView = 0;
void objc_setAssociatedObject(id obj, const void *key, id val, unsigned long policy);
id objc_getAssociatedObject(id obj, const void *key);
static void v_setMaskView(id self, SEL s, id mv) {
    (void)s; objc_setAssociatedObject(self, &k_maskView, mv, 1 /* retain, nonatomic */);
    RT_SEND1(void, RT_SEND0(id, self, "layer"), "setMask:", id, mv ? RT_SEND0(id, mv, "layer") : NULL);
}
static id v_maskView(id self, SEL s) { (void)s; return objc_getAssociatedObject(self, &k_maskView); }
static id scr_traits(id self, SEL s) { (void)self; (void)s; return objc_autorelease(RT_SEND0(id, (id)rt_class_named("UITraitCollection"), "new")); }
static id tc_with1(Class c, SEL s, long v) { (void)s; (void)v; return objc_autorelease(RT_SEND0(id, (id)c, "new")); }
static id tc_withTraits(Class c, SEL s, id arr) { (void)s; (void)arr; return objc_autorelease(RT_SEND0(id, (id)c, "new")); }
static rt_bool tc_contains(id self, SEL s, id other) { (void)self; (void)s; (void)other; return 1; }
void iosrt_UIGraphicsBeginImageContextWithOptions(CGSize sz, long opaque, double scale);
id iosrt_UIGraphicsGetImageFromCurrentImageContext(void);
void iosrt_UIGraphicsEndImageContext(void);
static id v_snapshotView(id self, SEL s, long after) {
    (void)s; CGRect b = RT_SEND0(CGRect, self, "bounds");
    iosrt_UIGraphicsBeginImageContextWithOptions(b.size, 0, 0);
    v_drawHierarchy(self, NULL, (CGRect){ { 0, 0 }, b.size }, after);
    id img = iosrt_UIGraphicsGetImageFromCurrentImageContext();
    iosrt_UIGraphicsEndImageContext();
    id iv = RT_SEND1(id, RT_SEND0(id, (id)rt_class_named("UIImageView"), "alloc"), "initWithImage:", id, img);
    RT_SEND1(void, iv, "setFrame:", CGRect, RT_SEND0(CGRect, self, "frame"));
    return objc_autorelease(iv);
}
static id v_resizableSnapshot(id self, SEL s, CGRect r, long after, UIEdgeInsets ins) { (void)r; (void)ins; return v_snapshotView(self, s, after); }
static void ve_setEffect(id self, SEL s, id e) { (void)self; (void)s; (void)e; }
static id vc_modalViewController(id self, SEL s) { (void)s; return RT_SEND0(id, self, "presentedViewController"); }

void rt_uikit_extras_init(void) {
    Class o = rt_class_named("NSObject");
    NEWCLASS(UIAppearanceProxyImpl, o, 0);
    Class ap = RT_CLASS(UIAppearanceProxyImpl);
    CM(ap, "resolveInstanceMethod:", ap_resolve); IM(ap, "respondsToSelector:", ap_responds);
    Class v = RT_CLASS(UIView);
    CM(v, "appearance", ap_appearance); CM(v, "appearanceWhenContainedIn:", ap_appearance1);
    CM(v, "appearanceWhenContainedInInstancesOfClasses:", ap_appearance1); CM(v, "appearanceForTraitCollection:", ap_appearance1);
    CM(v, "appearanceForTraitCollection:whenContainedIn:", ap_appearance2);
    Class bi = rt_class_named("UIBarItem");
    if (bi) { CM(bi, "appearance", ap_appearance); CM(bi, "appearanceWhenContainedIn:", ap_appearance1); CM(bi, "appearanceWhenContainedInInstancesOfClasses:", ap_appearance1); }

    NEWCLASS(UITextInputMode, o, sizeof(struct tim) - sizeof(void *));
    Class t = RT_CLASS(UITextInputMode);
    CM(t, "activeInputModes", tim_active); CM(t, "currentInputMode", tim_current); IM(t, "primaryLanguage", tim_lang);

    Class app = RT_CLASS(UIApplication);
    IM(app, "setApplicationSupportsShakeToEdit:", app_setShake); IM(app, "applicationSupportsShakeToEdit", app_shake);
    IM(app, "userInterfaceLayoutDirection", app_layoutDirection);
    IM(v, "setContentStretch:", iv_setContentStretch);
    IM(v, "drawViewHierarchyInRect:afterScreenUpdates:", v_drawHierarchy);
    IM(rt_class_named("UIViewController"), "modalViewController", vc_modalViewController);
    IM(v, "setMaskView:", v_setMaskView); IM(v, "maskView", v_maskView);
    IM(v, "snapshotViewAfterScreenUpdates:", v_snapshotView);
    IM(v, "resizableSnapshotViewFromRect:afterScreenUpdates:withCapInsets:", v_resizableSnapshot);
    if (rt_class_named("UIVisualEffectView")) IM(rt_class_named("UIVisualEffectView"), "setEffect:", ve_setEffect);
    IM(rt_class_named("UIScreen"), "traitCollection", scr_traits);
    Class tc = rt_class_named("UITraitCollection");
    if (tc) {
        static const char *const with1[] = { "traitCollectionWithForceTouchCapability:", "traitCollectionWithUserInterfaceIdiom:",
            "traitCollectionWithHorizontalSizeClass:", "traitCollectionWithVerticalSizeClass:", "traitCollectionWithLayoutDirection:",
            "traitCollectionWithUserInterfaceStyle:" };
        for (size_t i = 0; i < sizeof with1 / sizeof *with1; i++) CM(tc, with1[i], tc_with1);
        CM(tc, "traitCollectionWithDisplayScale:", tc_with1);
        CM(tc, "traitCollectionWithTraitsFromCollections:", tc_withTraits);
        IM(tc, "containsTraitsInCollection:", tc_contains);
    }

    NEWCLASS(SKPaymentQueue, o, sizeof(struct skq) - sizeof(void *));
    Class q = RT_CLASS(SKPaymentQueue);
    CM(q, "defaultQueue", skq_default); CM(q, "canMakePayments", skq_canMake);
    IM(q, "addTransactionObserver:", skq_addObserver); IM(q, "removeTransactionObserver:", skq_removeObserver);
    IM(q, "transactions", skq_transactions); IM(q, "addPayment:", skq_noop1); IM(q, "finishTransaction:", skq_noop1);
    IM(q, "restoreCompletedTransactions", skq_noop);
    NEWCLASS(SKPayment, o, 0);
    NEWCLASS(SKProductsRequest, o, sizeof(struct skr) - sizeof(void *));
    Class r = RT_CLASS(SKProductsRequest);
    IM(r, "initWithProductIdentifiers:", skr_init); IM(r, "setDelegate:", skr_setDelegate); IM(r, "delegate", skr_delegate);
    IM(r, "start", skr_start); IM(r, "cancel", skq_noop);
}
