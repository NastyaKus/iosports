/*
 * Accelerate: vDSP FFTs, convolution, matrix and vector routines, windows,
 * and vecLib's vv* / cblas_sgemv, written from Apple's documented semantics
 * (argument order, strides, packing and scaling conventions).
 *
 * FFT conventions (vDSP): forward = +1 uses e^(-2*pi*i/N); complex FFTs are
 * unscaled both ways; the real FFT works on N/2 packed complex points
 * (even samples in realp, odd in imagp, see vDSP_ctoz), its forward result is
 * 2x the DFT with the Nyquist term stored in imagp[0], and its inverse is the
 * unnormalised inverse DFT (a forward + inverse round trip scales by 2N).
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct { float *realp, *imagp; } SplitF;
typedef struct { double *realp, *imagp; } SplitD;
typedef long vstride;
typedef unsigned long vlen;

/* --------------------------------------------------------------- FFT core */
struct fft_setup { unsigned log2max; double *cs, *sn; };   /* twiddles for the largest size */
static struct fft_setup *setup_new(unsigned long log2n) {
    if (log2n > 24) return NULL;
    struct fft_setup *s = rt_xcalloc(1, sizeof *s);
    size_t n = (size_t)1 << log2n;
    s->log2max = (unsigned)log2n;
    s->cs = rt_xmalloc((n / 2 + 1) * sizeof(double)); s->sn = rt_xmalloc((n / 2 + 1) * sizeof(double));
    for (size_t k = 0; k <= n / 2; k++) { s->cs[k] = cos(2 * M_PI * (double)k / (double)n); s->sn[k] = sin(2 * M_PI * (double)k / (double)n); }
    return s;
}
void *iosrt_vDSP_create_fftsetup(unsigned long log2n, int radix) { (void)radix; return setup_new(log2n); }
void *iosrt_vDSP_create_fftsetupD(unsigned long log2n, int radix) { (void)radix; return setup_new(log2n); }
void iosrt_vDSP_destroy_fftsetup(void *p) { struct fft_setup *s = p; if (s) { free(s->cs); free(s->sn); free(s); } }
void iosrt_vDSP_destroy_fftsetupD(void *p) { iosrt_vDSP_destroy_fftsetup(p); }

/* in-place radix-2 FFT on contiguous double arrays; dir +1 forward (e^-i), -1 inverse */
static void fft_d(const struct fft_setup *s, double *re, double *im, unsigned log2n, int dir) {
    size_t n = (size_t)1 << log2n;
    if (n < 2) return;
    unsigned shift = s->log2max - log2n;   /* table step for smaller sizes */
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        size_t half = len >> 1, step = (n / len) << shift;
        for (size_t i = 0; i < n; i += len)
            for (size_t k = 0; k < half; k++) {
                double wr = s->cs[k * step], wi = dir > 0 ? -s->sn[k * step] : s->sn[k * step];
                size_t a = i + k, b = a + half;
                double tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
                re[b] = re[a] - tr; im[b] = im[a] - ti;
                re[a] += tr; im[a] += ti;
            }
    }
}
static __thread double *scratch; static __thread size_t scratch_n;
static double *scr(size_t n) { if (n > scratch_n) { free(scratch); scratch = rt_xmalloc(n * sizeof(double)); scratch_n = n; } return scratch; }
/* complex FFT with strided split-complex input/output (float or double) */
#define CPLX_FFT(NAME, SPLIT, T) \
static void NAME(void *setup, const SPLIT *a, vstride ia, const SPLIT *c, vstride ic, unsigned long log2n, int dir) { \
    struct fft_setup *s = setup; if (!s || log2n > s->log2max) return; \
    size_t n = (size_t)1 << log2n; double *re = scr(2 * n), *im = re + n; \
    for (size_t k = 0; k < n; k++) { re[k] = a->realp[k * ia]; im[k] = a->imagp[k * ia]; } \
    fft_d(s, re, im, (unsigned)log2n, dir); \
    for (size_t k = 0; k < n; k++) { c->realp[k * ic] = (T)re[k]; c->imagp[k * ic] = (T)im[k]; } \
}
CPLX_FFT(cfft_f, SplitF, float)
CPLX_FFT(cfft_d, SplitD, double)
void iosrt_vDSP_fft_zip(void *s, SplitF *c, vstride ic, unsigned long l, int d) { cfft_f(s, c, ic, c, ic, l, d); }
void iosrt_vDSP_fft_zipt(void *s, SplitF *c, vstride ic, SplitF *t, unsigned long l, int d) { (void)t; cfft_f(s, c, ic, c, ic, l, d); }
void iosrt_vDSP_fft_ziptD(void *s, SplitD *c, vstride ic, SplitD *t, unsigned long l, int d) { (void)t; cfft_d(s, c, ic, c, ic, l, d); }
void iosrt_vDSP_fft_zop(void *s, SplitF *a, vstride ia, SplitF *c, vstride ic, unsigned long l, int d) { cfft_f(s, a, ia, c, ic, l, d); }
void iosrt_vDSP_fft_zopt(void *s, SplitF *a, vstride ia, SplitF *c, vstride ic, SplitF *t, unsigned long l, int d) { (void)t; cfft_f(s, a, ia, c, ic, l, d); }
void iosrt_vDSP_fft_zoptD(void *s, SplitD *a, vstride ia, SplitD *c, vstride ic, SplitD *t, unsigned long l, int d) { (void)t; cfft_d(s, a, ia, c, ic, l, d); }

/* real FFT of N = 2^log2n samples packed as N/2 complex points z = even + i*odd:
 * forward  2X[k] = e - i*t,  e = Z[k] + conj(Z[h-k]),  t = (Z[k] - conj(Z[h-k])) W^k
 * inverse  Z[k]  = e + i*t,  e = S[k] + conj(S[h-k]),  t = (S[k] - conj(S[h-k])) W^-k,
 * then an unscaled inverse FFT of the N/2 points (W = e^(-2 pi i / N)) */
#define REAL_FFT(NAME, SPLIT, T) \
static void NAME(void *setup, const SPLIT *a, vstride ia, const SPLIT *c, vstride ic, unsigned long log2n, int dir) { \
    struct fft_setup *s = setup; if (!s || log2n < 1 || log2n > s->log2max) return; \
    size_t n = (size_t)1 << log2n, h = n / 2; double *re = scr(4 * h), *im = re + h, *ore = im + h, *oim = ore + h; \
    for (size_t k = 0; k < h; k++) { re[k] = a->realp[k * ia]; im[k] = a->imagp[k * ia]; } \
    unsigned shift = s->log2max - (unsigned)log2n; \
    if (dir > 0) fft_d(s, re, im, (unsigned)log2n - 1, 1); \
    for (size_t k = 1; k < h; k++) { \
        size_t j = h - k; \
        double er = re[k] + re[j], ei = im[k] - im[j], dr = re[k] - re[j], di = im[k] + im[j]; \
        double wr = s->cs[k << shift], wi = dir > 0 ? -s->sn[k << shift] : s->sn[k << shift]; \
        double tr = dr * wr - di * wi, ti = dr * wi + di * wr; \
        if (dir > 0) { ore[k] = er + ti; oim[k] = ei - tr; } \
        else { ore[k] = er - ti; oim[k] = ei + tr; } \
    } \
    if (dir > 0) { ore[0] = 2 * (re[0] + im[0]); oim[0] = 2 * (re[0] - im[0]); } \
    else { ore[0] = re[0] + im[0]; oim[0] = re[0] - im[0]; fft_d(s, ore, oim, (unsigned)log2n - 1, -1); } \
    for (size_t k = 0; k < h; k++) { c->realp[k * ic] = (T)ore[k]; c->imagp[k * ic] = (T)oim[k]; } \
}
REAL_FFT(rfft_f, SplitF, float)
REAL_FFT(rfft_d, SplitD, double)
void iosrt_vDSP_fft_zrip(void *s, SplitF *c, vstride ic, unsigned long l, int d) { rfft_f(s, c, ic, c, ic, l, d); }
void iosrt_vDSP_fft_zript(void *s, SplitF *c, vstride ic, SplitF *t, unsigned long l, int d) { (void)t; rfft_f(s, c, ic, c, ic, l, d); }
void iosrt_vDSP_fft_zripD(void *s, SplitD *c, vstride ic, unsigned long l, int d) { rfft_d(s, c, ic, c, ic, l, d); }
void iosrt_vDSP_fft_zrop(void *s, SplitF *a, vstride ia, SplitF *c, vstride ic, unsigned long l, int d) { rfft_f(s, a, ia, c, ic, l, d); }
void iosrt_vDSP_fft_zropt(void *s, SplitF *a, vstride ia, SplitF *c, vstride ic, SplitF *t, unsigned long l, int d) { (void)t; rfft_f(s, a, ia, c, ic, l, d); }

/* interleaved <-> split complex; ic is a stride in real elements (2 = contiguous) */
void iosrt_vDSP_ctoz(const float *c, vstride ic, SplitF *z, vstride iz, vlen n) { for (vlen k = 0; k < n; k++) { z->realp[k * iz] = c[k * ic]; z->imagp[k * iz] = c[k * ic + 1]; } }
void iosrt_vDSP_ctozD(const double *c, vstride ic, SplitD *z, vstride iz, vlen n) { for (vlen k = 0; k < n; k++) { z->realp[k * iz] = c[k * ic]; z->imagp[k * iz] = c[k * ic + 1]; } }
void iosrt_vDSP_ztoc(const SplitF *z, vstride iz, float *c, vstride ic, vlen n) { for (vlen k = 0; k < n; k++) { c[k * ic] = z->realp[k * iz]; c[k * ic + 1] = z->imagp[k * iz]; } }
void iosrt_vDSP_ztocD(const SplitD *z, vstride iz, double *c, vstride ic, vlen n) { for (vlen k = 0; k < n; k++) { c[k * ic] = z->realp[k * iz]; c[k * ic + 1] = z->imagp[k * iz]; } }

/* ------------------------------------------------------- vector routines */
#define A_(i) a[(i) * ia]
#define B_(i) b[(i) * ib]
#define C_(i) c[(i) * ic]
#define D_(i) d[(i) * id]
void iosrt_vDSP_conv(const float *a, vstride ia, const float *f, vstride iff, float *c, vstride ic, vlen n, vlen p) {
    for (vlen k = 0; k < n; k++) { double s = 0; for (vlen q = 0; q < p; q++) s += (double)A_(k + q) * f[(long)q * iff]; C_(k) = (float)s; }
}
void iosrt_vDSP_dotpr(const float *a, vstride ia, const float *b, vstride ib, float *c, vlen n) { double s = 0; for (vlen k = 0; k < n; k++) s += (double)A_(k) * B_(k); *c = (float)s; }
void iosrt_vDSP_dotprD(const double *a, vstride ia, const double *b, vstride ib, double *c, vlen n) { double s = 0; for (vlen k = 0; k < n; k++) s += A_(k) * B_(k); *c = s; }
void iosrt_vDSP_maxvi(const float *a, vstride ia, float *c, vlen *i, vlen n) {
    float m = -INFINITY; vlen at = 0; for (vlen k = 0; k < n; k++) if (A_(k) > m) { m = A_(k); at = k * (vlen)ia; } *c = m; *i = at;
}
void iosrt_vDSP_minvi(const float *a, vstride ia, float *c, vlen *i, vlen n) {
    float m = INFINITY; vlen at = 0; for (vlen k = 0; k < n; k++) if (A_(k) < m) { m = A_(k); at = k * (vlen)ia; } *c = m; *i = at;
}
void iosrt_vDSP_meanv(const float *a, vstride ia, float *c, vlen n) { double s = 0; for (vlen k = 0; k < n; k++) s += A_(k); *c = n ? (float)(s / (double)n) : 0; }
void iosrt_vDSP_meanvD(const double *a, vstride ia, double *c, vlen n) { double s = 0; for (vlen k = 0; k < n; k++) s += A_(k); *c = n ? s / (double)n : 0; }
void iosrt_vDSP_sve(const float *a, vstride ia, float *c, vlen n) { double s = 0; for (vlen k = 0; k < n; k++) s += A_(k); *c = (float)s; }
void iosrt_vDSP_sveD(const double *a, vstride ia, double *c, vlen n) { double s = 0; for (vlen k = 0; k < n; k++) s += A_(k); *c = s; }
void iosrt_vDSP_svesq(const float *a, vstride ia, float *c, vlen n) { double s = 0; for (vlen k = 0; k < n; k++) s += (double)A_(k) * A_(k); *c = (float)s; }
void iosrt_vDSP_normalize(const float *a, vstride ia, float *c, vstride ic, float *mean, float *sd, vlen n) {
    double s = 0, q = 0; for (vlen k = 0; k < n; k++) { s += A_(k); q += (double)A_(k) * A_(k); }
    double m = n ? s / (double)n : 0, v = n ? q / (double)n - m * m : 0, dev = v > 0 ? sqrt(v) : 0;
    if (mean) *mean = (float)m;
    if (sd) *sd = (float)dev;
    if (c) for (vlen k = 0; k < n; k++) C_(k) = dev > 0 ? (float)((A_(k) - m) / dev) : 0;
}
void iosrt_vDSP_nzcros(const float *a, vstride ia, vlen maxc, vlen *last, vlen *count, vlen n) {
    vlen cnt = 0, at = 0;
    for (vlen k = 1; k < n && cnt < maxc; k++) if (signbit(A_(k)) != signbit(A_(k - 1))) { cnt++; at = k * (vlen)ia; }
    *last = at; *count = cnt;
}
void iosrt_vDSP_polar(const float *a, vstride ia, float *c, vstride ic, vlen n) {
    for (vlen k = 0; k < n; k++) { float x = a[k * ia], y = a[k * ia + 1]; c[k * ic] = sqrtf(x * x + y * y); c[k * ic + 1] = atan2f(y, x); }
}
void iosrt_vDSP_vabs(const float *a, vstride ia, float *c, vstride ic, vlen n) { for (vlen k = 0; k < n; k++) C_(k) = fabsf(A_(k)); }
void iosrt_vDSP_vasm(const float *a, vstride ia, const float *b, vstride ib, const float *s, float *c, vstride ic, vlen n) { for (vlen k = 0; k < n; k++) C_(k) = (A_(k) + B_(k)) * *s; }
void iosrt_vDSP_vdist(const float *a, vstride ia, const float *b, vstride ib, float *c, vstride ic, vlen n) { for (vlen k = 0; k < n; k++) C_(k) = sqrtf(A_(k) * A_(k) + B_(k) * B_(k)); }
/* vDSP_vdiv(B, IB, A, IA, C, IC, N): C = A / B */
void iosrt_vDSP_vdiv(const float *b, vstride ib, const float *a, vstride ia, float *c, vstride ic, vlen n) { for (vlen k = 0; k < n; k++) C_(k) = A_(k) / B_(k); }
void iosrt_vDSP_vdivD(const double *b, vstride ib, const double *a, vstride ia, double *c, vstride ic, vlen n) { for (vlen k = 0; k < n; k++) C_(k) = A_(k) / B_(k); }
void iosrt_vDSP_svdiv(const float *s, const float *b, vstride ib, float *c, vstride ic, vlen n) { for (vlen k = 0; k < n; k++) C_(k) = *s / B_(k); }
void iosrt_vDSP_vsdiv(const float *a, vstride ia, const float *s, float *c, vstride ic, vlen n) { for (vlen k = 0; k < n; k++) C_(k) = A_(k) / *s; }
void iosrt_vDSP_vsdivD(const double *a, vstride ia, const double *s, double *c, vstride ic, vlen n) { for (vlen k = 0; k < n; k++) C_(k) = A_(k) / *s; }
void iosrt_vDSP_vfix32(const float *a, vstride ia, int32_t *c, vstride ic, vlen n) { for (vlen k = 0; k < n; k++) C_(k) = (int32_t)A_(k); }
void iosrt_vDSP_vfixr16(const float *a, vstride ia, int16_t *c, vstride ic, vlen n) {
    for (vlen k = 0; k < n; k++) { float v = rintf(A_(k)); C_(k) = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
}
void iosrt_vDSP_vfrac(const float *a, vstride ia, float *c, vstride ic, vlen n) { for (vlen k = 0; k < n; k++) C_(k) = A_(k) - truncf(A_(k)); }
void iosrt_vDSP_vgen(const float *a, const float *b, float *c, vstride ic, vlen n) {
    for (vlen k = 0; k < n; k++) C_(k) = n > 1 ? *a + (*b - *a) * (float)k / (float)(n - 1) : *a;
}
/* piecewise linear: B holds break indices, A the values at them */
void iosrt_vDSP_vgenp(const float *a, vstride ia, const float *b, vstride ib, float *c, vstride ic, vlen n, vlen m) {
    for (vlen k = 0; k < n; k++) {
        float x = (float)k;
        if (!m) { C_(k) = 0; continue; }
        if (x <= B_(0)) { C_(k) = A_(0); continue; }
        if (x >= B_(m - 1)) { C_(k) = A_(m - 1); continue; }
        vlen j = 0; while (j + 1 < m && B_(j + 1) <= x) j++;
        float t = (x - B_(j)) / (B_(j + 1) - B_(j));
        C_(k) = A_(j) + t * (A_(j + 1) - A_(j));
    }
}
void iosrt_vDSP_vma(const float *a, vstride ia, const float *b, vstride ib, const float *c, vstride ic, float *d, vstride id, vlen n) { for (vlen k = 0; k < n; k++) D_(k) = A_(k) * B_(k) + C_(k); }
void iosrt_vDSP_vsmsa(const float *a, vstride ia, const float *b, const float *c, float *d, vstride id, vlen n) { for (vlen k = 0; k < n; k++) D_(k) = A_(k) * *b + *c; }
void iosrt_vDSP_vmaxmg(const float *a, vstride ia, const float *b, vstride ib, float *c, vstride ic, vlen n) { for (vlen k = 0; k < n; k++) C_(k) = fmaxf(fabsf(A_(k)), fabsf(B_(k))); }
void iosrt_vDSP_vmin(const float *a, vstride ia, const float *b, vstride ib, float *c, vstride ic, vlen n) { for (vlen k = 0; k < n; k++) C_(k) = fminf(A_(k), B_(k)); }
void iosrt_vDSP_vneg(const float *a, vstride ia, float *c, vstride ic, vlen n) { for (vlen k = 0; k < n; k++) C_(k) = -A_(k); }
void iosrt_vDSP_vnegD(const double *a, vstride ia, double *c, vstride ic, vlen n) { for (vlen k = 0; k < n; k++) C_(k) = -A_(k); }
void iosrt_vDSP_vpythg(const float *a, vstride ia, const float *b, vstride ib, const float *c, vstride ic, const float *d, vstride id, float *e, vstride ie, vlen n) {
    for (vlen k = 0; k < n; k++) { float x = A_(k) - C_(k), y = B_(k) - D_(k); e[k * ie] = sqrtf(x * x + y * y); }
}
void iosrt_vDSP_vsq(const float *a, vstride ia, float *c, vstride ic, vlen n) { for (vlen k = 0; k < n; k++) C_(k) = A_(k) * A_(k); }
void iosrt_vDSP_vsqD(const double *a, vstride ia, double *c, vstride ic, vlen n) { for (vlen k = 0; k < n; k++) C_(k) = A_(k) * A_(k); }
/* vDSP_vsub(B, IB, A, IA, C, IC, N): C = A - B */
void iosrt_vDSP_vsub(const float *b, vstride ib, const float *a, vstride ia, float *c, vstride ic, vlen n) { for (vlen k = 0; k < n; k++) C_(k) = A_(k) - B_(k); }
void iosrt_vDSP_vsubD(const double *b, vstride ib, const double *a, vstride ia, double *c, vstride ic, vlen n) { for (vlen k = 0; k < n; k++) C_(k) = A_(k) - B_(k); }
void iosrt_vDSP_vswsum(const float *a, vstride ia, float *c, vstride ic, vlen n, vlen p) {
    for (vlen k = 0; k < n; k++) { double s = 0; for (vlen q = 0; q < p; q++) s += A_(k + q); C_(k) = (float)s; }
}
/* table lookup with linear interpolation: index = A*S1 + S2 into C[0..M-1] */
void iosrt_vDSP_vtabi(const float *a, vstride ia, const float *s1, const float *s2, const float *tab, vlen m, float *d, vstride id, vlen n) {
    for (vlen k = 0; k < n; k++) {
        float x = A_(k) * *s1 + *s2;
        if (!m) { D_(k) = 0; continue; }
        if (x <= 0) { D_(k) = tab[0]; continue; }
        if (x >= (float)(m - 1)) { D_(k) = tab[m - 1]; continue; }
        vlen i = (vlen)x; float t = x - (float)i;
        D_(k) = tab[i] + t * (tab[i + 1] - tab[i]);
    }
}
void iosrt_vDSP_vthr(const float *a, vstride ia, const float *b, float *c, vstride ic, vlen n) { for (vlen k = 0; k < n; k++) C_(k) = A_(k) >= *b ? A_(k) : *b; }
void iosrt_vDSP_vthres(const float *a, vstride ia, const float *b, float *c, vstride ic, vlen n) { for (vlen k = 0; k < n; k++) C_(k) = A_(k) >= *b ? A_(k) : 0; }
void iosrt_vDSP_vtrapzD(const double *a, vstride ia, const double *step, double *c, vstride ic, vlen n) {
    double s = 0; for (vlen k = 0; k < n; k++) { if (k) s += *step * (A_(k - 1) + A_(k)) / 2; C_(k) = s; }
}
static int cmp_up(const void *x, const void *y) { double a = *(const double *)x, b = *(const double *)y; return a < b ? -1 : a > b; }
static int cmp_down(const void *x, const void *y) { return cmp_up(y, x); }
void iosrt_vDSP_vsortD(double *c, vlen n, int order) { qsort(c, n, sizeof *c, order < 0 ? cmp_down : cmp_up); }
static const float *g_sort_vals; static int g_sort_order;
static int cmp_idx(const void *x, const void *y) {
    float a = g_sort_vals[*(const vlen *)x], b = g_sort_vals[*(const vlen *)y];
    int r = a < b ? -1 : a > b; return g_sort_order < 0 ? -r : r;
}
static pthread_mutex_t sort_lock = PTHREAD_MUTEX_INITIALIZER;
void iosrt_vDSP_vsorti(const float *c, vlen *idx, vlen *list, vlen n, int order) {
    (void)list;
    pthread_mutex_lock(&sort_lock);
    g_sort_vals = c; g_sort_order = order;
    qsort(idx, n, sizeof *idx, cmp_idx);
    pthread_mutex_unlock(&sort_lock);
}
/* windows: flag 1 = half window, 2 = normalised Hann */
void iosrt_vDSP_hann_window(float *c, vlen n, int flag) {
    vlen m = flag & 1 ? (n + 1) / 2 : n; double k0 = flag & 2 ? 0.8165 : 0.5;
    for (vlen k = 0; k < m; k++) c[k] = (float)(k0 * (1 - cos(2 * M_PI * (double)k / (double)n)));
}
void iosrt_vDSP_hamm_window(float *c, vlen n, int flag) {
    vlen m = flag & 1 ? (n + 1) / 2 : n;
    for (vlen k = 0; k < m; k++) c[k] = (float)(0.54 - 0.46 * cos(2 * M_PI * (double)k / (double)n));
}

/* --------------------------------------------------------------- matrices */
/* vDSP_mmov(A, C, M (columns), N (rows), TA, TC): copy an M x N submatrix */
void iosrt_vDSP_mmov(const float *a, float *c, vlen m, vlen n, vlen ta, vlen tc) { for (vlen r = 0; r < n; r++) memmove(c + r * tc, a + r * ta, m * sizeof *a); }
void iosrt_vDSP_mmovD(const double *a, double *c, vlen m, vlen n, vlen ta, vlen tc) { for (vlen r = 0; r < n; r++) memmove(c + r * tc, a + r * ta, m * sizeof *a); }
/* C (M x N) = A (M x P) * B (P x N), row-major with element strides */
void iosrt_vDSP_mmul(const float *a, vstride ia, const float *b, vstride ib, float *c, vstride ic, vlen m, vlen n, vlen p) {
    for (vlen i = 0; i < m; i++) for (vlen j = 0; j < n; j++) { double s = 0; for (vlen k = 0; k < p; k++) s += (double)A_(i * p + k) * B_(k * n + j); C_(i * n + j) = (float)s; }
}
void iosrt_vDSP_mmulD(const double *a, vstride ia, const double *b, vstride ib, double *c, vstride ic, vlen m, vlen n, vlen p) {
    for (vlen i = 0; i < m; i++) for (vlen j = 0; j < n; j++) { double s = 0; for (vlen k = 0; k < p; k++) s += A_(i * p + k) * B_(k * n + j); C_(i * n + j) = s; }
}
/* C (M x N) = transpose of A (N x M) */
void iosrt_vDSP_mtrans(const float *a, vstride ia, float *c, vstride ic, vlen m, vlen n) { for (vlen i = 0; i < m; i++) for (vlen j = 0; j < n; j++) C_(i * n + j) = A_(j * m + i); }
void iosrt_vDSP_mtransD(const double *a, vstride ia, double *c, vstride ic, vlen m, vlen n) { for (vlen i = 0; i < m; i++) for (vlen j = 0; j < n; j++) C_(i * n + j) = A_(j * m + i); }

/* ------------------------------------------------------- split complex */
#define AR(i) a->realp[(i) * ia]
#define AI(i) a->imagp[(i) * ia]
#define BR(i) b->realp[(i) * ib]
#define BI(i) b->imagp[(i) * ib]
void iosrt_vDSP_zvabs(const SplitF *a, vstride ia, float *c, vstride ic, vlen n) { for (vlen k = 0; k < n; k++) C_(k) = hypotf(AR(k), AI(k)); }
void iosrt_vDSP_zvadd(const SplitF *a, vstride ia, const SplitF *b, vstride ib, const SplitF *c, vstride ic, vlen n) {
    for (vlen k = 0; k < n; k++) { c->realp[k * ic] = AR(k) + BR(k); c->imagp[k * ic] = AI(k) + BI(k); }
}
/* D = conj(A) * B + C */
void iosrt_vDSP_zvcma(const SplitF *a, vstride ia, const SplitF *b, vstride ib, const SplitF *c, vstride ic, const SplitF *d, vstride id, vlen n) {
    for (vlen k = 0; k < n; k++) {
        float ar = AR(k), ai = -AI(k), br = BR(k), bi = BI(k);
        float r = ar * br - ai * bi + c->realp[k * ic], i = ar * bi + ai * br + c->imagp[k * ic];
        d->realp[k * id] = r; d->imagp[k * id] = i;
    }
}
/* C = A * B, or conj(A) * B when conj == -1 */
void iosrt_vDSP_zvmul(const SplitF *a, vstride ia, const SplitF *b, vstride ib, const SplitF *c, vstride ic, vlen n, int conj) {
    for (vlen k = 0; k < n; k++) {
        float ar = AR(k), ai = conj == -1 ? -AI(k) : AI(k), br = BR(k), bi = BI(k);
        float r = ar * br - ai * bi, i = ar * bi + ai * br;
        c->realp[k * ic] = r; c->imagp[k * ic] = i;
    }
}
/* D = A * b + C, b a complex scalar */
void iosrt_vDSP_zvsma(const SplitF *a, vstride ia, const SplitF *b, const SplitF *c, vstride ic, const SplitF *d, vstride id, vlen n) {
    float br = b->realp[0], bi = b->imagp[0];
    for (vlen k = 0; k < n; k++) {
        float r = AR(k) * br - AI(k) * bi + c->realp[k * ic], i = AR(k) * bi + AI(k) * br + c->imagp[k * ic];
        d->realp[k * id] = r; d->imagp[k * id] = i;
    }
}
/* C = A / B */
void iosrt_vDSP_zvzsml(const SplitF *a, vstride ia, const SplitF *b, vstride ib, const SplitF *c, vstride ic, vlen n) {
    for (vlen k = 0; k < n; k++) {
        float br = BR(k), bi = BI(k), den = br * br + bi * bi;
        float r = den ? (AR(k) * br + AI(k) * bi) / den : 0, i = den ? (AI(k) * br - AR(k) * bi) / den : 0;
        c->realp[k * ic] = r; c->imagp[k * ic] = i;
    }
}

/* -------------------------------------------------- vecLib vv* and BLAS */
void iosrt_vvexpf(float *y, const float *x, const int *n) { for (int k = 0; k < *n; k++) y[k] = expf(x[k]); }
void iosrt_vvfloorf(float *y, const float *x, const int *n) { for (int k = 0; k < *n; k++) y[k] = floorf(x[k]); }
void iosrt_vvfmodf(float *z, const float *y, const float *x, const int *n) { for (int k = 0; k < *n; k++) z[k] = fmodf(y[k], x[k]); }
void iosrt_vvlog2f(float *y, const float *x, const int *n) { for (int k = 0; k < *n; k++) y[k] = log2f(x[k]); }
void iosrt_vvlogf(float *y, const float *x, const int *n) { for (int k = 0; k < *n; k++) y[k] = logf(x[k]); }
void iosrt_vvsqrtf(float *y, const float *x, const int *n) { for (int k = 0; k < *n; k++) y[k] = sqrtf(x[k]); }
void iosrt_vvsincosf(float *s, float *c, const float *x, const int *n) { for (int k = 0; k < *n; k++) { s[k] = sinf(x[k]); c[k] = cosf(x[k]); } }
typedef float v4f __attribute__((vector_size(16)));
v4f iosrt_vfabsf(v4f v) { for (int k = 0; k < 4; k++) v[k] = fabsf(v[k]); return v; }
/* y = alpha * op(A) * x + beta * y; order 101 row-major / 102 column-major, trans 111 no / 112 yes */
void iosrt_cblas_sgemv(int order, int trans, int m, int n, float alpha, const float *a, int lda, const float *x, int incx, float beta, float *y, int incy) {
    int t = (trans != 111) ^ (order == 102);   /* work as row-major: transposed or not */
    int rows = trans == 111 ? m : n, cols = trans == 111 ? n : m;
    for (int i = 0; i < rows; i++) {
        double s = 0;
        for (int j = 0; j < cols; j++) {
            float aij = t ? a[(long)j * lda + i] : a[(long)i * lda + j];
            s += (double)aij * x[(long)j * (incx < 0 ? -incx : incx)];
        }
        long yi = (long)i * (incy < 0 ? -incy : incy);
        y[yi] = (float)(alpha * s + (beta ? beta * y[yi] : 0));
    }
}
