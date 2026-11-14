/*
 * LZFSE and LZVN decompression (the codecs of compiled asset catalogs),
 * written from the published description of the formats: a stream of blocks
 * "bvx-" (raw), "bvxn" (LZVN), "bvx1"/"bvx2" (LZFSE: literals and
 * literal-length/match-length/distance triples entropy-coded with finite state
 * entropy, read backwards), ended by "bvx$".
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define L_SYMS 20
#define M_SYMS 20
#define D_SYMS 64
#define LIT_SYMS 256
#define L_STATES 64
#define M_STATES 64
#define D_STATES 256
#define LIT_STATES 1024

static const uint8_t l_extra[L_SYMS] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 3, 5, 8 };
static const int32_t l_base[L_SYMS] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 20, 28, 60 };
static const uint8_t m_extra[M_SYMS] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 5, 8, 11 };
static const int32_t m_base[M_SYMS] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 24, 56, 312 };
static const uint8_t d_extra[D_SYMS] = { 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7,
                                         8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 11, 12, 12, 12, 12, 13, 13, 13, 13,
                                         14, 14, 14, 14, 15, 15, 15, 15 };
static const int32_t d_base[D_SYMS] = { 0, 1, 2, 3, 4, 6, 8, 10, 12, 16, 20, 24, 28, 36, 44, 52, 60, 76, 92, 108, 124, 156, 188, 220,
                                        252, 316, 380, 444, 508, 636, 764, 892, 1020, 1276, 1532, 1788, 2044, 2556, 3068, 3580,
                                        4092, 5116, 6140, 7164, 8188, 10236, 12284, 14332, 16380, 20476, 24572, 28668, 32764,
                                        40956, 49148, 57340, 65532, 81916, 98300, 114684, 131068, 163836, 196604, 229372 };

/* ------------------------------------------------- FSE tables and input */
struct lit_entry { int8_t k; uint8_t sym; int16_t delta; };
struct val_entry { uint8_t total_bits, value_bits; int16_t delta; int32_t vbase; };

static int init_lit_table(int nstates, int nsyms, const uint16_t *freq, struct lit_entry *t) {
    int nclz = __builtin_clz((unsigned)nstates), sum = 0;
    for (int i = 0; i < nsyms; i++) {
        int f = freq[i];
        if (!f) continue;
        sum += f;
        if (sum > nstates) return -1;
        int k = __builtin_clz((unsigned)f) - nclz, j0 = ((2 * nstates) >> k) - f;
        for (int j = 0; j < f; j++, t++) {
            t->sym = (uint8_t)i;
            if (j < j0) { t->k = (int8_t)k; t->delta = (int16_t)(((f + j) << k) - nstates); }
            else { t->k = (int8_t)(k - 1); t->delta = (int16_t)((j - j0) << (k - 1)); }
        }
    }
    return 0;
}
static int init_val_table(int nstates, int nsyms, const uint16_t *freq, const uint8_t *vbits, const int32_t *vbase, struct val_entry *t) {
    int nclz = __builtin_clz((unsigned)nstates), sum = 0;
    for (int i = 0; i < nsyms; i++) {
        int f = freq[i];
        if (!f) continue;
        sum += f;
        if (sum > nstates) return -1;
        int k = __builtin_clz((unsigned)f) - nclz, j0 = ((2 * nstates) >> k) - f;
        for (int j = 0; j < f; j++, t++) {
            t->value_bits = vbits[i]; t->vbase = vbase[i];
            if (j < j0) { t->total_bits = (uint8_t)(k + vbits[i]); t->delta = (int16_t)(((f + j) << k) - nstates); }
            else { t->total_bits = (uint8_t)(k - 1 + vbits[i]); t->delta = (int16_t)((j - j0) << (k - 1)); }
        }
    }
    return 0;
}
/* bits are read backwards from the end of a payload */
struct in64 { uint64_t accum; int nbits; };
static uint64_t mask64(uint64_t x, int n) { return n >= 64 ? x : n <= 0 ? 0 : x & ((1ULL << n) - 1); }
static int in_init(struct in64 *s, int n, const uint8_t **p, const uint8_t *start) {
    if (n) {
        if (*p < start + 8) return -1;
        *p -= 8; memcpy(&s->accum, *p, 8); s->nbits = n + 64;
    } else {
        if (*p < start + 7) return -1;
        *p -= 7; s->accum = 0; memcpy(&s->accum, *p, 7); s->nbits = 56;
    }
    if (s->nbits < 56 || s->nbits >= 64 || (s->accum >> s->nbits) != 0) return -1;
    return 0;
}
static int in_flush(struct in64 *s, const uint8_t **p, const uint8_t *start) {
    int nbits = (63 - s->nbits) & -8, nbytes = nbits >> 3;
    if (*p - nbytes < start) return -1;
    *p -= nbytes;
    uint64_t in = 0; memcpy(&in, *p, 8);
    s->accum = nbits ? (s->accum << nbits) | mask64(in, nbits) : s->accum;
    s->nbits += nbits;
    return 0;
}
static uint64_t in_pull(struct in64 *s, int n) {
    s->nbits -= n;
    uint64_t r = s->accum >> s->nbits;
    s->accum = mask64(s->accum, s->nbits);
    return r;
}

/* ----------------------------------------------------------- LZFSE block */
struct lzfse_hdr {
    uint32_t n_raw, n_payload, n_literals, n_matches, n_lit_payload, n_lmd_payload;
    int32_t literal_bits, lmd_bits;
    uint16_t lit_state[4], l_state, m_state, d_state;
    uint16_t l_freq[L_SYMS], m_freq[M_SYMS], d_freq[D_SYMS], lit_freq[LIT_SYMS];
};
static uint64_t field(uint64_t v, int off, int n) { return (v >> off) & ((1ULL << n) - 1); }
static int freq_value(uint32_t bits, int *nbits) {
    static const int8_t nb[32] = { 2, 3, 2, 5, 2, 3, 2, 8, 2, 3, 2, 5, 2, 3, 2, 14, 2, 3, 2, 5, 2, 3, 2, 8, 2, 3, 2, 5, 2, 3, 2, 14 };
    static const int8_t val[32] = { 0, 2, 1, 4, 0, 3, 1, -1, 0, 2, 1, 5, 0, 3, 1, -1, 0, 2, 1, 6, 0, 3, 1, -1, 0, 2, 1, 7, 0, 3, 1, -1 };
    uint32_t b = bits & 31; int n = nb[b];
    *nbits = n;
    if (n == 8) return 8 + (int)((bits >> 4) & 0xf);
    if (n == 14) return 24 + (int)((bits >> 4) & 0x3ff);
    return val[b];
}
/* parses a v1/v2 header at p; returns its size or -1 */
static long parse_hdr(const uint8_t *p, const uint8_t *end, int v2, struct lzfse_hdr *h) {
    memset(h, 0, sizeof *h);
    if (!v2) {   /* v1: plain fields and uint16 frequency tables */
        const size_t sz = 4 * 7 + 4 + 8 + 4 + 6 + 2 * (L_SYMS + M_SYMS + D_SYMS + LIT_SYMS);
        if (end - p < (long)sz) return -1;
        const uint8_t *q = p + 4;
        memcpy(&h->n_raw, q, 4); memcpy(&h->n_payload, q + 4, 4); memcpy(&h->n_literals, q + 8, 4); memcpy(&h->n_matches, q + 12, 4);
        memcpy(&h->n_lit_payload, q + 16, 4); memcpy(&h->n_lmd_payload, q + 20, 4); memcpy(&h->literal_bits, q + 24, 4);
        memcpy(h->lit_state, q + 28, 8); memcpy(&h->lmd_bits, q + 36, 4);
        memcpy(&h->l_state, q + 40, 2); memcpy(&h->m_state, q + 42, 2); memcpy(&h->d_state, q + 44, 2);
        q += 46;
        memcpy(h->l_freq, q, sizeof h->l_freq); q += sizeof h->l_freq;
        memcpy(h->m_freq, q, sizeof h->m_freq); q += sizeof h->m_freq;
        memcpy(h->d_freq, q, sizeof h->d_freq); q += sizeof h->d_freq;
        memcpy(h->lit_freq, q, sizeof h->lit_freq);
        return (long)sz;
    }
    if (end - p < 32) return -1;
    uint64_t v[3]; memcpy(&h->n_raw, p + 4, 4); memcpy(v, p + 8, 24);
    h->n_literals = (uint32_t)field(v[0], 0, 20); h->n_lit_payload = (uint32_t)field(v[0], 20, 20);
    h->n_matches = (uint32_t)field(v[0], 40, 20); h->literal_bits = (int32_t)field(v[0], 60, 3) - 7;
    for (int i = 0; i < 4; i++) h->lit_state[i] = (uint16_t)field(v[1], 10 * i, 10);
    h->n_lmd_payload = (uint32_t)field(v[1], 40, 20); h->lmd_bits = (int32_t)field(v[1], 60, 3) - 7;
    uint32_t hsize = (uint32_t)field(v[2], 0, 32);
    h->l_state = (uint16_t)field(v[2], 32, 10); h->m_state = (uint16_t)field(v[2], 42, 10); h->d_state = (uint16_t)field(v[2], 52, 10);
    if (hsize < 32 || (long)hsize > end - p) return -1;
    h->n_payload = h->n_lit_payload + h->n_lmd_payload;
    uint16_t *dst[4] = { h->l_freq, h->m_freq, h->d_freq, h->lit_freq };
    int cnt[4] = { L_SYMS, M_SYMS, D_SYMS, LIT_SYMS };
    const uint8_t *src = p + 32, *src_end = p + hsize;
    uint32_t accum = 0; int an = 0;
    if (hsize > 32) {
        for (int t = 0; t < 4; t++)
            for (int i = 0; i < cnt[t]; i++) {
                while (src < src_end && an + 8 <= 32) { accum |= (uint32_t)*src++ << an; an += 8; }
                int nb; int val = freq_value(accum, &nb);
                if (nb > an) return -1;
                dst[t][i] = (uint16_t)val; accum >>= nb; an -= nb;
            }
    }
    return hsize;
}
/* `lo` bounds the backwards reads: flushes may pull bytes from before the payload
 * (the block header), whose bits are never used */
static long lzfse_block(const struct lzfse_hdr *h, const uint8_t *lo, const uint8_t *payload, uint8_t *out, size_t out_cap, size_t out_pos) {
    static __thread struct lit_entry lit_t[LIT_STATES];
    static __thread struct val_entry l_t[L_STATES], m_t[M_STATES], d_t[D_STATES];
    if (init_lit_table(LIT_STATES, LIT_SYMS, h->lit_freq, lit_t) || init_val_table(L_STATES, L_SYMS, h->l_freq, l_extra, l_base, l_t) ||
        init_val_table(M_STATES, M_SYMS, h->m_freq, m_extra, m_base, m_t) || init_val_table(D_STATES, D_SYMS, h->d_freq, d_extra, d_base, d_t))
        return -1;
    /* literals: four interleaved states */
    uint8_t *lits = malloc(h->n_literals + 64);
    if (!lits) return -1;
    {
        const uint8_t *p = payload + h->n_lit_payload; struct in64 in;
        if (in_init(&in, h->literal_bits, &p, lo)) { free(lits); return -1; }
        uint16_t st[4] = { h->lit_state[0], h->lit_state[1], h->lit_state[2], h->lit_state[3] };
        for (uint32_t i = 0; i < h->n_literals; i += 4) {
            if (in_flush(&in, &p, lo)) { free(lits); return -1; }
            for (int k = 0; k < 4; k++) {
                if (st[k] >= LIT_STATES) { free(lits); return -1; }
                struct lit_entry e = lit_t[st[k]];
                lits[i + k] = e.sym;
                st[k] = (uint16_t)(e.delta + (int)in_pull(&in, e.k));
            }
        }
    }
    /* literal / match / distance triples */
    const uint8_t *lmd = payload + h->n_lit_payload, *p = lmd + h->n_lmd_payload;
    struct in64 in;
    if (in_init(&in, h->lmd_bits, &p, lo)) { free(lits); return -1; }
    uint16_t ls = h->l_state, ms = h->m_state, ds = h->d_state;
    int64_t D = -1; const uint8_t *lit = lits; size_t o = out_pos;
    for (uint32_t n = 0; n < h->n_matches; n++) {
        if (in_flush(&in, &p, lo) || ls >= L_STATES || ms >= M_STATES || ds >= D_STATES) { free(lits); return -1; }
        struct val_entry e;
        uint64_t b;
        e = l_t[ls]; b = in_pull(&in, e.total_bits); ls = (uint16_t)(e.delta + (int)(b >> e.value_bits)); int64_t L = e.vbase + (int64_t)mask64(b, e.value_bits);
        e = m_t[ms]; b = in_pull(&in, e.total_bits); ms = (uint16_t)(e.delta + (int)(b >> e.value_bits)); int64_t M = e.vbase + (int64_t)mask64(b, e.value_bits);
        e = d_t[ds]; b = in_pull(&in, e.total_bits); ds = (uint16_t)(e.delta + (int)(b >> e.value_bits)); int64_t nd = e.vbase + (int64_t)mask64(b, e.value_bits);
        if (nd) D = nd;
        if (o + (size_t)L + (size_t)M > out_cap || lit + L > lits + h->n_literals) { free(lits); return -1; }
        memcpy(out + o, lit, (size_t)L); o += (size_t)L; lit += L;
        if (M) {
            if (D <= 0 || (size_t)D > o) { free(lits); return -1; }
            for (int64_t k = 0; k < M; k++, o++) out[o] = out[o - (size_t)D];
        }
    }
    free(lits);
    return (long)(o - out_pos);
}

/* --------------------------------------------------------------- LZVN */
static long lzvn_block(const uint8_t *src, size_t n, uint8_t *out, size_t out_cap, size_t out_pos) {
    const uint8_t *p = src, *end = src + n;
    size_t o = out_pos; size_t D = 0;
    while (p < end) {
        uint8_t op = *p;
        size_t L = 0, M = 0, len = 1;
        if (op == 0x06) break;                                        /* end of stream */
        if (op == 0x0e || op == 0x16) { p++; continue; }              /* nop */
        if ((op >= 0x70 && op <= 0x7f) || (op >= 0xd0 && op <= 0xdf) || op == 0x1e || op == 0x26 || op == 0x2e || op == 0x36 || op == 0x3e) return -1;
        if (op >= 0xf0) {                                             /* match with the previous distance */
            if (op == 0xf0) { if (p + 2 > end) return -1; M = (size_t)p[1] + 16; len = 2; } else M = op & 0xf;
        } else if (op >= 0xe0) {                                      /* literals */
            if (op == 0xe0) { if (p + 2 > end) return -1; L = (size_t)p[1] + 16; len = 2; } else L = op & 0xf;
        } else if (op >= 0xa0 && op <= 0xbf) {                        /* medium distance */
            if (p + 3 > end) return -1;
            uint16_t v = (uint16_t)(p[1] | p[2] << 8);
            L = (op >> 3) & 3; M = ((size_t)((op & 7) << 2) | (v & 3)) + 3; D = v >> 2; len = 3;
        } else {
            L = op >> 6; M = ((op >> 3) & 7) + 3;
            switch (op & 7) {
            case 7: if (p + 3 > end) return -1; D = (size_t)(p[1] | p[2] << 8); len = 3; break;   /* large distance */
            case 6: len = 1; break;                                                               /* previous distance */
            default: if (p + 2 > end) return -1; D = (size_t)(op & 7) << 8 | p[1]; len = 2; break; /* small distance */
            }
        }
        p += len;
        if (L) {
            if (p + L > end || o + L > out_cap) return -1;
            memcpy(out + o, p, L); p += L; o += L;
        }
        if (M) {
            if (!D || D > o || o + M > out_cap) return -1;
            for (size_t k = 0; k < M; k++, o++) out[o] = out[o - D];
        }
    }
    return (long)(o - out_pos);
}

/* decompress a whole stream into out (capacity cap); returns the size or -1 */
long rt_lzfse_decode(const uint8_t *src, size_t n, uint8_t *out, size_t cap) {
    const uint8_t *p = src, *end = src + n;
    size_t o = 0;
    while (p + 4 <= end) {
        uint32_t magic; memcpy(&magic, p, 4);
        if (magic == 0x24787662) return (long)o;                      /* bvx$ */
        if (magic == 0x2d787662) {                                    /* bvx- raw */
            if (p + 8 > end) return -1;
            uint32_t r; memcpy(&r, p + 4, 4);
            if (p + 8 + r > end || o + r > cap) return -1;
            memcpy(out + o, p + 8, r); o += r; p += 8 + r;
        } else if (magic == 0x6e787662) {                             /* bvxn LZVN */
            if (p + 12 > end) return -1;
            uint32_t raw, pay; memcpy(&raw, p + 4, 4); memcpy(&pay, p + 8, 4);
            if (p + 12 + pay > end || o + raw > cap) return -1;
            long got = lzvn_block(p + 12, pay, out, o + raw, o);
            if (got != (long)raw) return -1;
            o += raw; p += 12 + pay;
        } else if (magic == 0x31787662 || magic == 0x32787662) {     /* bvx1 / bvx2 LZFSE */
            struct lzfse_hdr h;
            long hs = parse_hdr(p, end, magic == 0x32787662, &h);
            if (hs < 0 || p + hs + h.n_payload > end || o + h.n_raw > cap) return -1;
            long got = lzfse_block(&h, src, p + hs, out, o + h.n_raw, o);
            if (got != (long)h.n_raw) return -1;
            o += h.n_raw; p += hs + h.n_payload;
        } else return -1;
    }
    return (long)o;
}
