/*
 * iosrt — experimental iOS (Mach-O arm64) compatibility runtime for Android/Linux arm64.
 * Internal declarations shared by all runtime modules.
 *
 * ABI note (important for every exported function and every IMP):
 *   Guest code is compiled for the Darwin arm64 PCS, the runtime for AAPCS64.
 *   They are identical for non-variadic calls EXCEPT:
 *     - Darwin passes all variadic arguments on the stack (8-byte slots).  Every
 *       exported variadic function therefore goes through an asm trampoline in
 *       src/abi/darwin_variadic.S, and Darwin `va_list` is a plain pointer.
 *     - Darwin callers may rely on the callee extending <32-bit return values.
 *       AAPCS64 does not guarantee that, so exported functions / IMPs NEVER
 *       return bool/char/short: use rt_bool (long) instead.
 */
#pragma once
#define _GNU_SOURCE 1
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <setjmp.h>
#include <pthread.h>

#define RT_EXPORT __attribute__((visibility("default")))
#define RT_UNUSED __attribute__((unused))

typedef long rt_bool;                 /* return type for BOOL-returning exports */
typedef struct objc_object *id;
typedef struct objc_class *Class;
typedef const char *SEL;              /* interned C string */
typedef id (*IMP)(id, SEL, ...);
typedef double CGFloat;
typedef struct { CGFloat x, y; } CGPoint;
typedef struct { CGFloat width, height; } CGSize;
typedef struct { CGPoint origin; CGSize size; } CGRect;
typedef struct { unsigned long location, length; } NSRange;
typedef long NSInteger;
typedef unsigned long NSUInteger;
typedef uint16_t unichar;

struct objc_object { Class isa; };

/* Same layout as Apple's objc_class (arm64, no ptrauth). */
struct objc_class {
    Class isa;
    Class superclass;
    void *cache;        /* runtime-owned: struct rt_class * */
    void *vtable;       /* unused (0) */
    uintptr_t bits;     /* guest: class_ro_t* | swift flags; native: 0 */
};

/* ------------------------------------------------------------------ util */
void rt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void rt_warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void *rt_xmalloc(size_t n);
void *rt_xcalloc(size_t n, size_t m);
char *rt_xstrdup(const char *s);

/* growable string builder */
typedef struct { char *buf; size_t len, cap; } rt_sb;
void rt_sb_init(rt_sb *sb);
void rt_sb_putc(rt_sb *sb, char c);
void rt_sb_append(rt_sb *sb, const char *s, size_t n);
void rt_sb_puts(rt_sb *sb, const char *s);
void rt_sb_printf(rt_sb *sb, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
char *rt_sb_take(rt_sb *sb);  /* NUL terminated, caller frees */

/* ------------------------------------------------------------- platform */
struct rt_display_list;
struct rt_platform {
    const char *name;
    void (*write_out)(int fd, const char *buf, size_t len);
    void (*present)(const struct rt_display_list *dl);         /* may be NULL */
    int  (*audio_start)(void *unit);                             /* may be NULL */
    void (*audio_stop)(void *unit);
    void (*on_exit)(int code, const char *reason);
};
extern struct rt_platform *rt_plat;
void rt_out(int fd, const char *buf, size_t len);
void rt_platform_set_host(void);

/* ---------------------------------------------------------- export table */
struct iosrt_export { const char *lib; const char *name; void *addr; };
void rt_exports_init(void);
const struct iosrt_export *rt_export_find(const char *name);
size_t rt_export_count(void);
const struct iosrt_export *rt_export_at(size_t i);

/* ---------------------------------------------------------------- loader */
#define RT_MAX_SEGS 16
#define RT_MAX_SECTS 96
#define RT_MAX_DYLIBS 128

struct rt_section {
    char segname[17], sectname[17];
    uint64_t addr;      /* slid address */
    uint64_t size;
    uint32_t flags;
};
struct rt_segment {
    char name[17];
    uint64_t vmaddr, vmsize, fileoff, filesize;
    uint32_t maxprot, initprot;
};
struct rt_missing {
    char *name;
    char *lib;
    unsigned count;
    int kind;           /* 0 = function trap, 1 = data block, 2 = class placeholder */
};
struct rt_image {
    char *path;
    uint8_t *file; size_t file_size;       /* thin slice copy */
    uint8_t *map; size_t map_size;         /* reserved VA */
    intptr_t slide;
    uint64_t min_vmaddr;
    uint64_t header_addr;                  /* slid address of mach_header */
    struct rt_segment segs[RT_MAX_SEGS]; int nsegs;
    struct rt_section sects[RT_MAX_SECTS]; int nsects;
    char *dylibs[RT_MAX_DYLIBS]; int ndylibs;
    uint64_t entryoff; int has_main;
    uint32_t cryptid;
    /* LC_DYLD_INFO */
    uint32_t rebase_off, rebase_size, bind_off, bind_size, weak_off, weak_size, lazy_off, lazy_size;
    int has_dyld_info;
    /* LC_DYLD_CHAINED_FIXUPS */
    uint32_t chained_off, chained_size; int has_chained;
    /* LC_SYMTAB */
    uint32_t symoff, nsyms, stroff, strsize;
    /* stats */
    unsigned n_rebases, n_binds, n_resolved;
    struct rt_missing *missing; int nmissing, capmissing;
    int dry_run;
};

struct rt_load_opts { int dry_run; int verbose; };
int  rt_image_load(struct rt_image *img, const char *path, const struct rt_load_opts *o, char *err, size_t errlen);
int  rt_image_load_buffer(struct rt_image *img, const char *path, uint8_t *data, size_t size, const struct rt_load_opts *o, char *err, size_t errlen);
int  rt_fixups_dyldinfo(struct rt_image *img, char *err, size_t errlen);
int  rt_fixups_chained(struct rt_image *img, char *err, size_t errlen);
void rt_image_protect(struct rt_image *img);
struct rt_section *rt_image_section(struct rt_image *img, const char *seg, const char *sect);
int  rt_image_contains(uint64_t addr);
const char *rt_image_symbolize(uint64_t addr, uint64_t *off);
extern struct rt_image *rt_main_image;

/* symbol resolution (with traps for missing symbols) */
void *rt_resolve_symbol(struct rt_image *img, const char *name, const char *lib, int weak_import);

/* --------------------------------------------------------------- launch */
struct rt_run_opts { int argc; char **argv; int dry_run; int verbose; };
RT_EXPORT int iosrt_run(const char *path, const struct rt_run_opts *o);
RT_EXPORT char *iosrt_report(const char *path);   /* compatibility report, caller frees */
/* terminate the guest (from any guest thread) */
_Noreturn void rt_guest_exit(int code, const char *reason);
int rt_is_guest_main_thread(void);

/* ------------------------------------------------------------- variadic */
struct rt_va { const uint64_t *p; };
static inline uint64_t rt_va_u64(struct rt_va *v) { return *v->p++; }
static inline double rt_va_dbl(struct rt_va *v) { double d; __builtin_memcpy(&d, v->p++, 8); return d; }
/* objc-aware formatter: %@ supported when objc != 0 */
void rt_format(rt_sb *out, const char *fmt, struct rt_va *va, int objc);

/* ----------------------------------------------------------------- objc */
#define RT_CLS_META         0x0001
#define RT_CLS_REALIZED     0x0002
#define RT_CLS_INITIALIZED  0x0004
#define RT_CLS_INITIALIZING 0x0008
#define RT_CLS_NATIVE       0x0010
#define RT_CLS_GUEST        0x0020
#define RT_CLS_MISSING      0x0040
#define RT_CLS_ROOT         0x0080
#define RT_CLS_LOADED       0x0100

struct rt_method { SEL name; const char *types; IMP imp; };
struct rt_ivar { const char *name; const char *type; long offset; uint32_t size; };
struct rt_prop { const char *name; const char *attrs; };

struct rt_mtab {                     /* SEL -> rt_method* (open addressing) */
    struct rt_method **slots; uint32_t cap, count;
};
struct rt_cache {                    /* SEL -> IMP, includes inherited */
    SEL *keys; IMP *vals; uint32_t cap, count; uint32_t gen;
};
struct rt_class {
    const char *name;
    Class cls;
    uint32_t flags;
    size_t instance_size;
    struct rt_mtab methods;
    struct rt_method **mlist; size_t nm, capm;     /* ordered list (for introspection) */
    struct rt_ivar *ivars; size_t nivars;
    struct rt_prop *props; size_t nprops;
    const void **protocols; size_t nprotocols, capprotocols;
    struct rt_cache cache;
    pthread_t init_thread;
    const void *ro;
    int warned_missing;
};

void rt_objc_init(void);
int  rt_objc_map_image(struct rt_image *img);       /* realize classes, selrefs, categories */
void rt_objc_load_image(struct rt_image *img);      /* +load */
SEL  rt_sel(const char *name);                      /* intern */
static inline struct rt_class *rt_cls(Class c) { return (struct rt_class *)c->cache; }
Class rt_class_named(const char *name);
void  rt_register_class(Class cls);
Class rt_object_class(id obj);
IMP   rt_lookup(Class cls, SEL sel, int *found);
IMP   rt_lookup_for_send(id self, SEL sel);
rt_bool rt_responds(id obj, SEL sel);
void  rt_add_method(Class cls, SEL sel, IMP imp, const char *types);
Class rt_missing_class(const char *name);
int   rt_class_is_meta(Class c);
int   rt_is_kind_of(id obj, Class cls);
const char *rt_class_name(Class c);

/* Native (runtime-implemented) classes are statically allocated so their
 * addresses can be exported as _OBJC_CLASS_$_Name before any guest binds. */
#define RT_DECLARE_CLASS(N) extern struct objc_class iosrt_class_##N, iosrt_meta_##N
#define RT_DEFINE_CLASS(N) struct objc_class iosrt_class_##N, iosrt_meta_##N
#define RT_CLASS(N) (&iosrt_class_##N)
void rt_native_class(Class cls, Class meta, const char *name, Class super, size_t ivar_bytes);
void rt_native_method(Class cls, const char *sel, void *imp, const char *types);
void rt_native_cmethod(Class cls, const char *sel, void *imp, const char *types);

/* messaging (asm) */
extern id objc_msgSend(id, SEL, ...);
extern id objc_msgSendSuper2(void *super, SEL, ...);
extern id objc_msgSendSuper(void *super, SEL, ...);
#define RT_SEND0(R, obj, sel) (((R (*)(id, SEL))objc_msgSend)((obj), rt_sel(sel)))
#define RT_SEND1(R, obj, sel, T1, a1) (((R (*)(id, SEL, T1))objc_msgSend)((obj), rt_sel(sel), (a1)))
#define RT_SEND2(R, obj, sel, T1, a1, T2, a2) (((R (*)(id, SEL, T1, T2))objc_msgSend)((obj), rt_sel(sel), (a1), (a2)))

/* memory management */
id   rt_alloc_instance(Class cls, size_t extra);
void rt_dealloc_instance(id obj);
id   objc_retain(id obj);
void objc_release(id obj);
id   objc_autorelease(id obj);
long rt_retain_count(id obj);
void *objc_autoreleasePoolPush(void);
void objc_autoreleasePoolPop(void *token);
id   rt_autoreleased(id obj);    /* convenience: returns obj after autorelease */

/* blocks */
struct rt_block_descriptor { unsigned long reserved, size; void (*copy)(void *, const void *); void (*dispose)(const void *); };
struct rt_block { void *isa; int flags; int reserved; void *invoke; struct rt_block_descriptor *descriptor; };
#define RT_BLOCK_NEEDS_FREE (1 << 24)
#define RT_BLOCK_HAS_COPY_DISPOSE (1 << 25)
#define RT_BLOCK_IS_GLOBAL (1 << 28)
#define RT_BLOCK_REFCOUNT_MASK 0xfffe
void *_Block_copy(const void *b);
void _Block_release(const void *b);
int rt_is_block(id obj);

/* ----------------------------------------------------------- foundation */
void rt_foundation_init(void);
id   rt_str(const char *utf8);                       /* autoreleased NSString */
id   rt_str_new(const char *utf8);                   /* +1 NSString */
id   rt_str_new_utf16(const unichar *u, size_t n);   /* +1 */
const char *rt_str_utf8(id s);                       /* inner pointer, valid while s alive */
const unichar *rt_str_utf16(id s, size_t *len, unichar **tofree);
int  rt_is_string(id obj);
char *rt_describe(id obj);                           /* malloc'd UTF-8 of [obj description] */
id   rt_num_int(long long v);
id   rt_num_dbl(double v);
id   rt_array_new(id *objs, size_t n);               /* +1 */
size_t rt_array_count(id a);
id   rt_array_at(id a, size_t i);
id   rt_dict_get(id d, id key);
id   rt_plist_parse(const uint8_t *data, size_t len);  /* +1, XML or binary plist */
void rt_set_bundle_path(const char *path);
extern char *rt_bundle_path;
void rt_runloop_drain_main_queue(void);
int  rt_main_queue_pending(void);

/* ---------------------------------------------------------------- uikit */
void rt_uikit_init(void);
struct rt_dl_cmd {
    int op;                     /* 1 rect fill, 2 text, 3 ellipse fill, 4 ellipse stroke, 5 line, 6 rect stroke */
    float x, y, w, h;
    float r, g, b, a;
    float size, line;
    int align;
    char *text;
};
struct rt_display_list {
    struct rt_dl_cmd *cmds; size_t n, cap;
    float width, height;
    unsigned frame;
};
void rt_dl_clear(struct rt_display_list *dl);
struct rt_dl_cmd *rt_dl_push(struct rt_display_list *dl, int op);
void rt_dl_dump(const struct rt_display_list *dl, rt_sb *out);
/* platform -> runtime input */
RT_EXPORT void iosrt_push_touch(int phase, float x, float y);
RT_EXPORT void iosrt_set_screen(float w, float h, float scale);
RT_EXPORT void iosrt_request_quit(void);

/* ---------------------------------------------------------------- audio */
void rt_audio_init(void);
/* called by platform backends */
int rt_audio_render(void *unit, uint32_t frames, float *interleaved_out, uint32_t channels);
uint32_t rt_audio_unit_channels(void *unit);
double rt_audio_unit_rate(void *unit);
