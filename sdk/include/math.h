/* iosrt mini-SDK: libm subset */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define M_PI_4 0.78539816339744830962
#define M_E 2.71828182845904523536
#define M_SQRT2 1.41421356237309504880
#define M_LN2 0.69314718055994530942
#define NAN __builtin_nanf("")
#define INFINITY __builtin_inff()
#define HUGE_VAL __builtin_huge_val()
#define HUGE_VALF __builtin_huge_valf()
#define HUGE_VALL __builtin_huge_vall()
#define FP_NAN 1
#define FP_INFINITE 2
#define FP_ZERO 3
#define FP_NORMAL 4
#define FP_SUBNORMAL 5
#define FP_ILOGB0 (-2147483647 - 1)
#define FP_ILOGBNAN (-2147483647 - 1)
#define MATH_ERRNO 1
#define MATH_ERREXCEPT 2
#define math_errhandling 2
typedef float float_t;
typedef double double_t;
#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x)
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
#define isnormal(x) __builtin_isnormal(x)
#define signbit(x) __builtin_signbit(x)
#define isgreater(x, y) __builtin_isgreater(x, y)
#define isgreaterequal(x, y) __builtin_isgreaterequal(x, y)
#define isless(x, y) __builtin_isless(x, y)
#define islessequal(x, y) __builtin_islessequal(x, y)
#define islessgreater(x, y) __builtin_islessgreater(x, y)
#define isunordered(x, y) __builtin_isunordered(x, y)
#define _M(f) double f(double); float f##f(float); long double f##l(long double);
#define _M2(f) double f(double, double); float f##f(float, float); long double f##l(long double, long double);
_M(acos) _M(asin) _M(atan) _M(cos) _M(sin) _M(tan) _M(acosh) _M(asinh) _M(atanh) _M(cosh) _M(sinh) _M(tanh)
_M(exp) _M(exp2) _M(expm1) _M(log) _M(log10) _M(log2) _M(log1p) _M(logb) _M(sqrt) _M(cbrt) _M(fabs) _M(ceil) _M(floor)
_M(round) _M(trunc) _M(rint) _M(nearbyint) _M(erf) _M(erfc) _M(lgamma) _M(tgamma)
_M2(atan2) _M2(pow) _M2(fmod) _M2(remainder) _M2(hypot) _M2(fdim) _M2(fmax) _M2(fmin) _M2(copysign) _M2(nextafter)
#undef _M
#undef _M2
long lround(double); long lroundf(float); long lroundl(long double);
long long llround(double); long long llroundf(float); long long llroundl(long double);
long lrint(double); long lrintf(float); long lrintl(long double);
long long llrint(double); long long llrintf(float); long long llrintl(long double);
double frexp(double, int *); float frexpf(float, int *); long double frexpl(long double, int *);
double ldexp(double, int); float ldexpf(float, int); long double ldexpl(long double, int);
double modf(double, double *); float modff(float, float *); long double modfl(long double, long double *);
double scalbn(double, int); float scalbnf(float, int); long double scalbnl(long double, int);
double scalbln(double, long); float scalblnf(float, long); long double scalblnl(long double, long);
int ilogb(double); int ilogbf(float); int ilogbl(long double);
double fma(double, double, double); float fmaf(float, float, float); long double fmal(long double, long double, long double);
double remquo(double, double, int *); float remquof(float, float, int *); long double remquol(long double, long double, int *);
double nan(const char *); float nanf(const char *); long double nanl(const char *);
double nexttoward(double, long double); float nexttowardf(float, long double); long double nexttowardl(long double, long double);
#ifdef __cplusplus
}
#endif
