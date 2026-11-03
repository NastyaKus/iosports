/*
 * Symbol resolution against the runtime's HLE export table.
 * Unknown functions are bound to per-symbol trap stubs that report the
 * missing API by name instead of crashing at a random address.
 */
#include "../internal.h"
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <ctype.h>

#define STUB_SIZE 32
#define STUB_REGION (1u << 20)

static uint8_t *stub_region;
static size_t stub_used;
static pthread_mutex_t stub_lock = PTHREAD_MUTEX_INITIALIZER;

extern char iosrt_trap_entry[];   /* asm: mov x0, x16 ; b rt_missing_called */

/* Strict mode (default for the CLI/tests): calling a missing API ends the guest.
 * Lenient mode (IOSRT_STRICT=0, default in the Android shell): the call is logged
 * once and returns 0/nil, so apps can get past non-essential missing features. */
static int g_strict = -1;
int rt_is_strict(void) {
    if (g_strict < 0) { const char *s = getenv("IOSRT_STRICT"); g_strict = !(s && s[0] == '0'); }
    return g_strict;
}
void rt_set_strict(int on) { g_strict = on; }
static pthread_mutex_t seen_lock = PTHREAD_MUTEX_INITIALIZER;
static char **seen; static size_t nseen, capseen;
int rt_first_time(const char *key) {
    pthread_mutex_lock(&seen_lock);
    for (size_t i = 0; i < nseen; i++) if (!strcmp(seen[i], key)) { pthread_mutex_unlock(&seen_lock); return 0; }
    if (nseen == capseen) { capseen = capseen ? capseen * 2 : 64; seen = realloc(seen, capseen * sizeof *seen); }
    seen[nseen++] = rt_xstrdup(key);
    pthread_mutex_unlock(&seen_lock);
    return 1;
}
/* NSException-style programming errors: fatal in strict mode, logged in lenient mode */
void rt_soft_error(const char *msg) {
    if (rt_is_strict()) rt_guest_exit(73, msg);
    if (rt_first_time(msg)) rt_warn("error (ignored in lenient mode): %s", msg);
}
long rt_missing_called(const char *name) {
    if (rt_is_strict()) {
        char msg[512];
        snprintf(msg, sizeof msg, "called unimplemented function %s", name);
        rt_guest_exit(70, msg);
    }
    if (rt_first_time(name)) rt_warn("missing: %s (stub returns 0)", name);
    return 0;
}

static void *make_trap(const char *name) {
    pthread_mutex_lock(&stub_lock);
    if (!stub_region) {
        stub_region = mmap(NULL, STUB_REGION, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (stub_region == MAP_FAILED) {
            /* W^X systems: fall back to RW now, RX after fixups (rt_traps_seal) */
            stub_region = mmap(NULL, STUB_REGION, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        }
    }
    if (stub_used + STUB_SIZE > STUB_REGION) { pthread_mutex_unlock(&stub_lock); return (void *)iosrt_trap_entry; }
    uint32_t *s = (uint32_t *)(stub_region + stub_used);
    stub_used += STUB_SIZE;
    s[0] = 0x58000090;          /* ldr x16, #16  -> name     */
    s[1] = 0x580000b1;          /* ldr x17, #20  -> handler  */
    s[2] = 0xd61f0220;          /* br  x17                   */
    s[3] = 0xd503201f;          /* nop                       */
    uint64_t nm = (uint64_t)(uintptr_t)rt_xstrdup(name), h = (uint64_t)(uintptr_t)iosrt_trap_entry;
    memcpy(&s[4], &nm, 8);
    memcpy(&s[6], &h, 8);
    __builtin___clear_cache((char *)s, (char *)s + STUB_SIZE);
    pthread_mutex_unlock(&stub_lock);
    return s;
}

void rt_traps_seal(void) {
    if (stub_region) {
        mprotect(stub_region, STUB_REGION, PROT_READ | PROT_EXEC);
        __builtin___clear_cache((char *)stub_region, (char *)stub_region + stub_used);
    }
}

static void note_missing(struct rt_image *img, const char *name, const char *lib, int kind) {
    for (int i = 0; i < img->nmissing; i++) {
        if (!strcmp(img->missing[i].name, name)) { img->missing[i].count++; return; }
    }
    if (img->nmissing == img->capmissing) {
        img->capmissing = img->capmissing ? img->capmissing * 2 : 64;
        img->missing = realloc(img->missing, sizeof(struct rt_missing) * (size_t)img->capmissing);
    }
    struct rt_missing *m = &img->missing[img->nmissing++];
    m->name = rt_xstrdup(name); m->lib = rt_xstrdup(lib ? lib : "?"); m->count = 1; m->kind = kind;
}

/* Heuristic: is an unknown import a data symbol (constant, key, notification name...)?
 * Misclassification is survivable: data stubs live in a non-executable region and the
 * crash handler turns a call into one into "return 0" in lenient mode. */
static int looks_like_data(const char *n) {
    if (!strncmp(n, "_OBJC_EHTYPE_$_", 15) || !strncmp(n, "_OBJC_IVAR_$_", 13)) return 1;
    if (!strncmp(n, "__kCF", 5) || !strncmp(n, "__NSConcrete", 12) || !strncmp(n, "__dispatch_", 11)) return 1;
    static const char *verbs[] = { "Create", "Copy", "Get", "Set", "Add", "Remove", "Dispose", "Send", "Received", "Connect",
                                   "Init", "Instantiate", "Is", "Begin", "End", "Draw", "Make", "Start", "Stop", "Open", "Close",
                                   "Read", "Write", "Release", "Retain", "Register", "Schedule", "Perform", "Show", "Load", NULL };
    for (int i = 0; verbs[i]; i++) {
        const char *p = strstr(n + 1, verbs[i]);
        if (p && (p == n + 1 || islower((unsigned char)p[-1]) || isupper((unsigned char)p[-1])) && !strstr(n, "Notification") && !strstr(n, "Key")) return 0;
    }
    if (n[0] == '_' && n[1] == 'k' && isupper((unsigned char)n[2])) return 1;
    size_t L = strlen(n);
    const char *suf[] = { "Notification", "Key", "Keys", "Domain", "Exception", "AttributeName", "ServiceType", "Mode", "Category", NULL };
    for (int i = 0; suf[i]; i++) {
        size_t sl = strlen(suf[i]);
        if (L > sl && !strcmp(n + L - sl, suf[i])) return 1;
    }
    if (!strncmp(n, "_UIFontWeight", 13) || !strncmp(n, "_NSFoundationVersion", 20)) return 1;
    return 0;
}

/* data stubs: zeroed 256-byte blocks in one non-executable region */
#define DATA_REGION (4u << 20)
static uint8_t *data_region; static size_t data_used;
static struct { uintptr_t a; const char *name; } *data_names; static size_t ndata_names, capdata_names;
static void *make_data_stub(const char *name) {
    if (!data_region) data_region = mmap(NULL, DATA_REGION, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (data_region == MAP_FAILED || data_used + 256 > DATA_REGION) return rt_xcalloc(1, 256);
    void *p = data_region + data_used; data_used += 256;
    if (ndata_names == capdata_names) { capdata_names = capdata_names ? capdata_names * 2 : 64; data_names = realloc(data_names, capdata_names * sizeof *data_names); }
    data_names[ndata_names].a = (uintptr_t)p; data_names[ndata_names++].name = rt_xstrdup(name);
    return p;
}
/* used by the crash handler: was pc a call into a data stub? */
const char *rt_data_stub_name(uintptr_t pc) {
    if (!data_region || pc < (uintptr_t)data_region || pc >= (uintptr_t)data_region + data_used) return NULL;
    for (size_t i = 0; i < ndata_names; i++) if (pc >= data_names[i].a && pc < data_names[i].a + 256) return data_names[i].name;
    return "?";
}

static void *self_symbol(struct rt_image *img, const char *name) {
    for (uint32_t k = 0; k < img->nsyms; k++) {
        const uint8_t *nl = img->file + img->symoff + (size_t)k * 16;
        uint8_t type = nl[4];
        if ((type & 0x0e) != 0x0e || !(type & 0x01)) continue;  /* defined + external */
        uint32_t sx; memcpy(&sx, nl, 4);
        if (sx >= img->strsize || strcmp((const char *)img->file + img->stroff + sx, name)) continue;
        uint64_t v; memcpy(&v, nl + 8, 8);
        return (void *)(uintptr_t)(v + (uint64_t)img->slide);
    }
    return NULL;
}

void *rt_platform_dlsym_fallback(const char *name);
int rt_bind_kind;

void *rt_resolve_symbol(struct rt_image *img, const char *name, const char *lib, int weak_import) {
    if (!strcmp(lib, "self")) {
        void *a = self_symbol(img, name);
        if (a) { img->n_resolved++; return a; }
    }
    const struct iosrt_export *e = rt_export_find(name);
    if (e) { img->n_resolved++; return e->addr; }
    void *a = self_symbol(img, name);           /* weak definitions inside the app itself */
    if (a) { img->n_resolved++; return a; }
    a = rt_platform_dlsym_fallback(name);
    if (a) { img->n_resolved++; return a; }
    if (weak_import) { note_missing(img, name, lib, 1); return NULL; }
    if (!strncmp(name, "_OBJC_CLASS_$_", 14)) {
        note_missing(img, name, lib, 2);
        return img->dry_run ? (void *)0 : (void *)rt_missing_class(name + 14);
    }
    if (!strncmp(name, "_OBJC_METACLASS_$_", 18)) {
        note_missing(img, name, lib, 2);
        return img->dry_run ? (void *)0 : (void *)rt_missing_class(name + 18)->isa;
    }
    /* lazily bound => called through a stub => function. Non-lazy (GOT) binds of unknown
     * symbols become zeroed data stubs: reading them yields nil/0, and calling one is caught
     * by the crash handler (lenient mode) and turned into "return 0". */
    int as_data = rt_bind_kind == 2 ? 0 : rt_bind_kind == 1 ? 1 : looks_like_data(name);
    if (as_data) {
        note_missing(img, name, lib, 1);
        return img->dry_run ? NULL : make_data_stub(name);
    }
    note_missing(img, name, lib, 0);
    return img->dry_run ? NULL : make_trap(name);
}

/* dlsym() for guests: look the C name up in the export table. */
void *rt_dlsym_name(const char *cname) {
    char buf[512];
    snprintf(buf, sizeof buf, "_%s", cname);
    const struct iosrt_export *e = rt_export_find(buf);
    if (e) return e->addr;
    if (rt_main_image) {
        void *a = self_symbol(rt_main_image, buf);
        if (a) return a;
    }
    return NULL;
}
