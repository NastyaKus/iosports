/*
 * Exceptions for guest code: C++ (__cxa_throw / catch) and Objective-C
 * (@throw / @catch), with two-phase unwinding through the guest's own frames.
 *
 * Guest frames are unwound from the Mach-O image's __unwind_info (compact
 * unwind encodings for arm64) and __eh_frame (DWARF CFI, for functions the
 * compact format cannot describe). The landing pads, catch clauses and
 * cleanups come from the functions' LSDAs (__gcc_except_tab, the Itanium
 * format), interpreted by the personality routines here: guests bind
 * ___gxx_personality_v0 / ___objc_personality_v0 to markers, and the unwinder
 * recognises them. Frames of the runtime itself (host code: a guest throw
 * enters through one, Foundation's -raise is one) are stepped over with the
 * host's unwinder.
 *
 * Exception objects carry their own header (struct exc) ahead of the thrown
 * object; guests only see the _Unwind_Exception inside it (landing pads get
 * its address in x0) and the thrown object.
 * Implemented from the public Itanium C++ ABI, the DWARF spec and the
 * documented compact unwind format.
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>
#include <unwind.h>
#include <stddef.h>

void rt_print_backtrace(void);

/* ------------------------------------------------------------ context */
struct uctx { uint64_t x[31]; uint64_t sp, pc; uint64_t d[8]; };   /* layout used by iosrt_uw_install (darwin_abi.S) */
_Noreturn void iosrt_uw_install(const struct uctx *c);
_Static_assert(sizeof(struct uctx) == 328, "uctx layout");

static uint64_t rd64(uint64_t a) { uint64_t v; memcpy(&v, (const void *)(uintptr_t)a, 8); return v; }
static int is_guest(uint64_t pc) { return rt_image_for_addr(pc) != NULL; }

/* ------------------------------------------------- host frames (runtime) */
/* Capture the registers of the first guest frame above the caller: host frames in
 * between are stepped over with the host unwinder (DWARF CFI of the runtime). */
#if !defined(__ANDROID__)
struct host_walk { struct uctx *c; int found; };
static _Unwind_Reason_Code host_cb(struct _Unwind_Context *uc, void *arg) {
    struct host_walk *w = arg;
    uint64_t pc = (uint64_t)_Unwind_GetIP(uc);
    if (!is_guest(pc)) return _URC_NO_REASON;
    for (int i = 19; i <= 30; i++) w->c->x[i] = (uint64_t)_Unwind_GetGR(uc, i);
    w->c->sp = (uint64_t)_Unwind_GetCFA(uc);
    w->c->pc = pc;
    /* libgcc's _Unwind_GetGR cannot read the 128-bit vector registers: take d8-d15 as they
     * are now (the runtime frames being skipped do not keep floating-point state across calls) */
    __asm__ volatile("stp d8, d9, [%0]\n stp d10, d11, [%0, #16]\n stp d12, d13, [%0, #32]\n stp d14, d15, [%0, #48]" :: "r"(w->c->d) : "memory");
    w->found = 1;
    return _URC_END_OF_STACK;
}
#endif
#if defined(__ANDROID__)
/* a host frame in the middle of the guest's stack (the runtime called back into the
 * guest): step it with LLVM libunwind, seeded with the registers we have */
typedef void unw_cursor_t_;
typedef struct { uint64_t data[512]; } unw_context_t_;
typedef struct { uint64_t data[1024]; } unw_cursor_buf;
extern int __unw_getcontext(unw_context_t_ *);
extern int __unw_init_local(unw_cursor_t_ *, unw_context_t_ *);
extern int __unw_step(unw_cursor_t_ *);
extern int __unw_get_reg(unw_cursor_t_ *, int, uint64_t *);
extern int __unw_set_reg(unw_cursor_t_ *, int, uint64_t);
extern int __unw_get_fpreg(unw_cursor_t_ *, int, double *);
extern int __unw_set_fpreg(unw_cursor_t_ *, int, double);
#define UNW_REG_IP (-1)
#define UNW_REG_SP (-2)
static int host_step_to_guest(struct uctx *c) {
    static unw_context_t_ uctx; static unw_cursor_buf curbuf;   /* large; exceptions are rare, guarded below */
    unw_cursor_t_ *cur = &curbuf;
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&lock);
    __unw_getcontext(&uctx);
    __unw_init_local(cur, &uctx);
    for (int i = 19; i <= 30; i++) __unw_set_reg(cur, i, c->x[i]);
    for (int i = 0; i < 8; i++) { double d; memcpy(&d, &c->d[i], 8); __unw_set_fpreg(cur, 72 + i, d); }
    __unw_set_reg(cur, UNW_REG_SP, c->sp);
    __unw_set_reg(cur, UNW_REG_IP, c->pc);
    int ok = 0;
    for (int n = 0; n < 64; n++) {
        int r = __unw_step(cur);
        uint64_t pc = 0; __unw_get_reg(cur, UNW_REG_IP, &pc);
        if (r < 0 || !pc) break;
        if (is_guest(pc)) {
            for (int i = 19; i <= 30; i++) __unw_get_reg(cur, i, &c->x[i]);
            for (int i = 0; i < 8; i++) { double d = 0; __unw_get_fpreg(cur, 72 + i, &d); memcpy(&c->d[i], &d, 8); }
            __unw_get_reg(cur, UNW_REG_SP, &c->sp);
            c->pc = pc; ok = 1; break;
        }
        if (r == 0) break;
    }
    pthread_mutex_unlock(&lock);
    return ok;
}
/* LLVM libunwind ends _Unwind_Backtrace before calling back for a frame without unwind
 * info (the guest's), so the walk is done on the cursor: its registers are the guest
 * frame's even when the step into it reports the end of the stack */
static int capture_guest_caller(struct uctx *c) {
    static __thread unw_context_t_ uctx; static __thread unw_cursor_buf curbuf;
    unw_cursor_t_ *cur = &curbuf;
    memset(c, 0, sizeof *c);
    __unw_getcontext(&uctx);
    __unw_init_local(cur, &uctx);
    for (int n = 0; n < 64; n++) {
        int r = __unw_step(cur);
        uint64_t pc = 0; __unw_get_reg(cur, UNW_REG_IP, &pc);
        if (r < 0 || !pc) return 0;
        if (is_guest(pc)) {
            for (int i = 19; i <= 30; i++) __unw_get_reg(cur, i, &c->x[i]);
            for (int i = 0; i < 8; i++) { double d = 0; __unw_get_fpreg(cur, 72 + i, &d); memcpy(&c->d[i], &d, 8); }
            __unw_get_reg(cur, UNW_REG_SP, &c->sp);
            c->pc = pc;
            return 1;
        }
        if (r == 0) return 0;
    }
    return 0;
}
#else
static int host_step_to_guest(struct uctx *c) { (void)c; return 0; }
static int capture_guest_caller(struct uctx *c) {
    memset(c, 0, sizeof *c);
    struct host_walk w = { c, 0 };
    _Unwind_Backtrace(host_cb, &w);
    return w.found;
}
#endif

/* ------------------------------------------------------- DWARF helpers */
static uint64_t uleb(const uint8_t **p) { uint64_t r = 0; int s = 0; uint8_t b; do { b = *(*p)++; if (s < 64) r |= (uint64_t)(b & 0x7f) << s; s += 7; } while (b & 0x80); return r; }
static int64_t sleb(const uint8_t **p) { int64_t r = 0; int s = 0; uint8_t b; do { b = *(*p)++; if (s < 64) r |= (int64_t)(b & 0x7f) << s; s += 7; } while (b & 0x80); if (s < 64 && (b & 0x40)) r |= -((int64_t)1 << s); return r; }
enum { PE_omit = 0xff, PE_indirect = 0x80 };
static uint64_t read_enc(const uint8_t **p, uint8_t enc, uint64_t func_base) {
    if (enc == PE_omit) return 0;
    const uint8_t *start = *p; uint64_t v = 0;
    switch (enc & 0x0f) {
    case 0x00: memcpy(&v, *p, 8); *p += 8; break;
    case 0x01: v = uleb(p); break;
    case 0x02: { uint16_t t; memcpy(&t, *p, 2); *p += 2; v = t; break; }
    case 0x03: { uint32_t t; memcpy(&t, *p, 4); *p += 4; v = t; break; }
    case 0x04: memcpy(&v, *p, 8); *p += 8; break;
    case 0x09: v = (uint64_t)sleb(p); break;
    case 0x0a: { int16_t t; memcpy(&t, *p, 2); *p += 2; v = (uint64_t)(int64_t)t; break; }
    case 0x0b: { int32_t t; memcpy(&t, *p, 4); *p += 4; v = (uint64_t)(int64_t)t; break; }
    case 0x0c: memcpy(&v, *p, 8); *p += 8; break;
    default: return 0;
    }
    if (!v) return 0;
    switch (enc & 0x70) {
    case 0x10: v += (uint64_t)(uintptr_t)start; break;   /* pcrel */
    case 0x40: v += func_base; break;                     /* funcrel */
    default: break;
    }
    if (enc & PE_indirect) v = rd64(v);
    return v;
}

/* ------------------------------------------------- per-frame unwind info */
struct finfo {
    uint64_t func_start;
    uint64_t lsda;
    uint64_t personality;   /* function address (resolved through the GOT) */
    uint32_t encoding;      /* compact encoding (0 when from DWARF) */
    const uint8_t *fde;     /* DWARF FDE, when the compact entry says so */
};

/* DWARF FDE -> function start, LSDA, personality, and the CFI program */
struct cie_info { uint64_t code_align; int64_t data_align; uint64_t ra_reg; uint8_t fde_enc, lsda_enc; int has_z; uint64_t personality;
                  const uint8_t *insns, *end; };
static int parse_cie(const uint8_t *cie, struct cie_info *ci) {
    uint32_t len; memcpy(&len, cie, 4);
    if (len == 0xffffffffu) return -1;
    const uint8_t *p = cie + 8, *end = cie + 4 + len;   /* skip length + CIE id */
    uint8_t version = *p++;
    const char *aug = (const char *)p; p += strlen(aug) + 1;
    memset(ci, 0, sizeof *ci); ci->lsda_enc = PE_omit; ci->fde_enc = 0;
    ci->code_align = uleb(&p); ci->data_align = sleb(&p);
    ci->ra_reg = version == 1 ? *p++ : uleb(&p);
    const uint8_t *aug_end = NULL;
    for (const char *a = aug; *a; a++) {
        if (*a == 'z') { uint64_t l = uleb(&p); aug_end = p + l; ci->has_z = 1; }
        else if (*a == 'L') ci->lsda_enc = *p++;
        else if (*a == 'R') ci->fde_enc = *p++;
        else if (*a == 'P') { uint8_t e = *p++; ci->personality = read_enc(&p, e, 0); }
        else if (*a == 'S' || *a == 'B') {}
        else break;
    }
    if (aug_end) p = aug_end;
    ci->insns = p; ci->end = end;
    return 0;
}
static int parse_fde(const uint8_t *fde, struct cie_info *ci, uint64_t *start, uint64_t *range, uint64_t *lsda, const uint8_t **insns, const uint8_t **end) {
    uint32_t len; memcpy(&len, fde, 4);
    if (len == 0 || len == 0xffffffffu) return -1;
    uint32_t cie_off; memcpy(&cie_off, fde + 4, 4);
    if (!cie_off) return -1;   /* a CIE */
    if (parse_cie(fde + 4 - cie_off, ci)) return -1;
    const uint8_t *p = fde + 8;
    *start = read_enc(&p, ci->fde_enc, 0);
    *range = read_enc(&p, ci->fde_enc & 0x0f, 0);
    *lsda = 0;
    if (ci->has_z) {
        uint64_t l = uleb(&p); const uint8_t *q = p;
        if (ci->lsda_enc != PE_omit && l) *lsda = read_enc(&q, ci->lsda_enc, 0);
        p += l;
    }
    *insns = p; *end = fde + 4 + len;
    return 0;
}

/* compact unwind lookup in __unwind_info */
static int compact_lookup(struct rt_image *im, uint64_t pc, struct finfo *fi) {
    struct rt_section *ui = rt_image_section(im, "__TEXT", "__unwind_info");
    if (!ui) return -1;
    const uint8_t *base = (const uint8_t *)(uintptr_t)ui->addr;
    uint32_t h[7]; memcpy(h, base, sizeof h);
    if (h[0] != 1) return -1;
    uint32_t common_off = h[1], common_n = h[2], pers_off = h[3], pers_n = h[4], idx_off = h[5], idx_n = h[6];
    uint64_t mh = im->header_addr;
    uint32_t off = (uint32_t)(pc - mh);
    /* first level: last entry with functionOffset <= off (the final entry is a sentinel) */
    const uint8_t *idx = base + idx_off;
    uint32_t lo = 0, hi = idx_n ? idx_n - 1 : 0;
    if (idx_n < 2) return -1;
    while (hi - lo > 1) { uint32_t mid = (lo + hi) / 2, fo; memcpy(&fo, idx + mid * 12, 4); if (fo <= off) lo = mid; else hi = mid; }
    uint32_t e[3]; memcpy(e, idx + lo * 12, 12);
    uint32_t first_fn = e[0], page_off = e[1], lsda_idx_off = e[2];
    uint32_t next_fn; memcpy(&next_fn, idx + (lo + 1) * 12, 4);
    if (off < first_fn || !page_off) return -1;
    const uint8_t *page = base + page_off;
    uint32_t kind; memcpy(&kind, page, 4);
    uint32_t enc = 0, fstart = 0, fend = next_fn;
    if (kind == 2) {   /* regular */
        uint16_t eoff, en; memcpy(&eoff, page + 4, 2); memcpy(&en, page + 6, 2);
        const uint8_t *ents = page + eoff;
        int l = 0, r = en - 1, best = -1;
        while (l <= r) { int m = (l + r) / 2; uint32_t fo; memcpy(&fo, ents + m * 8, 4); if (fo <= off) { best = m; l = m + 1; } else r = m - 1; }
        if (best < 0) return -1;
        memcpy(&fstart, ents + best * 8, 4); memcpy(&enc, ents + best * 8 + 4, 4);
        if (best + 1 < en) memcpy(&fend, ents + (best + 1) * 8, 4);
    } else if (kind == 3) {   /* compressed */
        uint16_t eoff, en, encoff, encn; memcpy(&eoff, page + 4, 2); memcpy(&en, page + 6, 2); memcpy(&encoff, page + 8, 2); memcpy(&encn, page + 10, 2);
        const uint8_t *ents = page + eoff;
        int l = 0, r = en - 1, best = -1;
        while (l <= r) { int m = (l + r) / 2; uint32_t v; memcpy(&v, ents + m * 4, 4); if (first_fn + (v & 0xffffff) <= off) { best = m; l = m + 1; } else r = m - 1; }
        if (best < 0) return -1;
        uint32_t v; memcpy(&v, ents + best * 4, 4);
        fstart = first_fn + (v & 0xffffff);
        if (best + 1 < en) { uint32_t w; memcpy(&w, ents + (best + 1) * 4, 4); fend = first_fn + (w & 0xffffff); }
        uint32_t ei = v >> 24;
        if (ei < common_n) memcpy(&enc, base + common_off + ei * 4, 4);
        else if (ei - common_n < encn) memcpy(&enc, page + encoff + (ei - common_n) * 4, 4);
        else return -1;
    } else return -1;
    if (off >= fend) return -1;
    memset(fi, 0, sizeof *fi);
    fi->func_start = mh + fstart;
    fi->encoding = enc;
    if ((enc & 0x0f000000) == 0x03000000) {   /* UNWIND_ARM64_MODE_DWARF: offset into __eh_frame */
        struct rt_section *eh = rt_image_section(im, "__TEXT", "__eh_frame");
        if (!eh) return -1;
        fi->fde = (const uint8_t *)(uintptr_t)(eh->addr + (enc & 0x00ffffff));
        struct cie_info ci; uint64_t s, rng, lsda; const uint8_t *ins, *ie;
        if (parse_fde(fi->fde, &ci, &s, &rng, &lsda, &ins, &ie) == 0) {
            fi->func_start = s; fi->lsda = lsda; fi->personality = ci.personality;
        }
        return 0;
    }
    if (enc & 0x40000000) {   /* UNWIND_HAS_LSDA */
        const uint8_t *li = base + lsda_idx_off;
        uint32_t next_lsda; memcpy(&next_lsda, idx + (lo + 1) * 12 + 8, 4);
        uint32_t n = (next_lsda - lsda_idx_off) / 8;
        int l = 0, r = (int)n - 1;
        while (l <= r) {
            int m = (l + r) / 2; uint32_t fo, lo2; memcpy(&fo, li + m * 8, 4); memcpy(&lo2, li + m * 8 + 4, 4);
            if (fo == fstart) { fi->lsda = mh + lo2; break; }
            if (fo < fstart) l = m + 1; else r = m - 1;
        }
    }
    uint32_t pi = (enc >> 28) & 3;
    if (pi && pi <= pers_n) {
        uint32_t got_off; memcpy(&got_off, base + pers_off + (pi - 1) * 4, 4);
        fi->personality = rd64(mh + got_off);
    }
    return 0;
}

/* DWARF CFI: compute the caller's registers */
enum { R_UNDEF = 0, R_SAME, R_OFFSET, R_VAL_OFFSET, R_REG };
struct rule { uint8_t kind; int64_t val; };
struct cfi_state { uint64_t cfa_reg; int64_t cfa_off; struct rule r[96]; };
static int dwarf_step(struct uctx *c, const uint8_t *fde) {
    struct cie_info ci; uint64_t start, range, lsda; const uint8_t *ins, *end;
    if (parse_fde(fde, &ci, &start, &range, &lsda, &ins, &end)) return -1;
    struct cfi_state st, init, stack[8]; int sp = 0;
    memset(&st, 0, sizeof st); st.cfa_reg = 31;
    for (int i = 0; i < 96; i++) st.r[i].kind = R_SAME;
    uint64_t pc = c->pc - 1, loc = start;
    for (int pass = 0; pass < 2; pass++) {
        const uint8_t *p = pass ? ins : ci.insns, *e = pass ? end : ci.end;
        if (pass) init = st;
        while (p < e && loc <= pc) {
            uint8_t op = *p++, lowbits = op & 0x3f;
            switch (op & 0xc0) {
            case 0x40: loc += lowbits * ci.code_align; continue;
            case 0x80: { uint64_t o = uleb(&p); if (lowbits < 96) { st.r[lowbits].kind = R_OFFSET; st.r[lowbits].val = (int64_t)o * ci.data_align; } continue; }
            case 0xc0: if (lowbits < 96) st.r[lowbits] = init.r[lowbits]; continue;
            }
            switch (op) {
            case 0x00: break;
            case 0x01: loc = read_enc(&p, ci.fde_enc, 0); break;
            case 0x02: loc += *p++ * ci.code_align; break;
            case 0x03: { uint16_t d; memcpy(&d, p, 2); p += 2; loc += d * ci.code_align; break; }
            case 0x04: { uint32_t d; memcpy(&d, p, 4); p += 4; loc += d * ci.code_align; break; }
            case 0x05: { uint64_t r = uleb(&p), o = uleb(&p); if (r < 96) { st.r[r].kind = R_OFFSET; st.r[r].val = (int64_t)o * ci.data_align; } break; }
            case 0x06: { uint64_t r = uleb(&p); if (r < 96) st.r[r] = init.r[r]; break; }
            case 0x07: { uint64_t r = uleb(&p); if (r < 96) st.r[r].kind = R_UNDEF; break; }
            case 0x08: { uint64_t r = uleb(&p); if (r < 96) st.r[r].kind = R_SAME; break; }
            case 0x09: { uint64_t r = uleb(&p), r2 = uleb(&p); if (r < 96) { st.r[r].kind = R_REG; st.r[r].val = (int64_t)r2; } break; }
            case 0x0a: if (sp < 8) stack[sp++] = st; break;
            case 0x0b: if (sp > 0) { struct cfi_state t = stack[--sp]; memcpy(st.r, t.r, sizeof st.r); st.cfa_reg = t.cfa_reg; st.cfa_off = t.cfa_off; } break;
            case 0x0c: st.cfa_reg = uleb(&p); st.cfa_off = (int64_t)uleb(&p); break;
            case 0x0d: st.cfa_reg = uleb(&p); break;
            case 0x0e: st.cfa_off = (int64_t)uleb(&p); break;
            case 0x0f: return -1;                                   /* DW_CFA_def_cfa_expression */
            case 0x10: { uleb(&p); uint64_t l = uleb(&p); p += l; break; }   /* expression rules: not used for x19-x30 by compilers */
            case 0x11: { uint64_t r = uleb(&p); int64_t o = sleb(&p); if (r < 96) { st.r[r].kind = R_OFFSET; st.r[r].val = o * ci.data_align; } break; }
            case 0x12: st.cfa_reg = uleb(&p); st.cfa_off = sleb(&p) * ci.data_align; break;
            case 0x13: st.cfa_off = sleb(&p) * ci.data_align; break;
            case 0x14: { uint64_t r = uleb(&p), o = uleb(&p); if (r < 96) { st.r[r].kind = R_VAL_OFFSET; st.r[r].val = (int64_t)o * ci.data_align; } break; }
            case 0x15: { uint64_t r = uleb(&p); int64_t o = sleb(&p); if (r < 96) { st.r[r].kind = R_VAL_OFFSET; st.r[r].val = o * ci.data_align; } break; }
            case 0x16: { uleb(&p); uint64_t l = uleb(&p); p += l; break; }
            case 0x2d: break;                                       /* AArch64 negate_ra_state (pointer auth) */
            case 0x2e: uleb(&p); break;                             /* GNU_args_size */
            case 0x2f: { uint64_t r = uleb(&p), o = uleb(&p); if (r < 96) { st.r[r].kind = R_OFFSET; st.r[r].val = -(int64_t)o * ci.data_align; } break; }
            default: return -1;
            }
        }
    }
    uint64_t regval[96];
    for (int i = 0; i < 31; i++) regval[i] = c->x[i];
    regval[31] = c->sp;
    for (int i = 0; i < 32; i++) regval[64 + i] = 0;
    for (int i = 0; i < 8; i++) regval[72 + i] = c->d[i];
    if (st.cfa_reg >= 96) return -1;
    uint64_t cfa = regval[st.cfa_reg] + (uint64_t)st.cfa_off;
    struct uctx n = *c;
    for (int i = 0; i < 96; i++) {
        uint64_t v;
        switch (st.r[i].kind) {
        case R_OFFSET: v = rd64(cfa + (uint64_t)st.r[i].val); break;
        case R_VAL_OFFSET: v = cfa + (uint64_t)st.r[i].val; break;
        case R_REG: v = st.r[i].val < 96 ? regval[st.r[i].val] : 0; break;
        default: continue;
        }
        if (i < 31) n.x[i] = v;
        else if (i >= 72 && i < 80) n.d[i - 72] = v;
    }
    uint64_t ra = st.r[ci.ra_reg < 96 ? ci.ra_reg : 30].kind == R_UNDEF ? 0 : n.x[ci.ra_reg < 31 ? ci.ra_reg : 30];
    n.sp = cfa; n.pc = ra;
    *c = n;
    return ra ? 0 : -1;
}

/* step one guest frame: c describes the frame (pc is a return address into it) */
static int guest_step(struct uctx *c, const struct finfo *fi) {
    uint32_t enc = fi->encoding;
    switch (enc & 0x0f000000) {
    case 0x04000000: {   /* FRAME: x29/x30 at fp, saved pairs below fp */
        uint64_t fp = c->x[29], loc = fp;
        static const uint32_t pairs[5] = { 1, 2, 4, 8, 0x10 };
        for (int k = 0; k < 5; k++) if (enc & pairs[k]) { loc -= 8; c->x[19 + 2 * k] = rd64(loc); loc -= 8; c->x[20 + 2 * k] = rd64(loc); }
        for (int k = 0; k < 4; k++) if (enc & (0x100u << k)) { loc -= 8; c->d[2 * k] = rd64(loc); loc -= 8; c->d[2 * k + 1] = rd64(loc); }
        c->x[29] = rd64(fp); c->x[30] = rd64(fp + 8);
        c->sp = fp + 16; c->pc = c->x[30];
        return c->pc ? 0 : -1;
    }
    case 0x02000000: {   /* FRAMELESS: the return address is still in lr (only possible for the frame that threw) */
        uint64_t size = ((enc >> 12) & 0xfff) * 16, loc = c->sp + size;
        static const uint32_t pairs[5] = { 1, 2, 4, 8, 0x10 };
        for (int k = 0; k < 5; k++) if (enc & pairs[k]) { loc -= 8; c->x[19 + 2 * k] = rd64(loc); loc -= 8; c->x[20 + 2 * k] = rd64(loc); }
        for (int k = 0; k < 4; k++) if (enc & (0x100u << k)) { loc -= 8; c->d[2 * k] = rd64(loc); loc -= 8; c->d[2 * k + 1] = rd64(loc); }
        c->sp += size; c->pc = c->x[30];
        return c->pc ? 0 : -1;
    }
    case 0x03000000: return fi->fde ? dwarf_step(c, fi->fde) : -1;
    default: return -1;
    }
}
/* unwind info of the frame c is in (pc - 1: the call instruction) */
static int frame_info(const struct uctx *c, struct finfo *fi) {
    struct rt_image *im = rt_image_for_addr(c->pc - 1);
    if (!im) return -1;
    return compact_lookup(im, c->pc - 1, fi);
}
/* move to the caller's frame; crosses runtime frames back into the guest */
static int next_frame(struct uctx *c) {
    struct finfo fi;
    if (frame_info(c, &fi)) return -1;
    if (guest_step(c, &fi)) return -1;
    if (!c->pc) return -1;
    if (!is_guest(c->pc)) return host_step_to_guest(c) ? 0 : -1;
    return 0;
}

/* ---------------------------------------------------- exception objects */
struct uw_exc { uint64_t exception_class; void *cleanup; uint64_t private1, private2; };
struct exc {
    size_t refs;
    const void *type;               /* std::type_info of the thrown object (C++) */
    void (*dtor)(void *);
    struct exc *next;               /* caught-exceptions stack */
    int handlers;                   /* active catch clauses (negative: rethrown) */
    int selector;                   /* phase 1 result */
    uint64_t handler_sp;            /* frame of the handler found in phase 1 */
    void *adjusted;                 /* what the catch clause receives */
    int is_objc;
    int pad;
    struct uw_exc uw __attribute__((aligned(16)));
};
#define HDR (((sizeof(struct exc)) + 15) & ~(size_t)15)
static struct exc *of_obj(void *obj) { return (struct exc *)((char *)obj - HDR); }
static void *obj_of(struct exc *e) { return (char *)e + HDR; }
static struct exc *of_uw(void *uw) { return (struct exc *)((char *)uw - offsetof(struct exc, uw)); }
#define CLASS_CXX  0x434C4E47432B2B00ULL   /* "CLNGC++\0" */
#define CLASS_OBJC 0x434C4E474F424A43ULL   /* "CLNGOBJC" */

static __thread struct exc *t_caught;
static __thread int t_uncaught;

void *iosrt_cxa_allocate_exception(size_t n) {
    struct exc *e = rt_xcalloc(1, HDR + n);
    e->refs = 1;
    return obj_of(e);
}
void iosrt_cxa_free_exception(void *obj) { if (obj) free(of_obj(obj)); }
static void exc_destroy(struct exc *e) {
    if (e->is_objc) objc_release(*(id *)obj_of(e));
    else if (e->dtor) e->dtor(obj_of(e));
    free(e);
}

/* -------------------------------------------------------- type matching */
#ifdef IOSRT_WITH_CXX
extern char _ZTVN10__cxxabiv117__class_type_infoE[], _ZTVN10__cxxabiv120__si_class_type_infoE[],
            _ZTVN10__cxxabiv121__vmi_class_type_infoE[], _ZTVN10__cxxabiv119__pointer_type_infoE[];
#define VT(x) ((const void *)((x) + 16))
#endif
static void *objc_ehtype_vtable_addr(void);
static const char *ti_name(const void *ti) { return (const char *)((uintptr_t)((const void *const *)ti)[1] & ~(1ULL << 63)); }
static int same_type(const void *a, const void *b) { return a == b || (a && b && !strcmp(ti_name(a), ti_name(b))); }
static int ti_is(const void *ti, int kind) {   /* 1 class, 2 si, 3 vmi, 4 pointer */
#ifdef IOSRT_WITH_CXX
    const void *vp = *(const void *const *)ti;
    switch (kind) {
    case 1: return vp == VT(_ZTVN10__cxxabiv117__class_type_infoE);
    case 2: return vp == VT(_ZTVN10__cxxabiv120__si_class_type_infoE);
    case 3: return vp == VT(_ZTVN10__cxxabiv121__vmi_class_type_infoE);
    case 4: return vp == VT(_ZTVN10__cxxabiv119__pointer_type_infoE);
    }
#else
    (void)ti; (void)kind;
#endif
    return 0;
}
/* is `base` a (public) base class of `derived`? adjusts *obj to the base subobject */
static int find_base(const void *derived, const void *base, void **obj, int depth) {
    if (same_type(derived, base)) return 1;
    if (depth > 32) return 0;
    if (ti_is(derived, 2)) return find_base(((const void *const *)derived)[2], base, obj, depth + 1);
    if (ti_is(derived, 3)) {
        uint32_t n; memcpy(&n, (const char *)derived + 20, 4);
        const char *bases = (const char *)derived + 24;
        for (uint32_t i = 0; i < n; i++) {
            const void *bt; long of; memcpy(&bt, bases + i * 16, 8); memcpy(&of, bases + i * 16 + 8, 8);
            if (!(of & 2)) continue;   /* not public */
            void *sub = *obj;
            if (sub) {
                long off = of >> 8;
                if (of & 1) { char *vt = *(char **)sub; memcpy(&off, vt + off, 8); }
                sub = (char *)sub + off;
            }
            void *t = sub;
            if (find_base(bt, base, &t, depth + 1)) { *obj = t; return 1; }
        }
    }
    return 0;
}
/* can a catch clause of type `ct` catch exception e? sets e->adjusted */
static int can_catch(const void *ct, struct exc *e) {
    if (!ct) { e->adjusted = e->is_objc ? *(void **)obj_of(e) : obj_of(e); return 1; }     /* catch (...) */
    void *evt = objc_ehtype_vtable_addr();
    if (*(void *const *)ct == evt) {   /* Objective-C @catch */
        if (!e->is_objc) return 0;
        id o = *(id *)obj_of(e);
        Class want; memcpy(&want, (const char *)ct + 16, 8);
        int ok = !want;   /* @catch (id) */
        if (!ok) for (Class k = o ? o->isa : NULL; k; k = k->superclass) if (k == want) { ok = 1; break; }
        if (!ok && !want) ok = 1;
        if (!ok) {   /* class reference not bound (missing class): match by name */
            const char *nm; memcpy(&nm, (const char *)ct + 8, 8);
            for (Class k = o ? o->isa : NULL; k && nm; k = k->superclass) if (!strcmp(rt_class_name(k), nm)) { ok = 1; break; }
        }
        if (ok) e->adjusted = o;
        return ok;
    }
    if (e->is_objc || !e->type) return 0;
    void *obj = obj_of(e);
    /* catch (T *): the handler receives the (adjusted) pointer value itself */
    if (ti_is(ct, 4) && ti_is(e->type, 4)) {
        const void *cp = ((const void *const *)ct)[3], *tp = ((const void *const *)e->type)[3];
        void *ptr = *(void **)obj;
        if (same_type(ct, e->type) || same_type(cp, tp) || (cp && !strcmp(ti_name(cp), "v"))) { e->adjusted = ptr; return 1; }
        void *t = ptr;
        if (find_base(tp, cp, &t, 0)) { e->adjusted = t; return 1; }
        return 0;
    }
    if (same_type(ct, e->type)) { e->adjusted = ti_is(ct, 4) ? *(void **)obj : obj; return 1; }
    void *t = obj;
    if (find_base(e->type, ct, &t, 0)) { e->adjusted = t; return 1; }
    return 0;
}

/* ------------------------------------------------------------ personality */
/* the functions guests bind as personality routines (never called directly) */
void iosrt_gxx_personality_v0(void) { rt_guest_exit(72, "C++ personality routine called directly"); }
void iosrt_objc_personality_v0(void) { rt_guest_exit(72, "Objective-C personality routine called directly"); }
enum { P_CONTINUE, P_HANDLER, P_CLEANUP };
/* scan the LSDA for the call site of pc: phase 1 looks for a handler, phase 2 for
 * the landing pad to run (the handler in the handler frame, or a cleanup) */
static int personality(const struct finfo *fi, uint64_t pc, struct exc *e, int phase2, int handler_frame, uint64_t *lp, int *sel) {
    if (!fi->lsda) return P_CONTINUE;
    const uint8_t *p = (const uint8_t *)(uintptr_t)fi->lsda;
    uint64_t func = fi->func_start, ip = pc - 1;
    uint8_t lp_enc = *p++;
    uint64_t lp_start = lp_enc != PE_omit ? read_enc(&p, lp_enc, func) : func;
    uint8_t tt_enc = *p++;
    const uint8_t *tt_base = NULL;
    if (tt_enc != PE_omit) { uint64_t o = uleb(&p); tt_base = p + o; }
    uint8_t cs_enc = *p++;
    uint64_t cs_len = uleb(&p);
    const uint8_t *cs = p, *cs_end = p + cs_len, *actions = cs_end;
    while (cs < cs_end) {
        uint64_t start = read_enc(&cs, cs_enc, 0), len = read_enc(&cs, cs_enc, 0), pad = read_enc(&cs, cs_enc, 0), action = uleb(&cs);
        if (ip < func + start) return P_CONTINUE;   /* sorted: no entry covers ip */
        if (ip >= func + start + len) continue;
        if (!pad) return P_CONTINUE;
        *lp = lp_start + pad;
        if (!action) { *sel = 0; return phase2 ? P_CLEANUP : P_CONTINUE; }
        const uint8_t *ap = actions + action - 1;
        int has_cleanup = 0;
        for (;;) {
            int64_t ti = sleb(&ap);
            const uint8_t *nextp = ap;
            int64_t next = sleb(&ap);
            if (ti > 0 && tt_base) {
                size_t sz = (tt_enc & 0x0f) == 0x03 || (tt_enc & 0x0f) == 0x0b ? 4 : (tt_enc & 0x0f) == 0x02 || (tt_enc & 0x0f) == 0x0a ? 2 : 8;
                const uint8_t *ep = tt_base - (size_t)ti * sz;
                const void *ct = (const void *)(uintptr_t)read_enc(&ep, tt_enc, 0);
                if ((!phase2 || handler_frame) && can_catch(ct, e)) { *sel = (int)ti; return P_HANDLER; }
            } else if (ti == 0) has_cleanup = 1;
            /* ti < 0: exception specification; treated as not matching */
            if (!next) break;
            ap = nextp + next;
        }
        if (phase2 && has_cleanup) { *sel = 0; return P_CLEANUP; }
        return P_CONTINUE;
    }
    return P_CONTINUE;
}
static int is_our_personality(uint64_t p) {
    return p == (uint64_t)(uintptr_t)iosrt_gxx_personality_v0 || p == (uint64_t)(uintptr_t)iosrt_objc_personality_v0;
}

/* ------------------------------------------------------------- unwinding */
static int is_our_personality(uint64_t p);
/* phase 1 from c: is there a handler? (records it in e) */
static int g_trace = -1;
static void trace_frame(const char *what, const struct uctx *c, const struct finfo *fi) {
    if (g_trace < 0) g_trace = getenv("IOSRT_TRACE_EH") != NULL;
    if (!g_trace) return;
    struct rt_image *im = rt_image_for_addr(c->pc);
    uint64_t sl = im ? (uint64_t)im->slide : 0;
    rt_warn("eh %s: pc=0x%llx sp=0x%llx fp=0x%llx func=0x%llx enc=0x%08x lsda=%s pers=%s", what,
            (unsigned long long)(c->pc - sl), (unsigned long long)c->sp, (unsigned long long)c->x[29],
            (unsigned long long)(fi ? fi->func_start - sl : 0), fi ? fi->encoding : 0, fi && fi->lsda ? "yes" : "no",
            !fi ? "-" : is_our_personality(fi->personality) ? "ours" : fi->personality ? "other" : "none");
}
static int search(struct uctx c, struct exc *e) {
    for (int n = 0; n < 10000; n++) {
        struct finfo fi;
        if (frame_info(&c, &fi)) { trace_frame("no unwind info", &c, NULL); return 0; }
        trace_frame("search", &c, &fi);
        if (is_our_personality(fi.personality)) {
            uint64_t lp; int sel;
            if (personality(&fi, c.pc, e, 0, 0, &lp, &sel) == P_HANDLER) { e->handler_sp = c.sp; e->selector = sel; return 1; }
        }
        if (next_frame(&c)) return 0;
    }
    return 0;
}
/* phase 2 from c: run cleanups up to the handler, then enter it */
_Noreturn static void cleanup_and_enter(struct uctx c, struct exc *e) {
    for (int n = 0; n < 10000; n++) {
        struct finfo fi;
        if (frame_info(&c, &fi)) break;
        if (is_our_personality(fi.personality)) {
            uint64_t lp = 0; int sel = 0;
            int hf = c.sp == e->handler_sp;
            int r = personality(&fi, c.pc, e, 1, hf, &lp, &sel);
            if (r == P_HANDLER || r == P_CLEANUP) {
                c.x[0] = (uint64_t)(uintptr_t)&e->uw; c.x[1] = (uint64_t)(int64_t)sel; c.pc = lp;
                iosrt_uw_install(&c);
            }
        }
        if (next_frame(&c)) break;
    }
    rt_guest_exit(72, "exception unwinding lost the handler frame");
}
void iosrt_cxx_throw(void *obj, void *tinfo, void (*dest)(void *));   /* cxx_abi.c: describes the exception */
_Noreturn static void uncaught(struct exc *e) {
    if (e->is_objc) {
        extern void *iosrt_uncaught_handler;
        id o = *(id *)obj_of(e);
        if (iosrt_uncaught_handler) { void *h = iosrt_uncaught_handler; iosrt_uncaught_handler = NULL; ((void (*)(id))h)(o); }
        char msg[1024];
        id name = rt_responds(o, rt_sel("name")) ? RT_SEND0(id, o, "name") : NULL;
        id reason = rt_responds(o, rt_sel("reason")) ? RT_SEND0(id, o, "reason") : NULL;
        if (name || reason) snprintf(msg, sizeof msg, "uncaught exception '%s', reason: '%s'", name ? rt_str_utf8(name) : "?", reason ? rt_str_utf8(reason) : "?");
        else { char *d = rt_describe(o); snprintf(msg, sizeof msg, "uncaught exception: %s", d); free(d); }
        rt_guest_exit(72, msg);
    }
#ifdef IOSRT_WITH_CXX
    iosrt_cxx_throw(obj_of(e), (void *)e->type, e->dtor);
#endif
    rt_guest_exit(72, "uncaught C++ exception");
}
_Noreturn static void raise_exc(struct exc *e) {
    struct uctx c;
    t_uncaught++;
    if (!capture_guest_caller(&c) || !search(c, e)) {
        if (getenv("IOSRT_VERBOSE")) rt_print_backtrace();
        uncaught(e);
    }
    cleanup_and_enter(c, e);
}

/* ------------------------------------------------------------- C++ ABI */
_Noreturn void iosrt_cxa_throw(void *obj, void *tinfo, void (*dest)(void *)) {
    struct exc *e = of_obj(obj);
    e->type = tinfo; e->dtor = dest; e->is_objc = 0;
    e->uw.exception_class = CLASS_CXX;
    raise_exc(e);
}
_Noreturn void iosrt_unwind_resume(void *uw) {
    struct exc *e = of_uw(uw);
    struct uctx c;
    if (!capture_guest_caller(&c)) uncaught(e);
    cleanup_and_enter(c, e);
}
void *iosrt_cxa_begin_catch(void *uw) {
    if (!uw) return NULL;
    struct exc *e = of_uw(uw);
    e->handlers = e->handlers < 0 ? -e->handlers + 1 : e->handlers + 1;
    if (t_caught != e) { e->next = t_caught; t_caught = e; }
    if (t_uncaught > 0) t_uncaught--;
    return e->adjusted ? e->adjusted : obj_of(e);
}
void iosrt_cxa_end_catch(void) {
    struct exc *e = t_caught;
    if (!e) return;
    if (e->handlers < 0) {          /* rethrown: the new throw owns it */
        if (++e->handlers == 0) t_caught = e->next;
        return;
    }
    if (--e->handlers == 0) {
        t_caught = e->next;
        if (--e->refs == 0) exc_destroy(e);
    }
}
_Noreturn void iosrt_cxa_rethrow(void) {
    struct exc *e = t_caught;
    if (!e) rt_guest_exit(134, "throw; with no exception being handled (std::terminate)");
    e->handlers = -e->handlers;
    raise_exc(e);
}
void *iosrt_cxa_get_exception_ptr(void *uw) { struct exc *e = of_uw(uw); return e->adjusted ? e->adjusted : obj_of(e); }
const void *iosrt_cxa_current_exception_type(void) { return t_caught && !t_caught->is_objc ? t_caught->type : NULL; }
long iosrt_uncaught_exception(void) { return t_uncaught > 0; }
int iosrt_uncaught_exceptions(void) { return t_uncaught; }
_Noreturn void iosrt_terminate(void) {
    if (t_caught) uncaught(t_caught);
    rt_guest_exit(134, "std::terminate called");
}
_Noreturn void iosrt_cxa_call_unexpected(void *uw) { (void)uw; iosrt_terminate(); }
/* std::exception_ptr { void *__ptr_; } holding the thrown object */
void *iosrt_current_exception(void) { struct exc *e = t_caught; if (!e) return NULL; e->refs++; return obj_of(e); }
void iosrt_exception_ptr_copy(void **dst, void *const *src) { *dst = *src; if (*dst) of_obj(*dst)->refs++; }
void iosrt_exception_ptr_dtor(void **p) { if (*p) { struct exc *e = of_obj(*p); if (--e->refs == 0) exc_destroy(e); *p = NULL; } }
void **iosrt_exception_ptr_assign(void **dst, void *const *src) {
    if (*src) of_obj(*src)->refs++;
    void *old = *dst; *dst = *src;
    if (old) { struct exc *e = of_obj(old); if (--e->refs == 0) exc_destroy(e); }
    return dst;
}
_Noreturn void iosrt_rethrow_exception(void *p) {   /* exception_ptr passed by value: its pointer */
    if (!p) iosrt_terminate();
    struct exc *e = of_obj(p);
    e->refs++; e->handlers = 0; e->adjusted = NULL;
    raise_exc(e);
}

/* -------------------------------------------------------- Objective-C ABI */
/* objc_ehtype_vtable: guests point their EHTYPEs at &vtable[2] */
void *iosrt_objc_ehtype_vtable[4];
static void *objc_ehtype_vtable_addr(void) { return &iosrt_objc_ehtype_vtable[2]; }
struct objc_ehtype { void *vt; const char *name; Class cls; };
struct objc_ehtype iosrt_OBJC_EHTYPE_id, iosrt_OBJC_EHTYPE_NSException;
_Noreturn void objc_exception_throw(id o) {
    void *obj = iosrt_cxa_allocate_exception(sizeof(id));
    *(id *)obj = objc_retain(o);
    struct exc *e = of_obj(obj);
    e->is_objc = 1; e->uw.exception_class = CLASS_OBJC;
    raise_exc(e);
}
_Noreturn void objc_exception_rethrow(void) { iosrt_cxa_rethrow(); }
id objc_begin_catch(void *uw) { return (id)iosrt_cxa_begin_catch(uw); }
void objc_end_catch(void) { iosrt_cxa_end_catch(); }
_Noreturn void objc_terminate(void) { iosrt_terminate(); }
void iosrt_objc_personality(void) { iosrt_objc_personality_v0(); }

void rt_unwind_init(void) {
    iosrt_OBJC_EHTYPE_id.vt = objc_ehtype_vtable_addr(); iosrt_OBJC_EHTYPE_id.name = "id"; iosrt_OBJC_EHTYPE_id.cls = NULL;
    iosrt_OBJC_EHTYPE_NSException.vt = objc_ehtype_vtable_addr(); iosrt_OBJC_EHTYPE_NSException.name = "NSException";
    iosrt_OBJC_EHTYPE_NSException.cls = rt_class_named("NSException");
}
