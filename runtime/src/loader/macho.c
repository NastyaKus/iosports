/*
 * Mach-O arm64 loader: fat/thin parsing, segment mapping, protections.
 * Written from the public Mach-O format description; no Apple code is used.
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define MH_MAGIC_64   0xfeedfacfu
#define MH_CIGAM_64   0xcffaedfeu
#define MH_MAGIC      0xfeedfaceu
#define FAT_MAGIC     0xcafebabeu
#define FAT_MAGIC_64  0xcafebabfu
#define CPU_TYPE_ARM64 0x0100000c
#define CPU_TYPE_ARM   0x0000000c
#define CPU_TYPE_X86_64 0x01000007
#define CPU_SUBTYPE_ARM64E 2
#define MH_EXECUTE 2

#define LC_SEGMENT_64          0x19
#define LC_SYMTAB              0x02
#define LC_LOAD_DYLIB          0x0c
#define LC_LOAD_WEAK_DYLIB     0x80000018
#define LC_REEXPORT_DYLIB      0x8000001f
#define LC_LAZY_LOAD_DYLIB     0x20
#define LC_LOAD_UPWARD_DYLIB   0x80000023
#define LC_DYLD_INFO           0x22
#define LC_DYLD_INFO_ONLY      0x80000022
#define LC_MAIN                0x80000028
#define LC_ENCRYPTION_INFO_64  0x2c
#define LC_DYLD_CHAINED_FIXUPS 0x80000034

struct rt_image *rt_main_image;
static struct rt_image *g_images[8];
static int g_nimages;

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint64_t be64(const uint8_t *p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }
static uint32_t le32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t le64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }

static const char *cpu_name(uint32_t t) {
    switch (t) {
    case CPU_TYPE_ARM64: return "arm64";
    case CPU_TYPE_ARM: return "armv7 (32-bit)";
    case CPU_TYPE_X86_64: return "x86_64";
    case 7: return "i386";
    default: return "unknown";
    }
}

static int read_file(const char *path, uint8_t **out, size_t *sz, char *err, size_t errlen) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { snprintf(err, errlen, "cannot open %s: %s", path, strerror(errno)); return -1; }
    struct stat st;
    if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode)) { close(fd); snprintf(err, errlen, "%s is not a regular file", path); return -1; }
    uint8_t *buf = rt_xmalloc((size_t)st.st_size);
    size_t got = 0;
    while (got < (size_t)st.st_size) {
        ssize_t r = read(fd, buf + got, (size_t)st.st_size - got);
        if (r <= 0) { close(fd); free(buf); snprintf(err, errlen, "read error on %s", path); return -1; }
        got += (size_t)r;
    }
    close(fd);
    *out = buf; *sz = got;
    return 0;
}

/* Select the arm64 slice of a fat binary (or validate a thin one). */
static int select_slice(uint8_t *data, size_t size, uint8_t **slice, size_t *slice_size, char *err, size_t errlen) {
    if (size < 32) { snprintf(err, errlen, "file too small to be Mach-O"); return -1; }
    uint32_t m = be32(data);
    if (m == FAT_MAGIC || m == FAT_MAGIC_64) {
        uint32_t n = be32(data + 4);
        int is64 = m == FAT_MAGIC_64;
        size_t esz = is64 ? 32 : 20;
        char found[256] = "";
        int best = -1, arm64e = -1;
        for (uint32_t i = 0; i < n && 8 + (i + 1) * esz <= size; i++) {
            const uint8_t *e = data + 8 + i * esz;
            uint32_t ct = be32(e), cs = be32(e + 4) & 0xff;
            size_t flen = strlen(found);
            snprintf(found + flen, sizeof found - flen, "%s%s%s", flen ? ", " : "", cpu_name(ct), ct == CPU_TYPE_ARM64 && cs == CPU_SUBTYPE_ARM64E ? "e" : "");
            if (ct == CPU_TYPE_ARM64) {
                if (cs == CPU_SUBTYPE_ARM64E) arm64e = (int)i; else best = (int)i;
            }
        }
        if (best < 0) {
            if (arm64e >= 0) snprintf(err, errlen, "only arm64e slice present (pointer authentication is not supported); slices: %s", found);
            else snprintf(err, errlen, "no arm64 slice in fat binary (slices: %s). 32-bit apps need a CPU emulator (see touchHLE)", found);
            return -1;
        }
        const uint8_t *e = data + 8 + (size_t)best * esz;
        uint64_t off = is64 ? be64(e + 8) : be32(e + 8);
        uint64_t sz = is64 ? be64(e + 16) : be32(e + 12);
        if (off + sz > size) { snprintf(err, errlen, "fat slice out of bounds"); return -1; }
        *slice = data + off; *slice_size = (size_t)sz;
        return 0;
    }
    uint32_t lm = le32(data);
    if (lm == MH_MAGIC) { snprintf(err, errlen, "32-bit Mach-O (%s) is not supported; arm64 only", cpu_name(le32(data + 4))); return -1; }
    if (lm != MH_MAGIC_64) { snprintf(err, errlen, "not a Mach-O file (magic 0x%08x)", lm); return -1; }
    uint32_t ct = le32(data + 4);
    if (ct != CPU_TYPE_ARM64) { snprintf(err, errlen, "Mach-O is for %s, need arm64", cpu_name(ct)); return -1; }
    if ((le32(data + 8) & 0xff) == CPU_SUBTYPE_ARM64E) { snprintf(err, errlen, "arm64e (pointer authentication) is not supported"); return -1; }
    *slice = data; *slice_size = size;
    return 0;
}

static void copy_name16(char *dst, const uint8_t *src) { memcpy(dst, src, 16); dst[16] = 0; }

static char *short_lib_name(const char *path) {
    const char *b = strrchr(path, '/');
    b = b ? b + 1 : path;
    size_t n = strcspn(b, ".");
    char *r = rt_xmalloc(n + 1);
    memcpy(r, b, n); r[n] = 0;
    return r;
}

static int parse_commands(struct rt_image *img, char *err, size_t errlen) {
    uint8_t *f = img->file;
    uint32_t filetype = le32(f + 12), ncmds = le32(f + 16), sizeofcmds = le32(f + 20);
    if (filetype != MH_EXECUTE) { snprintf(err, errlen, "Mach-O filetype %u is not an executable (dylibs/bundles not supported yet)", filetype); return -1; }
    if (32 + (size_t)sizeofcmds > img->file_size) { snprintf(err, errlen, "load commands out of bounds"); return -1; }
    const uint8_t *p = f + 32, *end = p + sizeofcmds;
    for (uint32_t i = 0; i < ncmds; i++) {
        if (p + 8 > end) { snprintf(err, errlen, "truncated load command"); return -1; }
        uint32_t cmd = le32(p), sz = le32(p + 4);
        if (sz < 8 || p + sz > end) { snprintf(err, errlen, "bad load command size"); return -1; }
        switch (cmd) {
        case LC_SEGMENT_64: {
            if (img->nsegs >= RT_MAX_SEGS) break;
            struct rt_segment *s = &img->segs[img->nsegs++];
            copy_name16(s->name, p + 8);
            s->vmaddr = le64(p + 24); s->vmsize = le64(p + 32);
            s->fileoff = le64(p + 40); s->filesize = le64(p + 48);
            s->maxprot = le32(p + 56); s->initprot = le32(p + 60);
            uint32_t nsects = le32(p + 64);
            for (uint32_t k = 0; k < nsects && img->nsects < RT_MAX_SECTS; k++) {
                const uint8_t *sp = p + 72 + k * 80;
                if (sp + 80 > p + sz) break;
                struct rt_section *sc = &img->sects[img->nsects++];
                copy_name16(sc->sectname, sp);
                copy_name16(sc->segname, sp + 16);
                sc->addr = le64(sp + 32); sc->size = le64(sp + 40);
                sc->flags = le32(sp + 64);
            }
            break;
        }
        case LC_DYLD_INFO: case LC_DYLD_INFO_ONLY:
            img->has_dyld_info = 1;
            img->rebase_off = le32(p + 8); img->rebase_size = le32(p + 12);
            img->bind_off = le32(p + 16); img->bind_size = le32(p + 20);
            img->weak_off = le32(p + 24); img->weak_size = le32(p + 28);
            img->lazy_off = le32(p + 32); img->lazy_size = le32(p + 36);
            break;
        case LC_DYLD_CHAINED_FIXUPS:
            img->has_chained = 1;
            img->chained_off = le32(p + 8); img->chained_size = le32(p + 12);
            break;
        case LC_MAIN:
            img->has_main = 1; img->entryoff = le64(p + 8);
            break;
        case LC_ENCRYPTION_INFO_64:
            img->cryptid = le32(p + 16);
            break;
        case LC_SYMTAB:
            img->symoff = le32(p + 8); img->nsyms = le32(p + 12);
            img->stroff = le32(p + 16); img->strsize = le32(p + 20);
            break;
        case LC_LOAD_DYLIB: case LC_LOAD_WEAK_DYLIB: case LC_REEXPORT_DYLIB:
        case LC_LAZY_LOAD_DYLIB: case LC_LOAD_UPWARD_DYLIB: {
            uint32_t off = le32(p + 8);
            if (img->ndylibs < RT_MAX_DYLIBS && off < sz) {
                char tmp[512]; size_t n = strnlen((const char *)p + off, sz - off);
                if (n >= sizeof tmp) n = sizeof tmp - 1;
                memcpy(tmp, p + off, n); tmp[n] = 0;
                img->dylibs[img->ndylibs++] = rt_xstrdup(tmp);
            }
            break;
        }
        default: break;
        }
        p += sz;
    }
    if (img->symoff && ((uint64_t)img->symoff + (uint64_t)img->nsyms * 16 > img->file_size || (uint64_t)img->stroff + img->strsize > img->file_size)) {
        img->nsyms = 0;
    }
    return 0;
}

static int map_segments(struct rt_image *img, char *err, size_t errlen) {
    uint64_t lo = UINT64_MAX, hi = 0;
    for (int i = 0; i < img->nsegs; i++) {
        struct rt_segment *s = &img->segs[i];
        if (s->vmsize == 0 || (s->initprot == 0 && s->maxprot == 0)) continue;   /* __PAGEZERO */
        if (s->vmaddr < lo) lo = s->vmaddr;
        if (s->vmaddr + s->vmsize > hi) hi = s->vmaddr + s->vmsize;
        if (s->fileoff + s->filesize > img->file_size) { snprintf(err, errlen, "segment %s exceeds file", s->name); return -1; }
    }
    if (lo == UINT64_MAX) { snprintf(err, errlen, "no loadable segments"); return -1; }
    const size_t align = 0x4000;
    size_t total = (size_t)(hi - lo);
    size_t reserve = total + align;
    uint8_t *raw = mmap(NULL, reserve, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (raw == MAP_FAILED) { snprintf(err, errlen, "mmap reserve failed: %s", strerror(errno)); return -1; }
    uint8_t *base = (uint8_t *)(((uintptr_t)raw + align - 1) & ~(uintptr_t)(align - 1));
    if (mprotect(base, total, PROT_READ | PROT_WRITE) != 0) { snprintf(err, errlen, "mprotect failed: %s", strerror(errno)); return -1; }
    img->map = raw; img->map_size = reserve;
    img->min_vmaddr = lo;
    img->slide = (intptr_t)((uintptr_t)base - lo);
    for (int i = 0; i < img->nsegs; i++) {
        struct rt_segment *s = &img->segs[i];
        if (s->vmsize == 0 || (s->initprot == 0 && s->maxprot == 0)) continue;
        uint64_t copy = s->filesize < s->vmsize ? s->filesize : s->vmsize;
        memcpy(base + (s->vmaddr - lo), img->file + s->fileoff, (size_t)copy);
        if (s->fileoff == 0 && s->filesize) img->header_addr = s->vmaddr + (uint64_t)img->slide;
    }
    for (int i = 0; i < img->nsects; i++) img->sects[i].addr += (uint64_t)img->slide;
    return 0;
}

void rt_image_protect(struct rt_image *img) {
    for (int i = 0; i < img->nsegs; i++) {
        struct rt_segment *s = &img->segs[i];
        if (s->vmsize == 0 || (s->initprot == 0 && s->maxprot == 0)) continue;
        int prot = 0;
        if (s->initprot & 1) prot |= PROT_READ;
        if (s->initprot & 2) prot |= PROT_WRITE;
        if (s->initprot & 4) prot |= PROT_EXEC;
        if (!strcmp(s->name, "__DATA_CONST") || !strncmp(s->name, "__AUTH", 6)) prot |= PROT_WRITE; /* objc runtime patches it */
        uint8_t *a = (uint8_t *)(uintptr_t)(s->vmaddr + (uint64_t)img->slide);
        size_t len = (size_t)((s->vmsize + 0xfff) & ~0xfffULL);
        if (prot & PROT_EXEC) __builtin___clear_cache((char *)a, (char *)a + len);
        if (mprotect(a, len, prot) != 0) rt_warn("mprotect(%s) failed", s->name);
    }
}

struct rt_section *rt_image_section(struct rt_image *img, const char *seg, const char *sect) {
    for (int i = 0; i < img->nsects; i++) {
        if ((!seg || !strcmp(img->sects[i].segname, seg)) && !strcmp(img->sects[i].sectname, sect)) return &img->sects[i];
    }
    return NULL;
}

int rt_image_contains(uint64_t addr) {
    for (int i = 0; i < g_nimages; i++) {
        struct rt_image *im = g_images[i];
        uint64_t b = (uint64_t)(uintptr_t)im->map;
        if (addr >= b && addr < b + im->map_size) return 1;
    }
    return 0;
}

const char *rt_image_symbolize(uint64_t addr, uint64_t *off) {
    for (int i = 0; i < g_nimages; i++) {
        struct rt_image *im = g_images[i];
        uint64_t b = (uint64_t)(uintptr_t)im->map;
        if (addr < b || addr >= b + im->map_size) continue;
        uint64_t un = addr - (uint64_t)im->slide;
        const char *best = NULL; uint64_t bestv = 0;
        for (uint32_t k = 0; k < im->nsyms; k++) {
            const uint8_t *nl = im->file + im->symoff + (size_t)k * 16;
            uint8_t type = nl[4];
            if (type & 0xe0) continue;                 /* stab */
            if ((type & 0x0e) != 0x0e) continue;       /* N_SECT */
            uint64_t v = le64(nl + 8);
            uint32_t sx = le32(nl);
            if (v <= un && v >= bestv && sx < im->strsize) { bestv = v; best = (const char *)im->file + im->stroff + sx; }
        }
        if (off) *off = best ? un - bestv : un - im->min_vmaddr;
        return best ? best : "<image>";
    }
    return NULL;
}

int rt_image_load_buffer(struct rt_image *img, const char *path, uint8_t *data, size_t size,
                         const struct rt_load_opts *o, char *err, size_t errlen) {
    memset(img, 0, sizeof *img);
    img->path = rt_xstrdup(path);
    img->dry_run = o && o->dry_run;
    uint8_t *slice; size_t ssz;
    if (select_slice(data, size, &slice, &ssz, err, errlen)) return -1;
    img->file = rt_xmalloc(ssz);
    memcpy(img->file, slice, ssz);
    img->file_size = ssz;
    if (parse_commands(img, err, errlen)) return -1;
    if (img->cryptid != 0) {
        snprintf(err, errlen, "binary is FairPlay-encrypted (cryptid=%u); only decrypted or self-built apps can run", img->cryptid);
        return -1;
    }
    if (!img->has_main) { snprintf(err, errlen, "no LC_MAIN (LC_UNIXTHREAD-style executables are not supported)"); return -1; }
    if (map_segments(img, err, errlen)) return -1;
    if (g_nimages < 8) g_images[g_nimages++] = img;
    rt_log("mapped %s at %p slide=0x%lx (%d segs, %d sects, %d dylibs)", path, (void *)img->map, (long)img->slide,
           img->nsegs, img->nsects, img->ndylibs);
    int rc;
    if (img->has_chained) rc = rt_fixups_chained(img, err, errlen);
    else if (img->has_dyld_info) rc = rt_fixups_dyldinfo(img, err, errlen);
    else { snprintf(err, errlen, "no dyld fixup information"); rc = -1; }
    if (rc) return -1;
    return 0;
}

int rt_image_load(struct rt_image *img, const char *path, const struct rt_load_opts *o, char *err, size_t errlen) {
    uint8_t *data; size_t size;
    if (read_file(path, &data, &size, err, errlen)) return -1;
    int rc = rt_image_load_buffer(img, path, data, size, o, err, errlen);
    free(data);
    return rc;
}

/* exported helper used by fixup code */
char *rt_lib_short(const char *path) { return short_lib_name(path); }
