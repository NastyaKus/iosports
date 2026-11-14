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
