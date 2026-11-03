/*
 * System APIs that apps call at startup but that have no meaningful Android
 * equivalent. Each one reports a *plausible failure* (or a harmless success) so
 * callers take their normal error path instead of reading uninitialised output:
 * Mach host/task/port/VM, dyld image introspection, Security keychain,
 * SystemConfiguration reachability, CommonCrypto digests, a few BSD calls.
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/utsname.h>

#define KERN_SUCCESS 0
#define KERN_FAILURE 5
#define KERN_NOT_SUPPORTED 46

/* ---------------------------------------------------------------- mach */
uint32_t iosrt_mach_host_self(void) { return 0x203; }
uint32_t iosrt_mach_thread_self(void) { return 0x303; }
int iosrt_host_page_size(uint32_t host, unsigned long *out) { (void)host; if (out) *out = 16384; return KERN_SUCCESS; }
int iosrt_kern_failure(void) { return KERN_FAILURE; }
int iosrt_kern_success(void) { return KERN_SUCCESS; }
const char *iosrt_mach_error_string(int e) { (void)e; return "(os/kern) failure (iosrt)"; }
int iosrt_mach_wait_until(uint64_t deadline) {
    extern uint64_t iosrt_mach_absolute_time(void);
    uint64_t now = iosrt_mach_absolute_time();
    if (deadline > now) { uint64_t ns = deadline - now; struct timespec ts = { (time_t)(ns / 1000000000ull), (long)(ns % 1000000000ull) }; nanosleep(&ts, NULL); }
    return KERN_SUCCESS;
}
unsigned long iosrt_vm_page_mask = 16383;
uint32_t iosrt_NDR_record[2];
uint32_t iosrt_pthread_mach_thread_np(pthread_t t) { (void)t; return 0x303; }
void *iosrt_pthread_get_stackaddr_np(pthread_t t) {
    pthread_attr_t a; void *addr = NULL; size_t sz = 0;
    if (pthread_getattr_np(t, &a) == 0) { pthread_attr_getstack(&a, &addr, &sz); pthread_attr_destroy(&a); }
    return (char *)addr + sz;        /* Darwin returns the top of the stack */
}
size_t iosrt_pthread_get_stacksize_np(pthread_t t) {
    pthread_attr_t a; void *addr = NULL; size_t sz = 0;
    if (pthread_getattr_np(t, &a) == 0) { pthread_attr_getstack(&a, &addr, &sz); pthread_attr_destroy(&a); }
    return sz;
}

/* ----------------------------------------------------- dyld introspection */
uint32_t iosrt_dyld_image_count(void) { return rt_main_image ? 1 : 0; }
const void *iosrt_dyld_get_image_header(uint32_t i) { return i == 0 && rt_main_image ? (const void *)(uintptr_t)rt_main_image->header_addr : NULL; }
intptr_t iosrt_dyld_get_image_vmaddr_slide(uint32_t i) { return i == 0 && rt_main_image ? rt_main_image->slide : 0; }
const char *iosrt_dyld_get_image_name(uint32_t i) { return i == 0 && rt_main_image ? rt_main_image->path : NULL; }
void iosrt_dyld_register_func_for_add_image(void (*fn)(const void *, intptr_t)) {
    if (fn && rt_main_image) fn((const void *)(uintptr_t)rt_main_image->header_addr, rt_main_image->slide);
}
void iosrt_dyld_register_func_for_remove_image(void (*fn)(const void *, intptr_t)) { (void)fn; }
struct darwin_dl_info { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; };
int iosrt_dladdr(const void *addr, struct darwin_dl_info *info) {
    uint64_t off = 0;
    const char *s = rt_image_symbolize((uint64_t)(uintptr_t)addr, &off);
    if (!s || !rt_main_image) { memset(info, 0, sizeof *info); return 0; }
    info->dli_fname = rt_main_image->path; info->dli_fbase = (void *)(uintptr_t)rt_main_image->header_addr;
    info->dli_sname = s; info->dli_saddr = (void *)((uintptr_t)addr - off);
    return 1;
}
/* getsectdatafromheader_64(header, seg, sect, &size) */
void *iosrt_getsectdatafromheader_64(const void *mh, const char *seg, const char *sect, unsigned long *size) {
    (void)mh;
    if (!rt_main_image) { if (size) *size = 0; return NULL; }
    struct rt_section *s = rt_image_section(rt_main_image, seg, sect);
    if (size) *size = s ? s->size : 0;
    return s ? (void *)(uintptr_t)(s->addr - (uint64_t)rt_main_image->slide) : NULL;   /* unslid, as on Darwin */
}
const char *iosrt_getprogname(void) {
    const char *p = rt_main_image ? strrchr(rt_main_image->path, '/') : NULL;
    return p ? p + 1 : "app";
}
int iosrt_backtrace(void **buf, int n) { (void)buf; (void)n; return 0; }

/* ----------------------------------------------------------- BSD odds */
struct darwin_utsname { char sysname[256], nodename[256], release[256], version[256], machine[256]; };
int iosrt_uname(struct darwin_utsname *u) {
    memset(u, 0, sizeof *u);
    strcpy(u->sysname, "Darwin"); strcpy(u->nodename, "iosrt"); strcpy(u->release, "18.0.0");
    strcpy(u->version, "Darwin Kernel Version 18.0.0 (iosrt)"); strcpy(u->machine, "iPhone10,3");
    return 0;
}
int iosrt_getifaddrs(void **ifap) { *ifap = NULL; return 0; }     /* no interfaces visible */
void iosrt_freeifaddrs(void *p) { (void)p; }
int iosrt_sysctl(int *name, unsigned n, void *old, size_t *oldlen, void *nw, size_t nwlen) {
    (void)name; (void)n; (void)old; (void)nw; (void)nwlen;
    if (oldlen) *oldlen = 0;
    return -1;
}
int iosrt_setxattr(const char *p, const char *n, const void *v, size_t sz, uint32_t pos, int opt) { (void)p; (void)n; (void)v; (void)sz; (void)pos; (void)opt; return 0; }
int iosrt_getxattr(const char *p, const char *n, void *v, size_t sz, uint32_t pos, int opt) { (void)p; (void)n; (void)v; (void)sz; (void)pos; (void)opt; return -1; }

/* -------------------------------------------------------------- Security */
#define errSecItemNotFound (-25300)
int32_t iosrt_SecItemCopyMatching(const void *q, const void **out) { (void)q; if (out) *out = NULL; return errSecItemNotFound; }
int32_t iosrt_SecItemAdd(const void *a, const void **out) { (void)a; if (out) *out = NULL; return 0; }
int32_t iosrt_SecItemUpdate(const void *q, const void *a) { (void)q; (void)a; return errSecItemNotFound; }
int32_t iosrt_SecItemDelete(const void *q) { (void)q; return errSecItemNotFound; }
void iosrt_arc4random_buf(void *b, size_t n);
int iosrt_SecRandomCopyBytes(const void *rnd, size_t n, void *out) { (void)rnd; iosrt_arc4random_buf(out, n); return 0; }
void *iosrt_kSecRandomDefault = NULL;

/* ---------------------------------------------- SystemConfiguration */
void *iosrt_SCNetworkReachabilityCreateWithName(void *alloc, const char *name) { (void)alloc; (void)name; return NULL; }
void *iosrt_SCNetworkReachabilityCreateWithAddress(void *alloc, const void *addr) { (void)alloc; (void)addr; return NULL; }
rt_bool iosrt_SCNetworkReachabilityGetFlags(void *r, uint32_t *flags) { (void)r; if (flags) *flags = 0; return 0; }
rt_bool iosrt_SCNetworkReachabilitySetCallback(void *r, void *cb, void *ctx) { (void)r; (void)cb; (void)ctx; return 0; }
rt_bool iosrt_SCNetworkReachabilityScheduleWithRunLoop(void *r, void *rl, void *mode) { (void)r; (void)rl; (void)mode; return 0; }
rt_bool iosrt_SCNetworkReachabilityUnscheduleFromRunLoop(void *r, void *rl, void *mode) { (void)r; (void)rl; (void)mode; return 0; }
rt_bool iosrt_SCNetworkReachabilitySetDispatchQueue(void *r, void *q) { (void)r; (void)q; return 0; }

/* ---------------------------------------------------- CommonCrypto */
/* MD5 (RFC 1321) */
typedef struct { uint32_t a, b, c, d; uint64_t len; uint8_t buf[64]; uint32_t n; uint8_t pad[12]; } md5_ctx;
static const uint32_t K5[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391 };
static const uint8_t R5[64] = { 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
                                4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21 };
#define ROL(x, c) (((x) << (c)) | ((x) >> (32 - (c))))
static void md5_block(md5_ctx *c, const uint8_t *p) {
    uint32_t m[16]; for (int i = 0; i < 16; i++) m[i] = (uint32_t)p[i * 4] | (uint32_t)p[i * 4 + 1] << 8 | (uint32_t)p[i * 4 + 2] << 16 | (uint32_t)p[i * 4 + 3] << 24;
    uint32_t a = c->a, b = c->b, cc = c->c, d = c->d;
    for (int i = 0; i < 64; i++) {
        uint32_t f; int g;
        if (i < 16) { f = (b & cc) | (~b & d); g = i; } else if (i < 32) { f = (d & b) | (~d & cc); g = (5 * i + 1) % 16; }
        else if (i < 48) { f = b ^ cc ^ d; g = (3 * i + 5) % 16; } else { f = cc ^ (b | ~d); g = (7 * i) % 16; }
        uint32_t t = d; d = cc; cc = b; b = b + ROL(a + f + K5[i] + m[g], R5[i]); a = t;
    }
    c->a += a; c->b += b; c->c += cc; c->d += d;
}
int iosrt_CC_MD5_Init(md5_ctx *c) { memset(c, 0, sizeof *c); c->a = 0x67452301; c->b = 0xefcdab89; c->c = 0x98badcfe; c->d = 0x10325476; return 1; }
int iosrt_CC_MD5_Update(md5_ctx *c, const void *data, uint32_t len) {
    const uint8_t *p = data; c->len += len;
    while (len--) { c->buf[c->n++] = *p++; if (c->n == 64) { md5_block(c, c->buf); c->n = 0; } }
    return 1;
}
int iosrt_CC_MD5_Final(uint8_t *out, md5_ctx *c) {
    uint64_t bits = c->len * 8; uint8_t one = 0x80, zero = 0;
    iosrt_CC_MD5_Update(c, &one, 1); while (c->n != 56) iosrt_CC_MD5_Update(c, &zero, 1);
    uint8_t lb[8]; for (int i = 0; i < 8; i++) lb[i] = (uint8_t)(bits >> (8 * i));
    iosrt_CC_MD5_Update(c, lb, 8);
    uint32_t v[4] = { c->a, c->b, c->c, c->d };
    for (int i = 0; i < 16; i++) out[i] = (uint8_t)(v[i / 4] >> (8 * (i % 4)));
    return 1;
}
uint8_t *iosrt_CC_MD5(const void *data, uint32_t len, uint8_t *out) { md5_ctx c; iosrt_CC_MD5_Init(&c); iosrt_CC_MD5_Update(&c, data, len); iosrt_CC_MD5_Final(out, &c); return out; }
/* SHA-1 (FIPS 180-4) */
typedef struct { uint32_t h[5]; uint64_t len; uint8_t buf[64]; uint32_t n; } sha1_ctx;
static void sha1_block(sha1_ctx *c, const uint8_t *p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 80; i++) w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & cc) | (~b & d); k = 0x5a827999; } else if (i < 40) { f = b ^ cc ^ d; k = 0x6ed9eba1; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8f1bbcdc; } else { f = b ^ cc ^ d; k = 0xca62c1d6; }
        uint32_t t = ROL(a, 5) + f + e + k + w[i]; e = d; d = cc; cc = ROL(b, 30); b = a; a = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}
int iosrt_CC_SHA1_Init(sha1_ctx *c) { memset(c, 0, sizeof *c); c->h[0] = 0x67452301; c->h[1] = 0xefcdab89; c->h[2] = 0x98badcfe; c->h[3] = 0x10325476; c->h[4] = 0xc3d2e1f0; return 1; }
int iosrt_CC_SHA1_Update(sha1_ctx *c, const void *data, uint32_t len) {
    const uint8_t *p = data; c->len += len;
    while (len--) { c->buf[c->n++] = *p++; if (c->n == 64) { sha1_block(c, c->buf); c->n = 0; } }
    return 1;
}
int iosrt_CC_SHA1_Final(uint8_t *out, sha1_ctx *c) {
    uint64_t bits = c->len * 8; uint8_t one = 0x80, zero = 0;
    iosrt_CC_SHA1_Update(c, &one, 1); while (c->n != 56) iosrt_CC_SHA1_Update(c, &zero, 1);
    uint8_t lb[8]; for (int i = 0; i < 8; i++) lb[i] = (uint8_t)(bits >> (56 - 8 * i));
    iosrt_CC_SHA1_Update(c, lb, 8);
    for (int i = 0; i < 20; i++) out[i] = (uint8_t)(c->h[i / 4] >> (24 - 8 * (i % 4)));
    return 1;
}
uint8_t *iosrt_CC_SHA1(const void *data, uint32_t len, uint8_t *out) { sha1_ctx c; iosrt_CC_SHA1_Init(&c); iosrt_CC_SHA1_Update(&c, data, len); iosrt_CC_SHA1_Final(out, &c); return out; }
/* SHA-256 */
typedef struct { uint32_t h[8]; uint64_t len; uint8_t buf[64]; uint32_t n; } sha256_ctx;
static const uint32_t K256[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };
#define ROR(x, c) (((x) >> (c)) | ((x) << (32 - (c))))
static void sha256_block(sha256_ctx *c, const uint8_t *p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
    for (int i = 16; i < 64; i++) { uint32_t s0 = ROR(w[i - 15], 7) ^ ROR(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = ROR(w[i - 2], 17) ^ ROR(w[i - 2], 19) ^ (w[i - 2] >> 10); w[i] = w[i - 16] + s0 + w[i - 7] + s1; }
    uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4], f = c->h[5], g = c->h[6], h = c->h[7];
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25), ch = (e & f) ^ (~e & g), t1 = h + S1 + ch + K256[i] + w[i];
        uint32_t S0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22), mj = (a & b) ^ (a & cc) ^ (b & cc), t2 = S0 + mj;
        h = g; g = f; f = e; e = d + t1; d = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e; c->h[5] += f; c->h[6] += g; c->h[7] += h;
}
int iosrt_CC_SHA256_Init(sha256_ctx *c) {
    static const uint32_t iv[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    memset(c, 0, sizeof *c); memcpy(c->h, iv, sizeof iv); return 1;
}
int iosrt_CC_SHA256_Update(sha256_ctx *c, const void *data, uint32_t len) {
    const uint8_t *p = data; c->len += len;
    while (len--) { c->buf[c->n++] = *p++; if (c->n == 64) { sha256_block(c, c->buf); c->n = 0; } }
    return 1;
}
int iosrt_CC_SHA256_Final(uint8_t *out, sha256_ctx *c) {
    uint64_t bits = c->len * 8; uint8_t one = 0x80, zero = 0;
    iosrt_CC_SHA256_Update(c, &one, 1); while (c->n != 56) iosrt_CC_SHA256_Update(c, &zero, 1);
    uint8_t lb[8]; for (int i = 0; i < 8; i++) lb[i] = (uint8_t)(bits >> (56 - 8 * i));
    iosrt_CC_SHA256_Update(c, lb, 8);
    for (int i = 0; i < 32; i++) out[i] = (uint8_t)(c->h[i / 4] >> (24 - 8 * (i % 4)));
    return 1;
}
uint8_t *iosrt_CC_SHA256(const void *data, uint32_t len, uint8_t *out) { sha256_ctx c; iosrt_CC_SHA256_Init(&c); iosrt_CC_SHA256_Update(&c, data, len); iosrt_CC_SHA256_Final(out, &c); return out; }
/* HMAC with MD5/SHA1/SHA256 (kCCHmacAlgSHA1=0, MD5=1, SHA256=2) */
void iosrt_CCHmac(uint32_t alg, const void *key, size_t klen, const void *data, size_t dlen, uint8_t *out) {
    uint8_t k[64] = { 0 }, ipad[64], opad[64], inner[32];
    size_t hl = alg == 1 ? 16 : alg == 2 ? 32 : 20;
    if (klen > 64) { if (alg == 1) iosrt_CC_MD5(key, (uint32_t)klen, k); else if (alg == 2) iosrt_CC_SHA256(key, (uint32_t)klen, k); else iosrt_CC_SHA1(key, (uint32_t)klen, k); }
    else memcpy(k, key, klen);
    for (int i = 0; i < 64; i++) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    if (alg == 1) { md5_ctx c; iosrt_CC_MD5_Init(&c); iosrt_CC_MD5_Update(&c, ipad, 64); iosrt_CC_MD5_Update(&c, data, (uint32_t)dlen); iosrt_CC_MD5_Final(inner, &c);
                    iosrt_CC_MD5_Init(&c); iosrt_CC_MD5_Update(&c, opad, 64); iosrt_CC_MD5_Update(&c, inner, (uint32_t)hl); iosrt_CC_MD5_Final(out, &c); }
    else if (alg == 2) { sha256_ctx c; iosrt_CC_SHA256_Init(&c); iosrt_CC_SHA256_Update(&c, ipad, 64); iosrt_CC_SHA256_Update(&c, data, (uint32_t)dlen); iosrt_CC_SHA256_Final(inner, &c);
                         iosrt_CC_SHA256_Init(&c); iosrt_CC_SHA256_Update(&c, opad, 64); iosrt_CC_SHA256_Update(&c, inner, (uint32_t)hl); iosrt_CC_SHA256_Final(out, &c); }
    else { sha1_ctx c; iosrt_CC_SHA1_Init(&c); iosrt_CC_SHA1_Update(&c, ipad, 64); iosrt_CC_SHA1_Update(&c, data, (uint32_t)dlen); iosrt_CC_SHA1_Final(inner, &c);
           iosrt_CC_SHA1_Init(&c); iosrt_CC_SHA1_Update(&c, opad, 64); iosrt_CC_SHA1_Update(&c, inner, (uint32_t)hl); iosrt_CC_SHA1_Final(out, &c); }
}
int32_t iosrt_CCCrypt(uint32_t op, uint32_t alg, uint32_t opt, const void *key, size_t klen, const void *iv, const void *in, size_t inlen,
                      void *out, size_t outcap, size_t *moved) {
    (void)op; (void)alg; (void)opt; (void)key; (void)klen; (void)iv; (void)in; (void)inlen; (void)out; (void)outcap;
    if (moved) *moved = 0;
    return -4305;    /* kCCUnimplemented */
}
void *iosrt_reallocf(void *p, size_t n) { void *r = realloc(p, n); if (!r) free(p); return r; }

/* ------------------------------------------- vm_allocate / vm_remap mirroring */
/* Darwin VM calls are used for "magic ring buffers" (e.g. TPCircularBuffer): allocate
 * 2N bytes, free the upper half, then remap the lower half there. Emulated with a
 * memfd per allocation and MAP_SHARED mappings, so remaps alias the same pages. */
#include <sys/mman.h>
#include <sys/syscall.h>
struct vm_region { uintptr_t addr; size_t size; int fd; };
static struct vm_region vm_regions[256]; static int n_vm_regions;
static pthread_mutex_t vm_lock = PTHREAD_MUTEX_INITIALIZER;
int iosrt_vm_allocate(uint32_t task, uintptr_t *addr, size_t size, int flags) {
    (void)task;
    int fd = (int)syscall(SYS_memfd_create, "iosrt-vm", 0);
    if (fd < 0 || ftruncate(fd, (off_t)size) != 0) { if (fd >= 0) close(fd); return 3; /* KERN_NO_SPACE */ }
    void *want = (flags & 1) ? NULL : (void *)*addr;      /* VM_FLAGS_ANYWHERE = 1 */
    void *p = mmap(want, size, PROT_READ | PROT_WRITE, MAP_SHARED | (want ? MAP_FIXED : 0), fd, 0);
    if (p == MAP_FAILED) { close(fd); return 3; }
    pthread_mutex_lock(&vm_lock);
    if (n_vm_regions < 256) vm_regions[n_vm_regions++] = (struct vm_region){ (uintptr_t)p, size, fd };
    pthread_mutex_unlock(&vm_lock);
    *addr = (uintptr_t)p;
    return KERN_SUCCESS;
}
int iosrt_vm_deallocate(uint32_t task, uintptr_t addr, size_t size) {
    (void)task;
    if (size) munmap((void *)addr, size);
    pthread_mutex_lock(&vm_lock);
    for (int i = 0; i < n_vm_regions; i++)
        if (vm_regions[i].addr == addr && vm_regions[i].size <= size) { close(vm_regions[i].fd); vm_regions[i] = vm_regions[--n_vm_regions]; break; }
    pthread_mutex_unlock(&vm_lock);
    return KERN_SUCCESS;
}
int iosrt_vm_remap(uint32_t target, uintptr_t *taddr, size_t size, uintptr_t mask, int flags, uint32_t src_task,
                   uintptr_t saddr, long copy, int *cur, int *max, unsigned inherit) {
    (void)target; (void)mask; (void)src_task; (void)copy; (void)inherit;
    int fd = -1; off_t off = 0;
    pthread_mutex_lock(&vm_lock);
    for (int i = 0; i < n_vm_regions; i++)
        if (saddr >= vm_regions[i].addr && saddr + size <= vm_regions[i].addr + vm_regions[i].size) { fd = vm_regions[i].fd; off = (off_t)(saddr - vm_regions[i].addr); break; }
    pthread_mutex_unlock(&vm_lock);
    if (fd < 0) return KERN_FAILURE;
    void *want = (flags & 1) ? NULL : (void *)*taddr;
    void *p = mmap(want, size, PROT_READ | PROT_WRITE, MAP_SHARED | (want ? MAP_FIXED : 0), fd, off);
    if (p == MAP_FAILED) return KERN_FAILURE;
    *taddr = (uintptr_t)p;
    if (cur) *cur = 3; if (max) *max = 3;
    return KERN_SUCCESS;
}
/* CFBundle keys */
id iosrt_kCFBundleExecutableKey, iosrt_kCFBundleIdentifierKey, iosrt_kCFBundleVersionKey, iosrt_kCFBundleNameKey,
   iosrt_kCFBundleInfoDictionaryVersionKey, iosrt_kCFBundleDevelopmentRegionKey, iosrt_kCFBundleLocalizationsKey;
void rt_stubs_init(void) {
    iosrt_kCFBundleExecutableKey = rt_str_new("CFBundleExecutable");
    iosrt_kCFBundleIdentifierKey = rt_str_new("CFBundleIdentifier");
    iosrt_kCFBundleVersionKey = rt_str_new("CFBundleVersion");
    iosrt_kCFBundleNameKey = rt_str_new("CFBundleName");
    iosrt_kCFBundleInfoDictionaryVersionKey = rt_str_new("CFBundleInfoDictionaryVersion");
    iosrt_kCFBundleDevelopmentRegionKey = rt_str_new("CFBundleDevelopmentRegion");
    iosrt_kCFBundleLocalizationsKey = rt_str_new("CFBundleLocalizations");
}

/* guests (crash reporters) installing signal handlers must not replace the runtime's */
int iosrt_sigaction(int sig, const void *act, void *old) { (void)sig; (void)act; if (old) memset(old, 0, 16); return 0; }
int iosrt_sigaltstack(const void *ss, void *old) { (void)ss; if (old) memset(old, 0, 24); return 0; }
int iosrt_sigprocmask(int how, const void *set, void *old) { (void)how; (void)set; if (old) memset(old, 0, 4); return 0; }
int iosrt_pthread_sigmask(int how, const void *set, void *old) { return iosrt_sigprocmask(how, set, old); }
/* NSSetUncaughtExceptionHandler: remembered and called before the guest is terminated */
void *iosrt_uncaught_handler;
void iosrt_NSSetUncaughtExceptionHandler(void *h) { iosrt_uncaught_handler = h; }
void *iosrt_NSGetUncaughtExceptionHandler(void) { return iosrt_uncaught_handler; }
