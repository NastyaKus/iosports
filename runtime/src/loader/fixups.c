/*
 * dyld fixups: classic LC_DYLD_INFO rebase/bind opcode streams and
 * LC_DYLD_CHAINED_FIXUPS (DYLD_CHAINED_PTR_64 / _64_OFFSET).
 * Implemented from the documented format; lazy binds are resolved eagerly.
 */
#include "../internal.h"
#include <string.h>
#include <stdlib.h>

char *rt_lib_short(const char *path);

static uint64_t uleb(const uint8_t **p, const uint8_t *end) {
    uint64_t r = 0; int sh = 0;
    while (*p < end) {
        uint8_t b = *(*p)++;
        if (sh < 64) r |= (uint64_t)(b & 0x7f) << sh;
        sh += 7;
        if (!(b & 0x80)) break;
    }
    return r;
}
static int64_t sleb(const uint8_t **p, const uint8_t *end) {
    int64_t r = 0; int sh = 0; uint8_t b = 0;
    while (*p < end) {
        b = *(*p)++;
        if (sh < 64) r |= (int64_t)(b & 0x7f) << sh;
        sh += 7;
        if (!(b & 0x80)) break;
    }
    if (sh < 64 && (b & 0x40)) r |= -((int64_t)1 << sh);
    return r;
}

static const char *ordinal_lib(struct rt_image *img, int64_t ord, char *tmp, size_t n) {
    if (ord > 0 && ord <= img->ndylibs) {
        char *s = rt_lib_short(img->dylibs[ord - 1]);
        snprintf(tmp, n, "%s", s); free(s);
        return tmp;
    }
    if (ord == 0 || ord == -1) return "self";
    if (ord == -2) return "flat";
    if (ord == -3) return "weak";
    return "?";
}

static uint64_t *seg_ptr(struct rt_image *img, int seg, uint64_t off) {
    if (seg < 0 || seg >= img->nsegs) return NULL;
    struct rt_segment *s = &img->segs[seg];
    if (off + 8 > s->vmsize) return NULL;
    return (uint64_t *)(uintptr_t)(s->vmaddr + off + (uint64_t)img->slide);
}

static int do_rebase(struct rt_image *img, char *err, size_t errlen) {
    const uint8_t *p = img->file + img->rebase_off, *end = p + img->rebase_size;
    if ((uint64_t)img->rebase_off + img->rebase_size > img->file_size) { snprintf(err, errlen, "rebase info out of bounds"); return -1; }
    int seg = 0; uint64_t off = 0; int type = 1;
    while (p < end) {
        uint8_t b = *p++, op = b & 0xf0, imm = b & 0x0f;
        switch (op) {
        case 0x00: return 0;
        case 0x10: type = imm; break;
        case 0x20: seg = imm; off = uleb(&p, end); break;
        case 0x30: off += uleb(&p, end); break;
        case 0x40: off += (uint64_t)imm * 8; break;
        case 0x50: case 0x60: case 0x70: case 0x80: {
            uint64_t count = op == 0x50 ? imm : (op == 0x70 ? 1 : uleb(&p, end));
            uint64_t skip = op == 0x80 ? uleb(&p, end) : 0;
            uint64_t add = op == 0x70 ? uleb(&p, end) : 0;
            for (uint64_t i = 0; i < count; i++) {
                uint64_t *ptr = seg_ptr(img, seg, off);
                if (!ptr) { snprintf(err, errlen, "rebase outside segment"); return -1; }
                if (type == 1) *ptr += (uint64_t)img->slide;
                else { snprintf(err, errlen, "unsupported rebase type %d", type); return -1; }
                img->n_rebases++;
                off += 8 + skip + add;
            }
            break;
        }
        default: snprintf(err, errlen, "bad rebase opcode 0x%02x", b); return -1;
        }
    }
    return 0;
}

extern int rt_bind_kind;   /* 0 unknown (chained), 1 non-lazy (GOT/data), 2 lazy (stub -> function) */
static int do_bind(struct rt_image *img, uint32_t foff, uint32_t fsize, int lazy, char *err, size_t errlen) {
    if (!fsize) return 0;
    rt_bind_kind = lazy ? 2 : 1;
    if ((uint64_t)foff + fsize > img->file_size) { snprintf(err, errlen, "bind info out of bounds"); return -1; }
    const uint8_t *p = img->file + foff, *end = p + fsize;
    int seg = 0; uint64_t off = 0; int64_t ord = 0, addend = 0;
    const char *sym = NULL; int weak_import = 0;
    const char *cached_sym = NULL; void *cached_addr = NULL;
    char libtmp[128];
    while (p < end) {
        uint8_t b = *p++, op = b & 0xf0, imm = b & 0x0f;
        switch (op) {
        case 0x00: if (!lazy) return 0; break;
        case 0x10: ord = imm; break;
        case 0x20: ord = (int64_t)uleb(&p, end); break;
        case 0x30: ord = imm ? (int64_t)(int8_t)(0xf0 | imm) : 0; break;
        case 0x40:
            sym = (const char *)p; weak_import = imm & 1;
            p += strnlen(sym, (size_t)(end - p)) + 1;
            break;
        case 0x50: if (imm != 1) { snprintf(err, errlen, "unsupported bind type %d", imm); return -1; } break;
        case 0x60: addend = sleb(&p, end); break;
        case 0x70: seg = imm; off = uleb(&p, end); break;
        case 0x80: off += uleb(&p, end); break;
        case 0x90: case 0xa0: case 0xb0: case 0xc0: {
            uint64_t count = 1, skip = 0;
            if (op == 0xc0) { count = uleb(&p, end); skip = uleb(&p, end); }
            for (uint64_t i = 0; i < count; i++) {
                uint64_t *ptr = seg_ptr(img, seg, off);
                if (!ptr || !sym) { snprintf(err, errlen, "bind outside segment"); return -1; }
                void *addr;
                if (sym == cached_sym) addr = cached_addr;
                else {
                    addr = rt_resolve_symbol(img, sym, ordinal_lib(img, ord, libtmp, sizeof libtmp), weak_import);
                    cached_sym = sym; cached_addr = addr;
                }
                *ptr = (uint64_t)(uintptr_t)addr + (uint64_t)addend;
                img->n_binds++;
                off += 8 + skip;
            }
            if (op == 0xa0) off += uleb(&p, end);
            else if (op == 0xb0) off += (uint64_t)imm * 8;
            break;
        }
        case 0xd0: snprintf(err, errlen, "threaded binds (arm64e) are not supported"); return -1;
        default: snprintf(err, errlen, "bad bind opcode 0x%02x", b); return -1;
        }
    }
    return 0;
}

int rt_fixups_dyldinfo(struct rt_image *img, char *err, size_t errlen) {
    if (do_rebase(img, err, errlen)) return -1;
    if (do_bind(img, img->bind_off, img->bind_size, 0, err, errlen)) return -1;
    if (do_bind(img, img->lazy_off, img->lazy_size, 1, err, errlen)) return -1;
    rt_bind_kind = 0;
    /* weak binds only matter for C++ coalescing across images; single image -> nothing to do */
    rt_log("dyld-info fixups: %u rebases, %u binds", img->n_rebases, img->n_binds);
    return 0;
}

/* ---------------------------------------------------------- chained */
int rt_fixups_chained(struct rt_image *img, char *err, size_t errlen) {
    if ((uint64_t)img->chained_off + img->chained_size > img->file_size || img->chained_size < 28) {
        snprintf(err, errlen, "chained fixups out of bounds"); return -1;
    }
    const uint8_t *h = img->file + img->chained_off;
    uint32_t hv[7]; memcpy(hv, h, sizeof hv);
    uint32_t starts_off = hv[1], imports_off = hv[2], symbols_off = hv[3], imports_count = hv[4], imports_fmt = hv[5];
    if (hv[0] != 0) { snprintf(err, errlen, "unknown chained fixups version %u", hv[0]); return -1; }
    if (hv[6] != 0) { snprintf(err, errlen, "compressed chained symbol names not supported"); return -1; }
    const char *symbols = (const char *)h + symbols_off;
    /* resolve imports up front */
    void **targets = rt_xcalloc(imports_count ? imports_count : 1, sizeof(void *));
    int64_t *addends = rt_xcalloc(imports_count ? imports_count : 1, sizeof(int64_t));
    char libtmp[128];
    for (uint32_t i = 0; i < imports_count; i++) {
        int64_t ord; int weak; uint32_t name_off; int64_t addend = 0;
        if (imports_fmt == 1 || imports_fmt == 2) {
            uint32_t v; memcpy(&v, h + imports_off + (size_t)i * (imports_fmt == 1 ? 4 : 8), 4);
            ord = (int8_t)(v & 0xff); if ((v & 0xff) < 0xf0) ord = v & 0xff;
            weak = (v >> 8) & 1; name_off = v >> 9;
            if (imports_fmt == 2) { int32_t a; memcpy(&a, h + imports_off + (size_t)i * 8 + 4, 4); addend = a; }
        } else if (imports_fmt == 3) {
            uint64_t v; memcpy(&v, h + imports_off + (size_t)i * 16, 8);
            ord = (int16_t)(v & 0xffff); if ((v & 0xffff) < 0xfff0) ord = v & 0xffff;
            weak = (v >> 16) & 1; name_off = (uint32_t)(v >> 32);
            memcpy(&addend, h + imports_off + (size_t)i * 16 + 8, 8);
        } else { snprintf(err, errlen, "unknown chained import format %u", imports_fmt); return -1; }
        targets[i] = rt_resolve_symbol(img, symbols + name_off, ordinal_lib(img, ord, libtmp, sizeof libtmp), weak);
        addends[i] = addend;
    }
    const uint8_t *starts = h + starts_off;
    uint32_t seg_count; memcpy(&seg_count, starts, 4);
    uint64_t image_base = img->header_addr;
    for (uint32_t s = 0; s < seg_count; s++) {
        uint32_t so; memcpy(&so, starts + 4 + s * 4, 4);
        if (!so) continue;
        const uint8_t *ss = starts + so;
        uint16_t page_size, fmt, page_count; uint64_t seg_off;
        memcpy(&page_size, ss + 4, 2); memcpy(&fmt, ss + 6, 2);
        memcpy(&seg_off, ss + 8, 8); memcpy(&page_count, ss + 20, 2);
        if (fmt != 2 && fmt != 6) { snprintf(err, errlen, "unsupported chained pointer format %u (arm64e?)", fmt); return -1; }
        for (uint32_t pg = 0; pg < page_count; pg++) {
            uint16_t start; memcpy(&start, ss + 22 + pg * 2, 2);
            if (start == 0xffff) continue;
            uint8_t *loc = (uint8_t *)(uintptr_t)(image_base + seg_off + (uint64_t)pg * page_size + start);
            for (;;) {
                uint64_t v; memcpy(&v, loc, 8);
                uint64_t next = (v >> 51) & 0xfff;
                uint64_t nv;
                if (v >> 63) {
                    uint32_t ordinal = (uint32_t)(v & 0xffffff);
                    int64_t add = (int64_t)((v >> 24) & 0xff);
                    if (ordinal >= imports_count) { snprintf(err, errlen, "chained bind ordinal out of range"); return -1; }
                    nv = (uint64_t)(uintptr_t)targets[ordinal] + (uint64_t)(addends[ordinal] + add);
                    img->n_binds++;
                } else {
                    uint64_t target = v & 0xfffffffffULL, high8 = (v >> 36) & 0xff;
                    nv = fmt == 2 ? target + (uint64_t)img->slide : image_base + target;
                    nv |= high8 << 56;
                    img->n_rebases++;
                }
                memcpy(loc, &nv, 8);
                if (!next) break;
                loc += next * 4;
            }
        }
    }
    free(targets); free(addends);
    rt_log("chained fixups: %u rebases, %u binds", img->n_rebases, img->n_binds);
    return 0;
}
