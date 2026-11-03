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

_Noreturn void rt_missing_called(const char *name) {
    char msg[512];
    snprintf(msg, sizeof msg, "called unimplemented function %s", name);
    rt_guest_exit(70, msg);
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

/* Heuristic: is an unknown import a data symbol (constant, key, notification name...)? */
static int looks_like_data(const char *n) {
    if (!strncmp(n, "_OBJC_EHTYPE_$_", 15) || !strncmp(n, "_OBJC_IVAR_$_", 13)) return 1;
    if (!strncmp(n, "__kCF", 5) || !strncmp(n, "__NSConcrete", 12) || !strncmp(n, "__dispatch_", 11)) return 1;
    if (n[0] == '_' && n[1] == 'k' && isupper((unsigned char)n[2])) return 1;
    size_t L = strlen(n);
    const char *suf[] = { "Notification", "Key", "Constant", "Identifier", "Name", "Type", "Mode", "Attribute",
                          "Category", "Option", "Domain", "Exception", "Preset", "Port", "Keys", "Value", NULL };
    for (int i = 0; suf[i]; i++) {
        size_t sl = strlen(suf[i]);
        if (L > sl && !strcmp(n + L - sl, suf[i])) return 1;
    }
    if (!strncmp(n, "_UI", 3) && strstr(n, "Font")) return 1;
    return 0;
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
    if (looks_like_data(name)) {
        note_missing(img, name, lib, 1);
        return img->dry_run ? NULL : rt_xcalloc(1, 256);
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
