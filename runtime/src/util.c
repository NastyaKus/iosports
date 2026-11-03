#include "internal.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int rt_verbose_env = -1;

static void vlog(const char *prefix, const char *fmt, va_list ap) {
    char buf[2048];
    int n = snprintf(buf, sizeof buf, "%s", prefix);
    int m = vsnprintf(buf + n, sizeof buf - (size_t)n - 2, fmt, ap);
    if (m < 0) m = 0;
    size_t len = (size_t)n + ((size_t)m < sizeof buf - (size_t)n - 2 ? (size_t)m : sizeof buf - (size_t)n - 3);
    buf[len++] = '\n';
    rt_out(2, buf, len);
}

void rt_log(const char *fmt, ...) {
    if (rt_verbose_env < 0) rt_verbose_env = getenv("IOSRT_VERBOSE") != NULL;
    if (!rt_verbose_env) return;
    va_list ap; va_start(ap, fmt); vlog("[iosrt] ", fmt, ap); va_end(ap);
}

void rt_warn(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vlog("[iosrt] ", fmt, ap); va_end(ap);
}

void *rt_xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) { rt_warn("out of memory (%zu bytes)", n); abort(); }
    return p;
}
void *rt_xcalloc(size_t n, size_t m) {
    void *p = calloc(n ? n : 1, m ? m : 1);
    if (!p) { rt_warn("out of memory"); abort(); }
    return p;
}
char *rt_xstrdup(const char *s) {
    size_t n = strlen(s);
    char *p = rt_xmalloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

void rt_sb_init(rt_sb *sb) { sb->buf = NULL; sb->len = sb->cap = 0; }
static void sb_grow(rt_sb *sb, size_t extra) {
    if (sb->len + extra + 1 <= sb->cap) return;
    size_t nc = sb->cap ? sb->cap * 2 : 64;
    while (nc < sb->len + extra + 1) nc *= 2;
    sb->buf = realloc(sb->buf, nc);
    if (!sb->buf) abort();
    sb->cap = nc;
}
void rt_sb_putc(rt_sb *sb, char c) { sb_grow(sb, 1); sb->buf[sb->len++] = c; sb->buf[sb->len] = 0; }
void rt_sb_append(rt_sb *sb, const char *s, size_t n) {
    sb_grow(sb, n); memcpy(sb->buf + sb->len, s, n); sb->len += n; sb->buf[sb->len] = 0;
}
void rt_sb_puts(rt_sb *sb, const char *s) { rt_sb_append(sb, s, strlen(s)); }
void rt_sb_printf(rt_sb *sb, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt); va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n > 0) {
        sb_grow(sb, (size_t)n);
        vsnprintf(sb->buf + sb->len, (size_t)n + 1, fmt, ap2);
        sb->len += (size_t)n;
    }
    va_end(ap2);
}
char *rt_sb_take(rt_sb *sb) {
    if (!sb->buf) { sb_grow(sb, 0); sb->buf[0] = 0; }
    char *r = sb->buf; sb->buf = NULL; sb->len = sb->cap = 0;
    return r;
}

/* ------------------------------------------------------------- platform */
static void host_write(int fd, const char *buf, size_t len) {
    while (len) {
        ssize_t w = write(fd, buf, len);
        if (w <= 0) return;
        buf += w; len -= (size_t)w;
    }
}
static struct rt_platform host_platform = { "host", host_write, NULL, NULL, NULL, NULL };
struct rt_platform *rt_plat = &host_platform;
void rt_platform_set_host(void) { rt_plat = &host_platform; }

static pthread_mutex_t out_lock = PTHREAD_MUTEX_INITIALIZER;
void rt_out(int fd, const char *buf, size_t len) {
    pthread_mutex_lock(&out_lock);
    rt_plat->write_out(fd, buf, len);
    pthread_mutex_unlock(&out_lock);
}

/* platform hook for symbols the HLE table lacks (Android: libc++ remapping) */
__attribute__((weak)) void *rt_platform_dlsym_fallback(const char *name) { (void)name; return NULL; }
