/*
 * Storyboards and segues.
 *
 * A compiled storyboard (Name.storyboardc) is a folder:
 *   Info.plist  UIStoryboardDesignatedEntryPointIdentifier,
 *               UIViewControllerIdentifiersToNibNames { identifier: nib }
 *   <scene>.nib one nib per scene. Its File's Owner is a scene object that
 *               receives the controller through a "sceneViewController"
 *               outlet; the storyboard itself is the external object
 *               "UIStoryboardPlaceholder".
 *   <view>.nib  the controller's view, loaded lazily (the controller's
 *               UINibName) with UIExternalObjectsTableForViewLoading as
 *               external objects (segue templates, upstream objects).
 * Controllers carry UIStoryboardSegueTemplates; buttons trigger them with
 * a "perform:" action. Templates build a UIStoryboardSegue on demand.
 */
#include "uikit_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

RT_DECLARE_CLASS(NSObject);
RT_DECLARE_CLASS(UIStoryboard);
RT_DECLARE_CLASS(UIViewController);
RT_DEFINE_CLASS(UIStoryboardScene);
RT_DEFINE_CLASS(UIStoryboardSegueTemplate);
RT_DEFINE_CLASS(UIStoryboardShowSegueTemplate);
RT_DEFINE_CLASS(UIStoryboardShowDetailSegueTemplate);
RT_DEFINE_CLASS(UIStoryboardPushSegueTemplate);
RT_DEFINE_CLASS(UIStoryboardModalSegueTemplate);
RT_DEFINE_CLASS(UIStoryboardPresentationSegueTemplate);
RT_DEFINE_CLASS(UIStoryboardPopoverSegueTemplate);
RT_DEFINE_CLASS(UIStoryboardReplaceSegueTemplate);
RT_DEFINE_CLASS(UIStoryboardEmbedSegueTemplate);
RT_DEFINE_CLASS(UIStoryboardUnwindSegueTemplate);
RT_DEFINE_CLASS(UIStoryboardSegue);
RT_DEFINE_CLASS(UIStoryboardShowSegue);
RT_DEFINE_CLASS(UIStoryboardShowDetailSegue);
RT_DEFINE_CLASS(UIStoryboardPushSegue);
RT_DEFINE_CLASS(UIStoryboardModalSegue);
RT_DEFINE_CLASS(UIStoryboardPresentationSegue);
RT_DEFINE_CLASS(UIStoryboardPopoverSegue);
RT_DEFINE_CLASS(UIStoryboardReplaceSegue);
RT_DEFINE_CLASS(UIStoryboardEmbedSegue);
#define VC(x) ((struct uivc *)(x))
#define V(x) ((struct uiview *)(x))

id rt_plist_parse(const uint8_t *data, size_t len);

/* ============================================================ UIStoryboard */
struct uisb { Class isa; id name, bundle, dir, info; };
#define SB(x) ((struct uisb *)(x))
static char *read_all(const char *path, size_t *n) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = len > 0 ? malloc((size_t)len) : NULL;
    if (b && fread(b, 1, (size_t)len, f) != (size_t)len) { free(b); b = NULL; }
    fclose(f); *n = b ? (size_t)len : 0; return b;
}
static int is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
/* Name.storyboardc in the bundle (device variants and localization folders first) */
static int find_storyboard(id bundle, const char *name, char *out, size_t cap) {
    if (!bundle) bundle = RT_SEND0(id, (id)rt_class_named("NSBundle"), "mainBundle");
    const char *dir = rt_str_utf8(RT_SEND0(id, bundle, "bundlePath"));
    const char *subs[] = { "", "Base.lproj/", "en.lproj/", NULL }, *idioms[] = { "~iphone", "", NULL };
    for (int i = 0; subs[i]; i++) for (int j = 0; idioms[j]; j++) {
        snprintf(out, cap, "%s/%s%s%s.storyboardc", dir, subs[i], name, idioms[j]);
        if (is_dir(out)) return 1;
    }
    return 0;
}
static id sb_withName(Class c, SEL s, id name, id bundle) {
    (void)s; char path[4096];
    if (!name || !find_storyboard(bundle, rt_str_utf8(name), path, sizeof path)) {
        rt_warn("UIStoryboard: storyboard '%s' not found", name ? rt_str_utf8(name) : "?");
        return NULL;
    }
    char ip[4200]; snprintf(ip, sizeof ip, "%s/Info.plist", path);
    size_t n; char *b = read_all(ip, &n);
    id info = b ? rt_plist_parse((const uint8_t *)b, n) : NULL;
    free(b);
    id sb = rt_alloc_instance(c, 0);
    SB(sb)->name = RT_SEND0(id, name, "copy"); SB(sb)->bundle = objc_retain(bundle);
    SB(sb)->dir = rt_str_new(path); SB(sb)->info = info;
    return objc_autorelease(sb);
}
static void sb_dealloc(id self, SEL s) {
    (void)s; objc_release(SB(self)->name); objc_release(SB(self)->bundle); objc_release(SB(self)->dir); objc_release(SB(self)->info);
    rt_dealloc_instance(self);
}
const char *rt_storyboard_dir(id sb) { return sb && rt_is_kind_of(sb, RT_CLASS(UIStoryboard)) && SB(sb)->dir ? rt_str_utf8(SB(sb)->dir) : NULL; }
/* the scene object: File's Owner of a scene nib */
struct scene { Class isa; id vc; };
static void scene_setVC(id self, SEL s, id vc) { (void)s; ((struct scene *)self)->vc = vc; }
static id scene_vc(id self, SEL s) { (void)s; return ((struct scene *)self)->vc; }
static id sb_instantiateWithIdentifier(id self, SEL s, id ident) {
    (void)s;
    if (!ident) return NULL;
    id map = SB(self)->info ? rt_dict_get(SB(self)->info, rt_str("UIViewControllerIdentifiersToNibNames")) : NULL;
    id nibName = map ? rt_dict_get(map, ident) : NULL;
    if (!nibName) nibName = ident;
    char path[4200]; snprintf(path, sizeof path, "%s/%s.nib", rt_str_utf8(SB(self)->dir), rt_str_utf8(nibName));
    id scene = objc_autorelease(rt_alloc_instance(RT_CLASS(UIStoryboardScene), 0));
    id externals = objc_autorelease(RT_SEND0(id, RT_SEND0(id, (id)rt_class_named("NSMutableDictionary"), "alloc"), "init"));
    RT_SEND2(void, externals, "setObject:forKey:", id, self, id, rt_str("UIStoryboardPlaceholder"));
    id top = rt_nib_instantiate(path, scene, externals);
    id vc = ((struct scene *)scene)->vc;
    if (!vc) {   /* older layouts: the controller is the first top-level object */
        for (size_t i = 0; top && i < rt_array_count(top); i++) if (rt_is_kind_of(rt_array_at(top, i), RT_CLASS(UIViewController))) { vc = rt_array_at(top, i); break; }
    }
    if (!vc) { rt_warn("UIStoryboard %s: scene '%s' has no view controller", rt_str_utf8(SB(self)->name), rt_str_utf8(ident)); return NULL; }
    if (!VC(vc)->storyboard) VC(vc)->storyboard = objc_retain(self);
    if (!VC(vc)->sbIdentifier) VC(vc)->sbIdentifier = RT_SEND0(id, ident, "copy");
    return vc;   /* kept alive by the autorelease pool / the top-level array */
}
static id sb_instantiateInitial(id self, SEL s) {
    id entry = SB(self)->info ? rt_dict_get(SB(self)->info, rt_str("UIStoryboardDesignatedEntryPointIdentifier")) : NULL;
    if (!entry) { rt_warn("UIStoryboard %s has no initial view controller", rt_str_utf8(SB(self)->name)); return NULL; }
    return sb_instantiateWithIdentifier(self, s, entry);
}
/* iOS 13 creator variants: the block is not called (no custom init coders) */
static id sb_instantiateInitialCreator(id self, SEL s, void *creator) { (void)creator; return sb_instantiateInitial(self, s); }
static id sb_instantiateWithIdentifierCreator(id self, SEL s, id ident, void *creator) { (void)creator; return sb_instantiateWithIdentifier(self, s, ident); }

struct segue { Class isa; id ident, src, dest; void *handler; id container; long animates; };
#define SG(x) ((struct segue *)(x))

/* ======================================================= segue templates */
struct tmpl {
    Class isa; id identifier, destId, segueClassName, actionName; id source; id container;
    long performOnLoad, animates, presentationStyle, transitionStyle;
};
#define TM(x) ((struct tmpl *)(x))
static id dec(id c, const char *k) { return RT_SEND1(id, c, "decodeObjectForKey:", id, rt_str(k)); }
static int has(id c, const char *k) { return RT_SEND1(long, c, "containsValueForKey:", id, rt_str(k)) & 0xff; }
static id tmpl_initWithCoder(id self, SEL s, id coder) {
    (void)s; struct tmpl *t = TM(self);
    t->identifier = objc_retain(dec(coder, "UIIdentifier"));
    t->destId = objc_retain(dec(coder, "UIDestinationViewControllerIdentifier"));
    t->segueClassName = objc_retain(dec(coder, "UISegueClassName"));
    t->actionName = objc_retain(dec(coder, "UIActionName"));
    t->container = dec(coder, "UIContainerView");
    t->performOnLoad = RT_SEND1(long, coder, "decodeBoolForKey:", id, rt_str("UIPerformOnViewLoad")) & 0xff;
    t->animates = has(coder, "UIAnimates") ? RT_SEND1(long, coder, "decodeBoolForKey:", id, rt_str("UIAnimates")) & 0xff : 1;
    t->presentationStyle = has(coder, "UIModalPresentationStyle") ? RT_SEND1(long, coder, "decodeIntegerForKey:", id, rt_str("UIModalPresentationStyle")) : -1;
    t->transitionStyle = has(coder, "UIModalTransitionStyle") ? RT_SEND1(long, coder, "decodeIntegerForKey:", id, rt_str("UIModalTransitionStyle")) : -1;
    return self;
}
static void tmpl_dealloc(id self, SEL s) {
    (void)s; struct tmpl *t = TM(self);
    objc_release(t->identifier); objc_release(t->destId); objc_release(t->segueClassName); objc_release(t->actionName);
    rt_dealloc_instance(self);
}
static id tmpl_identifier(id self, SEL s) { (void)s; return TM(self)->identifier; }
static id tmpl_viewController(id self, SEL s) { (void)s; return TM(self)->source; }
static void tmpl_setViewController(id self, SEL s, id vc) { (void)s; TM(self)->source = vc; }
static void tmpl_setContainer(id self, SEL s, id v) { (void)s; TM(self)->container = v; }
static id tmpl_container(id self, SEL s) { (void)s; return TM(self)->container; }
void rt_segue_template_set_source(id t, id vc) { if (t && rt_is_kind_of(t, RT_CLASS(UIStoryboardSegueTemplate))) TM(t)->source = vc; }
int rt_segue_template_is_embed(id t) { return t && rt_is_kind_of(t, RT_CLASS(UIStoryboardEmbedSegueTemplate)) && TM(t)->performOnLoad; }
id rt_segue_template_identifier(id t) { return t && rt_is_kind_of(t, RT_CLASS(UIStoryboardSegueTemplate)) ? TM(t)->identifier : NULL; }
static Class default_segue_class(id t) {
    if (rt_is_kind_of(t, RT_CLASS(UIStoryboardShowDetailSegueTemplate))) return RT_CLASS(UIStoryboardShowDetailSegue);
    if (rt_is_kind_of(t, RT_CLASS(UIStoryboardShowSegueTemplate))) return RT_CLASS(UIStoryboardShowSegue);
    if (rt_is_kind_of(t, RT_CLASS(UIStoryboardPushSegueTemplate))) return RT_CLASS(UIStoryboardPushSegue);
    if (rt_is_kind_of(t, RT_CLASS(UIStoryboardPopoverSegueTemplate))) return RT_CLASS(UIStoryboardPopoverSegue);
    if (rt_is_kind_of(t, RT_CLASS(UIStoryboardPresentationSegueTemplate))) return RT_CLASS(UIStoryboardPresentationSegue);
    if (rt_is_kind_of(t, RT_CLASS(UIStoryboardModalSegueTemplate))) return RT_CLASS(UIStoryboardModalSegue);
    if (rt_is_kind_of(t, RT_CLASS(UIStoryboardReplaceSegueTemplate))) return RT_CLASS(UIStoryboardReplaceSegue);
    if (rt_is_kind_of(t, RT_CLASS(UIStoryboardEmbedSegueTemplate))) return RT_CLASS(UIStoryboardEmbedSegue);
    return RT_CLASS(UIStoryboardSegue);
}
/* build and run the segue; returns 1 if it ran */
int rt_segue_template_perform(id t, id sender) {
    struct tmpl *tm = TM(t);
    id src = tm->source;
    if (!src) { rt_warn("segue '%s' has no source view controller", tm->identifier ? rt_str_utf8(tm->identifier) : "?"); return 0; }
    if (rt_is_kind_of(t, RT_CLASS(UIStoryboardUnwindSegueTemplate))) {
        /* unwind: find a controller up the presentation/navigation chain that implements the action, then go back to it */
        SEL action = tm->actionName ? rt_sel(rt_str_utf8(tm->actionName)) : NULL;
        for (id vc = src; vc; vc = VC(vc)->parent ? VC(vc)->parent : VC(vc)->presenting) {
            id nav = RT_SEND0(id, vc, "navigationController");
            id stack = nav ? RT_SEND0(id, nav, "viewControllers") : NULL;
            for (size_t i = stack ? rt_array_count(stack) : 0; i-- > 0;) {
                id cand = rt_array_at(stack, i);
                if (cand != src && action && rt_responds(cand, action)) {
                    id seg = RT_SEND3(id, RT_SEND0(id, (id)RT_CLASS(UIStoryboardSegue), "alloc"), "initWithIdentifier:source:destination:", id, tm->identifier, id, src, id, cand);
                    RT_SEND2(void, src, "prepareForSegue:sender:", id, seg, id, sender);
                    RT_SEND1(void, cand, rt_str_utf8(tm->actionName), id, seg);
                    RT_SEND2(id, nav, "popToViewController:animated:", id, cand, long, tm->animates);
                    objc_release(seg);
                    return 1;
                }
            }
            if (vc != src && action && rt_responds(vc, action)) {
                id seg = RT_SEND3(id, RT_SEND0(id, (id)RT_CLASS(UIStoryboardSegue), "alloc"), "initWithIdentifier:source:destination:", id, tm->identifier, id, src, id, vc);
                RT_SEND2(void, src, "prepareForSegue:sender:", id, seg, id, sender);
                RT_SEND1(void, vc, rt_str_utf8(tm->actionName), id, seg);
                RT_SEND2(void, vc, "dismissViewControllerAnimated:completion:", long, tm->animates, void *, NULL);
                objc_release(seg);
                return 1;
            }
        }
        rt_warn("unwind segue %s: no destination implements it", tm->actionName ? rt_str_utf8(tm->actionName) : "?");
        return 0;
    }
    if (tm->identifier && rt_responds(src, rt_sel("shouldPerformSegueWithIdentifier:sender:")) && !rt_is_kind_of(t, RT_CLASS(UIStoryboardEmbedSegueTemplate)))
        if (!(RT_SEND2(long, src, "shouldPerformSegueWithIdentifier:sender:", id, tm->identifier, id, sender) & 0xff)) return 0;
    id sb = VC(src)->storyboard;
    if (!sb) for (id p = VC(src)->parent; p && !sb; p = VC(p)->parent) sb = VC(p)->storyboard;
    id dest = sb && tm->destId ? RT_SEND1(id, sb, "instantiateViewControllerWithIdentifier:", id, tm->destId) : NULL;
    if (!dest) { rt_warn("segue '%s': destination '%s' could not be instantiated", tm->identifier ? rt_str_utf8(tm->identifier) : "?", tm->destId ? rt_str_utf8(tm->destId) : "?"); return 0; }
    Class sc = tm->segueClassName ? rt_class_named(rt_str_utf8(tm->segueClassName)) : NULL;
    if (!sc) sc = default_segue_class(t);
    if (tm->presentationStyle >= 0) VC(dest)->modalStyle = tm->presentationStyle;
    if (tm->transitionStyle >= 0) VC(dest)->transitionStyle = tm->transitionStyle;
    id seg = RT_SEND3(id, RT_SEND0(id, (id)sc, "alloc"), "initWithIdentifier:source:destination:", id, tm->identifier, id, src, id, dest);
    if (rt_is_kind_of(seg, RT_CLASS(UIStoryboardSegue))) { SG(seg)->container = tm->container; SG(seg)->animates = tm->animates; }
    RT_SEND2(void, src, "prepareForSegue:sender:", id, seg, id, sender);
    RT_SEND0(void, seg, "perform");
    objc_release(seg);
    return 1;
}
static void tmpl_perform(id self, SEL s, id sender) { (void)s; rt_segue_template_perform(self, sender); }

/* ================================================================ segues */
static id seg_init(id self, SEL s, id ident, id src, id dest) {
    (void)s; struct segue *g = SG(self);
    g->ident = ident ? RT_SEND0(id, ident, "copy") : NULL; g->src = objc_retain(src); g->dest = objc_retain(dest); g->animates = 1;
    return self;
}
static id seg_withHandler(Class c, SEL s, id ident, id src, id dest, void *handler) {
    (void)s; id g = seg_init(rt_alloc_instance(c, 0), NULL, ident, src, dest);
    SG(g)->handler = handler ? _Block_copy(handler) : NULL;
    return objc_autorelease(g);
}
static void seg_dealloc(id self, SEL s) {
    (void)s; struct segue *g = SG(self);
    objc_release(g->ident); objc_release(g->src); objc_release(g->dest); if (g->handler) _Block_release(g->handler);
    rt_dealloc_instance(self);
}
static id seg_ident(id self, SEL s) { (void)s; return SG(self)->ident; }
static id seg_src(id self, SEL s) { (void)s; return SG(self)->src; }
static id seg_dest(id self, SEL s) { (void)s; return SG(self)->dest; }
static void seg_perform(id self, SEL s) {
    (void)s; struct segue *g = SG(self);
    if (g->handler) ((void (*)(void *))((struct rt_block *)g->handler)->invoke)(g->handler);
}
static void seg_show(id self, SEL s) { (void)s; RT_SEND2(void, SG(self)->src, "showViewController:sender:", id, SG(self)->dest, id, NULL); }
static void seg_showDetail(id self, SEL s) { (void)s; RT_SEND2(void, SG(self)->src, "showDetailViewController:sender:", id, SG(self)->dest, id, NULL); }
static void seg_push(id self, SEL s) {
    (void)s; id nav = RT_SEND0(id, SG(self)->src, "navigationController");
    if (nav) RT_SEND2(void, nav, "pushViewController:animated:", id, SG(self)->dest, long, SG(self)->animates);
    else seg_show(self, s);
}
static void seg_present(id self, SEL s) { (void)s; RT_SEND3(void, SG(self)->src, "presentViewController:animated:completion:", id, SG(self)->dest, long, SG(self)->animates, void *, NULL); }
static void seg_replace(id self, SEL s) { seg_showDetail(self, s); }
/* embed: the destination becomes a child shown inside the container view */
static void seg_embed(id self, SEL s) {
    (void)s; struct segue *g = SG(self);
    id container = g->container ? g->container : RT_SEND0(id, g->src, "view");
    RT_SEND1(void, g->src, "addChildViewController:", id, g->dest);
    id v = RT_SEND0(id, g->dest, "view");
    rt_view_set_frame(v, rt_view_bounds(container)); V(v)->autoresizing = 2 | 16;
    rt_view_add_subview(container, v);
    RT_SEND1(void, g->dest, "didMoveToParentViewController:", id, g->src);
}

/* ============================================================== init */
void rt_storyboard_init(void) {
    Class o = RT_CLASS(NSObject);
    NEWCLASS(UIStoryboard, o, sizeof(struct uisb) - sizeof(void *));
    Class sb = RT_CLASS(UIStoryboard);
    CM(sb, "storyboardWithName:bundle:", sb_withName); IM(sb, "dealloc", sb_dealloc);
    IM(sb, "instantiateInitialViewController", sb_instantiateInitial); IM(sb, "instantiateViewControllerWithIdentifier:", sb_instantiateWithIdentifier);
    IM(sb, "instantiateInitialViewControllerWithCreator:", sb_instantiateInitialCreator);
    IM(sb, "instantiateViewControllerWithIdentifier:creator:", sb_instantiateWithIdentifierCreator);
    NEWCLASS(UIStoryboardScene, o, sizeof(struct scene) - sizeof(void *));
    IM(RT_CLASS(UIStoryboardScene), "setSceneViewController:", scene_setVC); IM(RT_CLASS(UIStoryboardScene), "sceneViewController", scene_vc);

    NEWCLASS(UIStoryboardSegueTemplate, o, sizeof(struct tmpl) - sizeof(void *));
    Class t = RT_CLASS(UIStoryboardSegueTemplate);
    IM(t, "initWithCoder:", tmpl_initWithCoder); IM(t, "dealloc", tmpl_dealloc); IM(t, "identifier", tmpl_identifier);
    IM(t, "viewController", tmpl_viewController); IM(t, "setViewController:", tmpl_setViewController);
    IM(t, "containerView", tmpl_container); IM(t, "setContainerView:", tmpl_setContainer);
    IM(t, "perform:", tmpl_perform); IM(t, "_perform:", tmpl_perform);
    NEWCLASS(UIStoryboardShowSegueTemplate, t, 0); NEWCLASS(UIStoryboardShowDetailSegueTemplate, t, 0); NEWCLASS(UIStoryboardPushSegueTemplate, t, 0);
    NEWCLASS(UIStoryboardModalSegueTemplate, t, 0); NEWCLASS(UIStoryboardPresentationSegueTemplate, t, 0); NEWCLASS(UIStoryboardPopoverSegueTemplate, t, 0);
    NEWCLASS(UIStoryboardReplaceSegueTemplate, t, 0); NEWCLASS(UIStoryboardEmbedSegueTemplate, t, 0); NEWCLASS(UIStoryboardUnwindSegueTemplate, t, 0);

    NEWCLASS(UIStoryboardSegue, o, sizeof(struct segue) - sizeof(void *));
    Class g = RT_CLASS(UIStoryboardSegue);
    IM(g, "initWithIdentifier:source:destination:", seg_init); IM(g, "dealloc", seg_dealloc);
    CM(g, "segueWithIdentifier:source:destination:performHandler:", seg_withHandler);
    IM(g, "identifier", seg_ident); IM(g, "sourceViewController", seg_src); IM(g, "destinationViewController", seg_dest); IM(g, "perform", seg_perform);
    NEWCLASS(UIStoryboardShowSegue, g, 0); IM(RT_CLASS(UIStoryboardShowSegue), "perform", seg_show);
    NEWCLASS(UIStoryboardShowDetailSegue, g, 0); IM(RT_CLASS(UIStoryboardShowDetailSegue), "perform", seg_showDetail);
    NEWCLASS(UIStoryboardPushSegue, g, 0); IM(RT_CLASS(UIStoryboardPushSegue), "perform", seg_push);
    NEWCLASS(UIStoryboardModalSegue, g, 0); IM(RT_CLASS(UIStoryboardModalSegue), "perform", seg_present);
    NEWCLASS(UIStoryboardPresentationSegue, g, 0); IM(RT_CLASS(UIStoryboardPresentationSegue), "perform", seg_present);
    NEWCLASS(UIStoryboardPopoverSegue, g, 0); IM(RT_CLASS(UIStoryboardPopoverSegue), "perform", seg_present);
    NEWCLASS(UIStoryboardReplaceSegue, g, 0); IM(RT_CLASS(UIStoryboardReplaceSegue), "perform", seg_replace);
    NEWCLASS(UIStoryboardEmbedSegue, g, 0); IM(RT_CLASS(UIStoryboardEmbedSegue), "perform", seg_embed);
}
