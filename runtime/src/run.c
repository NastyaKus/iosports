/*
 * Launcher: runtime initialisation, image loading, ObjC image mapping,
 * +load / static initialisers, guest main() on a dedicated thread, guest
 * termination, crash reporting, and the compatibility report.
 */
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <ucontext.h>

void rt_libsystem_init(void);
void rt_traps_seal(void);
void rt_run_atexit(void);
void rt_set_args(int argc, char **argv, char **envp);
void rt_foundation_set_args(int argc, char **argv);
extern char **environ;

static pthread_once_t init_once = PTHREAD_ONCE_INIT;
static void runtime_init(void) {
    rt_libsystem_init();
    rt_objc_init();
    rt_exports_init();
    rt_foundation_init();
    rt_uikit_init();
    rt_audio_init();
}
RT_EXPORT void iosrt_init(void) { pthread_once(&init_once, runtime_init); }

/* --------------------------------------------------------- guest exit */
static pthread_t guest_main;
static int guest_main_valid;
static sigjmp_buf guest_jmp;
static int guest_code;
static char guest_reason[1024];
int rt_is_guest_main_thread(void) { return guest_main_valid && pthread_equal(pthread_self(), guest_main); }

_Noreturn void rt_guest_exit(int code, const char *reason) {
    guest_code = code;
    if (reason) {
        snprintf(guest_reason, sizeof guest_reason, "%s", reason);
        rt_warn("guest terminated: %s", reason);
    }
    if (rt_is_guest_main_thread()) siglongjmp(guest_jmp, 1);
    /* secondary guest thread: the whole guest has to go */
    if (rt_plat->on_exit) rt_plat->on_exit(code, reason);
    _exit(code);
}

/* ------------------------------------------------------- crash handler */
static void crash_handler(int sig, siginfo_t *si, void *uc_) {
    ucontext_t *uc = uc_;
    uint64_t pc = (uint64_t)uc->uc_mcontext.pc, lr = (uint64_t)uc->uc_mcontext.regs[30];
    char buf[1024]; int n;
    uint64_t off = 0;
    const char *sym = rt_image_symbolize(pc, &off);
    n = snprintf(buf, sizeof buf, "[iosrt] guest crashed: signal %d at address %p, pc=0x%llx (%s+0x%llx)",
                 sig, si->si_addr, (unsigned long long)pc, sym ? sym : "runtime", (unsigned long long)off);
    sym = rt_image_symbolize(lr, &off);
    n += snprintf(buf + n, sizeof buf - (size_t)n, ", lr=0x%llx (%s+0x%llx)\n", (unsigned long long)lr, sym ? sym : "runtime", (unsigned long long)off);
    if (write(2, buf, (size_t)n) < 0) { /* ignore */ }
    if (rt_plat->on_exit) rt_plat->on_exit(128 + sig, buf);
    _exit(128 + sig);
}
static void install_crash_handler(void) {
    static char altstack[65536];
    stack_t ss = { .ss_sp = altstack, .ss_size = sizeof altstack, .ss_flags = 0 };
    sigaltstack(&ss, NULL);
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = crash_handler; sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGSEGV, &sa, NULL); sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL); sigaction(SIGFPE, &sa, NULL);
}

/* -------------------------------------------------- bundle resolution */
static int is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
static char *resolve_executable(const char *path, char **bundle) {
    *bundle = NULL;
    if (!is_dir(path)) return rt_xstrdup(path);
    *bundle = rt_xstrdup(path);
    char ip[4096]; snprintf(ip, sizeof ip, "%s/Info.plist", path);
    FILE *f = fopen(ip, "rb");
    char *exe = NULL;
    if (f) {
        rt_sb sb; rt_sb_init(&sb); char b[4096]; size_t k;
        while ((k = fread(b, 1, sizeof b, f)) > 0) rt_sb_append(&sb, b, k);
        fclose(f);
        void *pool = objc_autoreleasePoolPush();
        id d = rt_plist_parse((const uint8_t *)sb.buf, sb.len);
        id e = d ? rt_dict_get(d, rt_str("CFBundleExecutable")) : NULL;
        if (e) exe = rt_xstrdup(rt_str_utf8(e));
        objc_release(d);
        objc_autoreleasePoolPop(pool);
        free(sb.buf);
    }
    if (!exe) {   /* Foo.app -> Foo */
        const char *b = strrchr(path, '/'); b = b ? b + 1 : path;
        exe = rt_xstrdup(b);
        char *dot = strstr(exe, ".app"); if (dot) *dot = 0;
    }
    char full[4096]; snprintf(full, sizeof full, "%s/%s", path, exe);
    free(exe);
    return rt_xstrdup(full);
}

/* --------------------------------------------------------- guest thread */
struct guest_args { struct rt_image *img; int argc; char **argv; };
static void *guest_thread(void *p) {
    struct guest_args *g = p;
    guest_main = pthread_self(); guest_main_valid = 1;
    char *apple[3]; char ep[4200];
    snprintf(ep, sizeof ep, "executable_path=%s", g->img->path);
    apple[0] = ep; apple[1] = NULL;
    if (sigsetjmp(guest_jmp, 0) == 0) {
        void *pool = objc_autoreleasePoolPush();
        rt_objc_load_image(g->img);
        /* static initialisers */
        for (int i = 0; i < g->img->nsects; i++) {
            struct rt_section *s = &g->img->sects[i];
            uint8_t type = s->flags & 0xff;
            if (type == 0x9) {               /* S_MOD_INIT_FUNC_POINTERS */
                void (**fns)(int, char **, char **, char **) = (void *)(uintptr_t)s->addr;
                for (uint64_t k = 0; k < s->size / 8; k++) fns[k](g->argc, g->argv, environ, apple);
            } else if (type == 0x16) {       /* S_INIT_FUNC_OFFSETS */
                const uint32_t *offs = (const uint32_t *)(uintptr_t)s->addr;
                for (uint64_t k = 0; k < s->size / 4; k++)
                    ((void (*)(int, char **, char **, char **))(uintptr_t)(g->img->header_addr + offs[k]))(g->argc, g->argv, environ, apple);
            }
        }
        objc_autoreleasePoolPop(pool);
        int (*main_fn)(int, char **, char **, char **) = (void *)(uintptr_t)(g->img->header_addr + g->img->entryoff);
        rt_log("calling main at %p", (void *)main_fn);
        int rc = main_fn(g->argc, g->argv, environ, apple);
        rt_run_atexit();
        guest_code = rc;
    }
    guest_main_valid = 0;
    return NULL;
}

static int load_main_image(const char *path, int dry, char *err, size_t errlen, struct rt_image **out) {
    char *bundle;
    char *exe = resolve_executable(path, &bundle);
    if (bundle) rt_set_bundle_path(bundle);
    struct rt_image *img = rt_xcalloc(1, sizeof *img);
    struct rt_load_opts o = { dry, 0 };
    int rc = rt_image_load(img, exe, &o, err, errlen);
    free(exe); free(bundle);
    *out = img;
    return rc;
}

int iosrt_run(const char *path, const struct rt_run_opts *o) {
    iosrt_init();
    char err[1024] = "";
    struct rt_image *img;
    if (load_main_image(path, 0, err, sizeof err, &img)) {
        rt_warn("cannot load %s: %s", path, err);
        if (rt_plat->on_exit) rt_plat->on_exit(66, err);
        return 66;
    }
    rt_traps_seal();
    rt_image_protect(img);
    rt_main_image = img;
    if (img->nmissing) {
        unsigned fn = 0;
        for (int i = 0; i < img->nmissing; i++) fn += img->missing[i].kind == 0;
        rt_log("%d imported symbols are not implemented (%u functions bound to traps)", img->nmissing, fn);
    }
    rt_objc_map_image(img);
    install_crash_handler();
    int argc = o && o->argc ? o->argc : 1;
    char *defargv[2] = { img->path, NULL };
    char **argv = o && o->argc ? o->argv : defargv;
    rt_set_args(argc, argv, environ);
    rt_foundation_set_args(argc, argv);
    struct guest_args g = { img, argc, argv };
    pthread_attr_t a; pthread_attr_init(&a);
    pthread_attr_setstacksize(&a, 8u << 20);
    pthread_t t;
    guest_reason[0] = 0;
    if (pthread_create(&t, &a, guest_thread, &g)) { rt_warn("cannot start guest thread"); return 70; }
    pthread_join(t, NULL);
    pthread_attr_destroy(&a);
    if (rt_plat->on_exit) rt_plat->on_exit(guest_code, guest_reason[0] ? guest_reason : NULL);
    return guest_code;
}

/* ------------------------------------------------- compatibility report */
static int cmp_missing(const void *a, const void *b) {
    const struct rt_missing *x = a, *y = b;
    int c = strcmp(x->lib, y->lib);
    return c ? c : strcmp(x->name, y->name);
}
char *iosrt_report(const char *path) {
    iosrt_init();
    char err[1024] = "";
    struct rt_image *img;
    rt_sb sb; rt_sb_init(&sb);
    rt_sb_printf(&sb, "iosrt compatibility report\nfile: %s\n", path);
    if (load_main_image(path, 1, err, sizeof err, &img)) {
        rt_sb_printf(&sb, "status: NOT LOADABLE\nreason: %s\n", err);
        return rt_sb_take(&sb);
    }
    unsigned total = img->n_resolved;
    for (int i = 0; i < img->nmissing; i++) total++;
    struct rt_section *cl = rt_image_section(img, NULL, "__objc_classlist");
    struct rt_section *ca = rt_image_section(img, NULL, "__objc_catlist");
    rt_sb_printf(&sb, "status: loadable (arm64, %s fixups)\n", img->has_chained ? "chained" : "dyld-info");
    rt_sb_printf(&sb, "segments: %d, sections: %d, linked libraries: %d\n", img->nsegs, img->nsects, img->ndylibs);
    rt_sb_printf(&sb, "rebases: %u, binds: %u\n", img->n_rebases, img->n_binds);
    rt_sb_printf(&sb, "objc classes: %llu, categories: %llu\n",
                 (unsigned long long)(cl ? cl->size / 8 : 0), (unsigned long long)(ca ? ca->size / 8 : 0));
    rt_sb_printf(&sb, "imported symbols: %u, provided by iosrt: %u (%.1f%%), missing: %d\n", total, img->n_resolved,
                 total ? 100.0 * img->n_resolved / total : 100.0, img->nmissing);
    qsort(img->missing, (size_t)img->nmissing, sizeof *img->missing, cmp_missing);
    /* per-library summary */
    rt_sb_puts(&sb, "missing by library:\n");
    for (int i = 0; i < img->nmissing;) {
        int j = i; while (j < img->nmissing && !strcmp(img->missing[j].lib, img->missing[i].lib)) j++;
        rt_sb_printf(&sb, "  %-22s %d\n", img->missing[i].lib, j - i);
        i = j;
    }
    rt_sb_puts(&sb, "missing symbols:\n");
    static const char *kinds[] = { "func", "data", "class" };
    for (int i = 0; i < img->nmissing; i++)
        rt_sb_printf(&sb, "  [%s] %s %s\n", img->missing[i].lib, kinds[img->missing[i].kind], img->missing[i].name);
    if (img->map) munmap(img->map, img->map_size);
    return rt_sb_take(&sb);
}
