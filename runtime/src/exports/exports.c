/* The HLE export table: Mach-O symbol name -> runtime address. Generated
 * from the *.def X-macro lists (also consumed by tools/gen_tbd.py). */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sched.h>

/* This TU deliberately does not include internal.h: every runtime symbol is
 * declared opaquely (extern char x[]) by the generated decls.h. */
struct objc_class;
struct iosrt_export { const char *lib; const char *name; void *addr; };
#include "decls.h"

#define SYM(lib, sym, addr)
#define CLASS(lib, name) extern struct objc_class iosrt_class_##name, iosrt_meta_##name;
#include "libsystem.def"
#include "objc.def"
#include "foundation.def"
#include "uikit.def"
#include "audio.def"
#undef SYM
#undef CLASS

static struct iosrt_export table[] = {
#define SYM(lib, sym, addr) { #lib, #sym, (void *)(addr) },
#define CLASS(lib, name) { #lib, "_OBJC_CLASS_$_" #name, (void *)&iosrt_class_##name }, \
                         { #lib, "_OBJC_METACLASS_$_" #name, (void *)&iosrt_meta_##name },
#include "libsystem.def"
#include "objc.def"
#include "foundation.def"
#include "uikit.def"
#include "audio.def"
#undef SYM
#undef CLASS
};
static const size_t ntable = sizeof table / sizeof table[0];

static int cmp(const void *a, const void *b) {
    return strcmp(((const struct iosrt_export *)a)->name, ((const struct iosrt_export *)b)->name);
}
void rt_exports_init(void) { qsort(table, ntable, sizeof table[0], cmp); }
const struct iosrt_export *rt_export_find(const char *name) {
    struct iosrt_export key = { NULL, name, NULL };
    return bsearch(&key, table, ntable, sizeof table[0], cmp);
}
size_t rt_export_count(void) { return ntable; }
const struct iosrt_export *rt_export_at(size_t i) { return i < ntable ? &table[i] : NULL; }
