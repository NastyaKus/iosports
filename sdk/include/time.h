#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef long time_t;
typedef unsigned long clock_t;
struct timespec { time_t tv_sec; long tv_nsec; };
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; long tm_gmtoff; char *tm_zone; };
#define CLOCKS_PER_SEC 1000000
#define TIME_UTC 1
typedef enum { _CLOCK_REALTIME = 0, _CLOCK_MONOTONIC = 6 } clockid_t;
#define CLOCK_REALTIME _CLOCK_REALTIME
#define CLOCK_MONOTONIC _CLOCK_MONOTONIC
time_t time(time_t *);
clock_t clock(void);
double difftime(time_t, time_t);
time_t mktime(struct tm *);
struct tm *localtime(const time_t *); struct tm *gmtime(const time_t *);
struct tm *localtime_r(const time_t *, struct tm *); struct tm *gmtime_r(const time_t *, struct tm *);
size_t strftime(char *, size_t, const char *, const struct tm *);
char *asctime(const struct tm *); char *ctime(const time_t *);
int nanosleep(const struct timespec *, struct timespec *);
int clock_gettime(clockid_t, struct timespec *);
int timespec_get(struct timespec *, int);
#ifdef __cplusplus
}
#endif
