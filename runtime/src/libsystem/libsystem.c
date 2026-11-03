/*
 * libSystem HLE: the subset of Darwin libc / libpthread / libdispatch /
 * libkern the guest needs, implemented on top of the host libc (bionic or
 * glibc). Most functions with identical C ABI are exported directly from the
 * host (see exports/libsystem.def); this file only contains the ones whose
 * ABI, struct layout or constants differ between Darwin and Linux/Android.
 */
#include "../internal.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <sched.h>
#include <math.h>
#include <ctype.h>
#include <malloc.h>

/* ------------------------------------------------------------ errno */
static __thread int darwin_errno;
static int map_errno(int e) {
    switch (e) {
    case EAGAIN: return 35;
    case EINPROGRESS: return 36;
    case EALREADY: return 37;
    case ENOTSOCK: return 38;
    case EDEADLK: return 11;
    case ENAMETOOLONG: return 63;
    case ENOTEMPTY: return 66;
    case ENOSYS: return 78;
    case ETIMEDOUT: return 60;
    case ECONNREFUSED: return 61;
    case ELOOP: return 62;
    case EOVERFLOW: return 84;
    case ENOTSUP: return 45;
    default: return e <= 34 ? e : e;   /* 1..34 identical on both */
    }
}
static void sync_errno(void) { darwin_errno = map_errno(errno); }
int *iosrt___error(void) { return &darwin_errno; }

/* -------------------------------------------------------------- stdio */
FILE *iosrt_stdinp, *iosrt_stdoutp, *iosrt_stderrp;
static int fd_of(FILE *f) { return f == stdout ? 1 : f == stderr ? 2 : -1; }

static void write_file(FILE *f, const char *s, size_t n) {
    int fd = fd_of(f);
    if (fd > 0) rt_out(fd, s, n);
    else fwrite(s, 1, n, f);
}

static char *vformat(const char *fmt, const uint64_t *args, size_t *len) {
    rt_sb sb; rt_sb_init(&sb);
    struct rt_va va = { args };
    rt_format(&sb, fmt, &va, 1);
    if (len) *len = sb.len;
    return rt_sb_take(&sb);
}

int iosrt_darwin_vfprintf(FILE *f, const char *fmt, const uint64_t *args) {
    size_t n; char *s = vformat(fmt, args, &n);
    write_file(f, s, n); free(s);
    return (int)n;
}
int iosrt_darwin_printf(const char *fmt, const uint64_t *args) { return iosrt_darwin_vfprintf(stdout, fmt, args); }
int iosrt_darwin_vprintf(const char *fmt, const uint64_t *args) { return iosrt_darwin_vfprintf(stdout, fmt, args); }
int iosrt_darwin_fprintf(FILE *f, const char *fmt, const uint64_t *args) { return iosrt_darwin_vfprintf(f, fmt, args); }
int iosrt_darwin_dprintf(int fd, const char *fmt, const uint64_t *args) {
    size_t n; char *s = vformat(fmt, args, &n);
    if (fd == 1 || fd == 2) rt_out(fd, s, n); else n = (size_t)write(fd, s, n);
    free(s); return (int)n;
}
int iosrt_darwin_vsnprintf(char *buf, size_t size, const char *fmt, const uint64_t *args) {
    size_t n; char *s = vformat(fmt, args, &n);
    if (size) { size_t c = n < size - 1 ? n : size - 1; memcpy(buf, s, c); buf[c] = 0; }
    free(s); return (int)n;
}
int iosrt_darwin_snprintf(char *buf, size_t size, const char *fmt, const uint64_t *args) { return iosrt_darwin_vsnprintf(buf, size, fmt, args); }
int iosrt_darwin_vsprintf(char *buf, const char *fmt, const uint64_t *args) { return iosrt_darwin_vsnprintf(buf, (size_t)-1 >> 1, fmt, args); }
int iosrt_darwin_sprintf(char *buf, const char *fmt, const uint64_t *args) { return iosrt_darwin_vsprintf(buf, fmt, args); }
int iosrt_darwin_sprintf_chk(char *buf, int flag, size_t slen, const char *fmt, const uint64_t *args) {
    (void)flag; return iosrt_darwin_vsnprintf(buf, slen, fmt, args);
}
int iosrt_darwin_snprintf_chk(char *buf, size_t maxlen, int flag, size_t slen, const char *fmt, const uint64_t *args) {
    (void)flag; (void)slen; return iosrt_darwin_vsnprintf(buf, maxlen, fmt, args);
}
int iosrt_darwin_vsnprintf_chk(char *buf, size_t maxlen, int flag, size_t slen, const char *fmt, const uint64_t *args) {
    (void)flag; (void)slen; return iosrt_darwin_vsnprintf(buf, maxlen, fmt, args);
}
int iosrt_darwin_vasprintf(char **out, const char *fmt, const uint64_t *args) {
    size_t n; *out = vformat(fmt, args, &n); return (int)n;
}
int iosrt_darwin_asprintf(char **out, const char *fmt, const uint64_t *args) { return iosrt_darwin_vasprintf(out, fmt, args); }

int iosrt_darwin_sscanf(const char *str, const char *fmt, const uint64_t *args) {
    /* forward up to 8 pointer arguments; all scanf targets are pointers */
    return sscanf(str, fmt, (void *)args[0], (void *)args[1], (void *)args[2], (void *)args[3],
                  (void *)args[4], (void *)args[5], (void *)args[6], (void *)args[7]);
}

int iosrt_puts(const char *s) { rt_out(1, s, strlen(s)); rt_out(1, "\n", 1); return 1; }
int iosrt_putchar(int c) { char ch = (char)c; rt_out(1, &ch, 1); return c & 0xff; }
int iosrt_fputs(const char *s, FILE *f) { write_file(f, s, strlen(s)); return 1; }
int iosrt_fputc(int c, FILE *f) { char ch = (char)c; write_file(f, &ch, 1); return c & 0xff; }
size_t iosrt_fwrite(const void *p, size_t sz, size_t n, FILE *f) {
    if (fd_of(f) > 0) { rt_out(fd_of(f), p, sz * n); return n; }
    return fwrite(p, sz, n, f);
}
int iosrt_fflush(FILE *f) { if (!f || fd_of(f) > 0) return 0; return fflush(f); }
ssize_t iosrt_write(int fd, const void *p, size_t n) {
    if (fd == 1 || fd == 2) { rt_out(fd, p, n); return (ssize_t)n; }
    ssize_t r = write(fd, p, n); if (r < 0) sync_errno(); return r;
}
void iosrt_perror(const char *s) {
    char b[512]; snprintf(b, sizeof b, "%s%s%s\n", s ? s : "", s ? ": " : "", strerror(errno));
    rt_out(2, b, strlen(b));
}
FILE *iosrt_fopen(const char *path, const char *mode) { FILE *f = fopen(path, mode); if (!f) sync_errno(); return f; }

/* ----------------------------------------------------- open/fcntl flags */
static int map_oflags(int d) {
    int f = d & 3;                      /* O_RDONLY/O_WRONLY/O_RDWR identical */
    if (d & 0x0004) f |= O_NONBLOCK;
    if (d & 0x0008) f |= O_APPEND;
    if (d & 0x0200) f |= O_CREAT;
    if (d & 0x0400) f |= O_TRUNC;
    if (d & 0x0800) f |= O_EXCL;
    if (d & 0x20000) f |= O_NOCTTY;
    if (d & 0x100000) f |= O_DIRECTORY;
    if (d & 0x1000000) f |= O_CLOEXEC;
    if (d & 0x80) f |= O_SYNC;
    return f;
}
int iosrt_darwin_open(const char *path, int flags, const uint64_t *args) {
    int r = open(path, map_oflags(flags), (flags & 0x200) ? (mode_t)args[0] : 0);
    if (r < 0) sync_errno(); return r;
}
int iosrt_darwin_openat(int dfd, const char *path, int flags, const uint64_t *args) {
    int r = openat(dfd == -2 ? AT_FDCWD : dfd, path, map_oflags(flags), (flags & 0x200) ? (mode_t)args[0] : 0);
    if (r < 0) sync_errno(); return r;
}
int iosrt_darwin_fcntl(int fd, int cmd, const uint64_t *args) {
    int r;
    switch (cmd) {
    case 1: r = fcntl(fd, F_GETFD); break;
    case 2: r = fcntl(fd, F_SETFD, (int)args[0]); break;
    case 3: { r = fcntl(fd, F_GETFL); break; }
    case 4: r = fcntl(fd, F_SETFL, map_oflags((int)args[0])); break;
    case 51: r = 0; break;                /* F_FULLFSYNC */
    case 48: case 67: r = 0; break;       /* F_NOCACHE / F_SETNOSIGPIPE */
    default: errno = EINVAL; r = -1;
    }
    if (r < 0) sync_errno(); return r;
}
int iosrt_darwin_ioctl(int fd, unsigned long req, const uint64_t *args) { (void)fd; (void)req; (void)args; darwin_errno = 25; return -1; }

ssize_t iosrt_read(int fd, void *b, size_t n) { ssize_t r = read(fd, b, n); if (r < 0) sync_errno(); return r; }
int iosrt_close(int fd) { if (fd <= 2) return 0; int r = close(fd); if (r < 0) sync_errno(); return r; }

/* ---------------------------------------------------------- stat */
struct darwin_timespec { long tv_sec; long tv_nsec; };
struct darwin_stat {
    int32_t st_dev; uint16_t st_mode; uint16_t st_nlink; uint64_t st_ino;
    uint32_t st_uid, st_gid; int32_t st_rdev; int32_t pad_;
    struct darwin_timespec st_atimespec, st_mtimespec, st_ctimespec, st_birthtimespec;
    int64_t st_size, st_blocks; int32_t st_blksize; uint32_t st_flags, st_gen; int32_t st_lspare; int64_t st_qspare[2];
};
static void conv_stat(const struct stat *s, struct darwin_stat *d) {
    memset(d, 0, sizeof *d);
    d->st_dev = (int32_t)s->st_dev; d->st_mode = (uint16_t)s->st_mode; d->st_nlink = (uint16_t)s->st_nlink;
    d->st_ino = s->st_ino; d->st_uid = s->st_uid; d->st_gid = s->st_gid; d->st_rdev = (int32_t)s->st_rdev;
    d->st_atimespec.tv_sec = s->st_atim.tv_sec; d->st_atimespec.tv_nsec = s->st_atim.tv_nsec;
    d->st_mtimespec.tv_sec = s->st_mtim.tv_sec; d->st_mtimespec.tv_nsec = s->st_mtim.tv_nsec;
    d->st_ctimespec.tv_sec = s->st_ctim.tv_sec; d->st_ctimespec.tv_nsec = s->st_ctim.tv_nsec;
    d->st_birthtimespec = d->st_ctimespec;
    d->st_size = s->st_size; d->st_blocks = s->st_blocks; d->st_blksize = (int32_t)s->st_blksize;
}
int iosrt_stat(const char *p, struct darwin_stat *d) { struct stat s; int r = stat(p, &s); if (r) { sync_errno(); return r; } conv_stat(&s, d); return 0; }
int iosrt_lstat(const char *p, struct darwin_stat *d) { struct stat s; int r = lstat(p, &s); if (r) { sync_errno(); return r; } conv_stat(&s, d); return 0; }
int iosrt_fstat(int fd, struct darwin_stat *d) { struct stat s; int r = fstat(fd, &s); if (r) { sync_errno(); return r; } conv_stat(&s, d); return 0; }
int iosrt_mkdir(const char *p, mode_t m) { int r = mkdir(p, m); if (r) sync_errno(); return r; }
int iosrt_unlink(const char *p) { int r = unlink(p); if (r) sync_errno(); return r; }
int iosrt_access(const char *p, int m) { int r = access(p, m); if (r) sync_errno(); return r; }

/* ---------------------------------------------------------- dirent */
struct darwin_dirent { uint64_t d_ino, d_seekoff; uint16_t d_reclen, d_namlen; uint8_t d_type; char d_name[1024]; };
struct rt_dir { DIR *d; struct darwin_dirent ent; };
void *iosrt_opendir(const char *p) {
    DIR *d = opendir(p); if (!d) { sync_errno(); return NULL; }
    struct rt_dir *r = rt_xcalloc(1, sizeof *r); r->d = d; return r;
}
struct darwin_dirent *iosrt_readdir(struct rt_dir *r) {
    struct dirent *e = readdir(r->d);
    if (!e) return NULL;
    r->ent.d_ino = e->d_ino; r->ent.d_type = e->d_type;
    size_t n = strnlen(e->d_name, 1023);
    memcpy(r->ent.d_name, e->d_name, n); r->ent.d_name[n] = 0;
    r->ent.d_namlen = (uint16_t)n; r->ent.d_reclen = sizeof r->ent;
    return &r->ent;
}
int iosrt_closedir(struct rt_dir *r) { int x = closedir(r->d); free(r); return x; }

/* -------------------------------------------------------------- mmap */
void *iosrt_mmap(void *a, size_t len, int prot, int flags, int fd, off_t off) {
    int f = flags & 3;
    if (flags & 0x10) f |= MAP_FIXED;
    if (flags & 0x1000) f |= MAP_ANONYMOUS;
    void *r = mmap(a, len, prot, f, (flags & 0x1000) ? -1 : fd, off);
    if (r == MAP_FAILED) sync_errno();
    return r;
}

/* ------------------------------------------------------------ sysconf */
long iosrt_sysconf(int name) {
    switch (name) {
    case 29: return sysconf(_SC_PAGESIZE);
    case 57: return sysconf(_SC_NPROCESSORS_CONF);
    case 58: return sysconf(_SC_NPROCESSORS_ONLN);
    case 5: return sysconf(_SC_OPEN_MAX);
    case 3: return sysconf(_SC_CLK_TCK);
    default: darwin_errno = EINVAL; return -1;
    }
}
int iosrt_sysctlbyname(const char *name, void *old, size_t *oldlen, const void *nw, size_t nwlen) {
    (void)nw; (void)nwlen;
    const char *sval = NULL; long ival = 0; int isint = 0;
    if (!strcmp(name, "hw.machine")) sval = "iPhone10,3";
    else if (!strcmp(name, "hw.model")) sval = "D22AP";
    else if (!strcmp(name, "kern.osversion")) sval = "15E148";
    else if (!strcmp(name, "hw.ncpu") || !strcmp(name, "hw.logicalcpu") || !strcmp(name, "hw.activecpu")) { isint = 1; ival = sysconf(_SC_NPROCESSORS_ONLN); }
    else if (!strcmp(name, "hw.memsize")) { isint = 2; ival = (long)sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE); }
    else { darwin_errno = 2; return -1; }
    if (sval) {
        size_t n = strlen(sval) + 1;
        if (old && oldlen && *oldlen >= n) memcpy(old, sval, n);
        if (oldlen) *oldlen = n;
    } else {
        size_t n = isint == 2 ? 8 : 4;
        if (old && oldlen && *oldlen >= n) { if (n == 8) memcpy(old, &ival, 8); else { int v = (int)ival; memcpy(old, &v, 4); } }
        if (oldlen) *oldlen = n;
    }
    return 0;
}

/* ------------------------------------------------------------ time */
static struct timespec t0;
static void time_init(void) { if (!t0.tv_sec) clock_gettime(CLOCK_MONOTONIC, &t0); }
uint64_t iosrt_mach_absolute_time(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
int iosrt_mach_timebase_info(uint32_t *info) { info[0] = 1; info[1] = 1; return 0; }
uint64_t iosrt_mach_continuous_time(void) { return iosrt_mach_absolute_time(); }
static clockid_t map_clock(int c) {
    switch (c) { case 0: return CLOCK_REALTIME; case 6: return CLOCK_MONOTONIC; case 4: return CLOCK_MONOTONIC_RAW;
                 case 12: return CLOCK_PROCESS_CPUTIME_ID; case 16: return CLOCK_THREAD_CPUTIME_ID; default: return CLOCK_MONOTONIC; }
}
int iosrt_clock_gettime(int c, struct timespec *ts) { return clock_gettime(map_clock(c), ts); }
uint64_t iosrt_clock_gettime_nsec_np(int c) { struct timespec t; clock_gettime(map_clock(c), &t); return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec; }

/* -------------------------------------------------------- pthread */
/* Darwin static initializers carry a signature word; host objects fit in the
 * Darwin-sized storage (mutex 64 B, cond 48 B), so we lazily construct. */
#define DARWIN_MUTEX_SIG_INIT 0x32AAABA7L
#define DARWIN_RMUTEX_SIG_INIT 0x32AAABA1L
#define DARWIN_COND_SIG_INIT 0x3CB0B1BBL
static pthread_mutex_t lazy_lock = PTHREAD_MUTEX_INITIALIZER;
static void mutex_fix(pthread_mutex_t *m) {
    long sig; memcpy(&sig, m, sizeof sig);
    if (sig != DARWIN_MUTEX_SIG_INIT && sig != DARWIN_RMUTEX_SIG_INIT) return;
    pthread_mutex_lock(&lazy_lock);
    memcpy(&sig, m, sizeof sig);
    if (sig == DARWIN_MUTEX_SIG_INIT || sig == DARWIN_RMUTEX_SIG_INIT) {
        pthread_mutexattr_t a; pthread_mutexattr_init(&a);
        if (sig == DARWIN_RMUTEX_SIG_INIT) pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(m, &a);
    }
    pthread_mutex_unlock(&lazy_lock);
}
int iosrt_pthread_mutex_lock(pthread_mutex_t *m) { mutex_fix(m); return pthread_mutex_lock(m); }
int iosrt_pthread_mutex_trylock(pthread_mutex_t *m) { mutex_fix(m); int r = pthread_mutex_trylock(m); return r == EBUSY ? 16 : r; }
int iosrt_pthread_mutex_unlock(pthread_mutex_t *m) { mutex_fix(m); return pthread_mutex_unlock(m); }
int iosrt_pthread_mutex_destroy(pthread_mutex_t *m) { mutex_fix(m); return pthread_mutex_destroy(m); }
/* Darwin mutexattr types: NORMAL 0, ERRORCHECK 1, RECURSIVE 2 */
int iosrt_pthread_mutexattr_settype(pthread_mutexattr_t *a, int t) {
    return pthread_mutexattr_settype(a, t == 2 ? PTHREAD_MUTEX_RECURSIVE : t == 1 ? PTHREAD_MUTEX_ERRORCHECK : PTHREAD_MUTEX_NORMAL);
}
static void cond_fix(pthread_cond_t *c) {
    long sig; memcpy(&sig, c, sizeof sig);
    if (sig != DARWIN_COND_SIG_INIT) return;
    pthread_mutex_lock(&lazy_lock);
    memcpy(&sig, c, sizeof sig);
    if (sig == DARWIN_COND_SIG_INIT) pthread_cond_init(c, NULL);
    pthread_mutex_unlock(&lazy_lock);
}
int iosrt_pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) { cond_fix(c); mutex_fix(m); return pthread_cond_wait(c, m); }
int iosrt_pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *t) {
    cond_fix(c); mutex_fix(m); int r = pthread_cond_timedwait(c, m, t); return r == ETIMEDOUT ? 60 : r;
}
int iosrt_pthread_cond_signal(pthread_cond_t *c) { cond_fix(c); return pthread_cond_signal(c); }
int iosrt_pthread_cond_broadcast(pthread_cond_t *c) { cond_fix(c); return pthread_cond_broadcast(c); }
int iosrt_pthread_cond_destroy(pthread_cond_t *c) { cond_fix(c); return pthread_cond_destroy(c); }
/* pthread_once_t on Darwin: { long sig; char opaque[8]; } */
static int once_begin2(long *pred);
int iosrt_pthread_once(long *once, void (*fn)(void)) {
    if (once_begin2(&once[1])) { fn(); __atomic_store_n(&once[1], ~0L, __ATOMIC_RELEASE); }
    return 0;
}
struct thread_start { void *(*fn)(void *); void *arg; };
void rt_thread_register(void);
void rt_thread_unregister(void);
static void *thread_tramp(void *p) {
    struct thread_start s = *(struct thread_start *)p; free(p);
    rt_thread_register();
    void *pool = objc_autoreleasePoolPush();
    void *r = s.fn(s.arg);
    objc_autoreleasePoolPop(pool);
    rt_thread_unregister();
    return r;
}
int iosrt_pthread_create(pthread_t *t, const pthread_attr_t *a, void *(*fn)(void *), void *arg) {
    struct thread_start *s = rt_xmalloc(sizeof *s); s->fn = fn; s->arg = arg;
    (void)a;     /* Darwin attr layout differs; use defaults with a generous stack */
    pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setstacksize(&at, 1 << 20);
    int r = pthread_create(t, &at, thread_tramp, s);
    pthread_attr_destroy(&at);
    return r;
}
int iosrt_pthread_setname_np(const char *n) { char b[16]; snprintf(b, sizeof b, "%s", n); return pthread_setname_np(pthread_self(), b); }
int iosrt_pthread_threadid_np(pthread_t t, uint64_t *id) { (void)t; if (id) *id = (uint64_t)gettid(); return 0; }
int iosrt_pthread_main_np(void) { return rt_is_guest_main_thread(); }
/* Darwin pthread_key_create returns keys usable across the process — identical API */

/* --------------------------------------------------- os_unfair_lock etc */
void iosrt_os_unfair_lock_lock(uint32_t *l) {
    uint32_t z = 0;
    while (!__atomic_compare_exchange_n(l, &z, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) { z = 0; sched_yield(); }
}
void iosrt_os_unfair_lock_unlock(uint32_t *l) { __atomic_store_n(l, 0, __ATOMIC_RELEASE); }
long iosrt_os_unfair_lock_trylock(uint32_t *l) { uint32_t z = 0; return __atomic_compare_exchange_n(l, &z, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED); }
void iosrt_os_unfair_lock_assert_owner(uint32_t *l) { (void)l; }
int32_t iosrt_OSAtomicAdd32(int32_t v, volatile int32_t *p) { return __atomic_add_fetch(p, v, __ATOMIC_SEQ_CST); }
int32_t iosrt_OSAtomicIncrement32(volatile int32_t *p) { return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST); }
int32_t iosrt_OSAtomicDecrement32(volatile int32_t *p) { return __atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST); }
int64_t iosrt_OSAtomicAdd64(int64_t v, volatile int64_t *p) { return __atomic_add_fetch(p, v, __ATOMIC_SEQ_CST); }
int64_t iosrt_OSAtomicIncrement64(volatile int64_t *p) { return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST); }
int64_t iosrt_OSAtomicDecrement64(volatile int64_t *p) { return __atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST); }
rt_bool iosrt_OSAtomicCompareAndSwap32(int32_t o, int32_t n, volatile int32_t *p) { return __atomic_compare_exchange_n(p, &o, n, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }
rt_bool iosrt_OSAtomicCompareAndSwap64(int64_t o, int64_t n, volatile int64_t *p) { return __atomic_compare_exchange_n(p, &o, n, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }
rt_bool iosrt_OSAtomicCompareAndSwapPtr(void *o, void *n, void *volatile *p) { return __atomic_compare_exchange_n(p, &o, n, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST); }
void iosrt_OSMemoryBarrier(void) { __atomic_thread_fence(__ATOMIC_SEQ_CST); }
void iosrt_OSSpinLockLock(volatile int32_t *l) { int32_t z = 0; while (!__atomic_compare_exchange_n(l, &z, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) { z = 0; sched_yield(); } }
void iosrt_OSSpinLockUnlock(volatile int32_t *l) { __atomic_store_n(l, 0, __ATOMIC_RELEASE); }
rt_bool iosrt_OSSpinLockTry(volatile int32_t *l) { int32_t z = 0; return __atomic_compare_exchange_n(l, &z, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED); }

/* ------------------------------------------------------- misc libc */
uintptr_t iosrt_stack_chk_guard = 0x5f1e5a0d00c0ffeeULL;
_Noreturn void iosrt_stack_chk_fail(void) { rt_guest_exit(134, "stack smashing detected"); }
_Noreturn void iosrt_abort(void) { rt_guest_exit(134, "abort() called"); }
_Noreturn void iosrt_assert_rtn(const char *fn, const char *file, int line, const char *expr) {
    char b[512]; snprintf(b, sizeof b, "assertion failed: (%s), function %s, file %s:%d", expr, fn, file, line);
    rt_guest_exit(134, b);
}

#define MAX_ATEXIT 64
static struct { void (*fn)(void *); void *arg; int cxa; } atexit_fns[MAX_ATEXIT];
static int n_atexit;
int iosrt_atexit(void (*fn)(void)) { if (n_atexit < MAX_ATEXIT) { atexit_fns[n_atexit].fn = (void (*)(void *))fn; atexit_fns[n_atexit++].cxa = 0; } return 0; }
int iosrt_cxa_atexit(void (*fn)(void *), void *arg, void *dso) { (void)dso; if (n_atexit < MAX_ATEXIT) { atexit_fns[n_atexit].fn = fn; atexit_fns[n_atexit].arg = arg; atexit_fns[n_atexit++].cxa = 1; } return 0; }
void rt_run_atexit(void) {
    while (n_atexit > 0) {
        n_atexit--;
        if (atexit_fns[n_atexit].cxa) atexit_fns[n_atexit].fn(atexit_fns[n_atexit].arg);
        else ((void (*)(void))atexit_fns[n_atexit].fn)();
    }
}
_Noreturn void iosrt_exit(int code) { rt_run_atexit(); rt_guest_exit(code, NULL); }
_Noreturn void iosrt__exit(int code) { rt_guest_exit(code, NULL); }

uint32_t iosrt_arc4random(void) {
    static uint64_t s; static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&m);
    if (!s) { struct timespec t; clock_gettime(CLOCK_REALTIME, &t); s = (uint64_t)t.tv_nsec ^ ((uint64_t)t.tv_sec << 20) ^ (uint64_t)getpid() ^ 0x9E3779B97F4A7C15ULL; }
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
    uint32_t r = (uint32_t)(s >> 16);
    pthread_mutex_unlock(&m);
    return r;
}
uint32_t iosrt_arc4random_uniform(uint32_t n) { if (n < 2) return 0; uint32_t min = (uint32_t)(-n) % n, r; do r = iosrt_arc4random(); while (r < min); return r % n; }
void iosrt_arc4random_buf(void *b, size_t n) { uint8_t *p = b; for (size_t i = 0; i < n; i++) p[i] = (uint8_t)iosrt_arc4random(); }

size_t iosrt_malloc_size(const void *p) { return p ? malloc_usable_size((void *)p) : 0; }
size_t iosrt_strlcpy(char *d, const char *s, size_t n) { size_t l = strlen(s); if (n) { size_t c = l < n - 1 ? l : n - 1; memcpy(d, s, c); d[c] = 0; } return l; }
size_t iosrt_strlcat(char *d, const char *s, size_t n) { size_t dl = strnlen(d, n); if (dl == n) return n + strlen(s); return dl + iosrt_strlcpy(d + dl, s, n - dl); }
void iosrt_memset_pattern4(void *b, const void *pat, size_t len) { uint8_t *p = b; for (size_t i = 0; i < len; i++) p[i] = ((const uint8_t *)pat)[i % 4]; }
void iosrt_memset_pattern8(void *b, const void *pat, size_t len) { uint8_t *p = b; for (size_t i = 0; i < len; i++) p[i] = ((const uint8_t *)pat)[i % 8]; }
void iosrt_memset_pattern16(void *b, const void *pat, size_t len) { uint8_t *p = b; for (size_t i = 0; i < len; i++) p[i] = ((const uint8_t *)pat)[i % 16]; }
void *iosrt_memcpy_chk(void *d, const void *s, size_t n, size_t dl) { (void)dl; return memcpy(d, s, n); }
void *iosrt_memmove_chk(void *d, const void *s, size_t n, size_t dl) { (void)dl; return memmove(d, s, n); }
void *iosrt_memset_chk(void *d, int c, size_t n, size_t dl) { (void)dl; return memset(d, c, n); }
char *iosrt_strcpy_chk(char *d, const char *s, size_t dl) { (void)dl; return strcpy(d, s); }
char *iosrt_strncpy_chk(char *d, const char *s, size_t n, size_t dl) { (void)dl; return strncpy(d, s, n); }
char *iosrt_strcat_chk(char *d, const char *s, size_t dl) { (void)dl; return strcat(d, s); }
char *iosrt_strncat_chk(char *d, const char *s, size_t n, size_t dl) { (void)dl; return strncat(d, s, n); }
size_t iosrt_strlcpy_chk(char *d, const char *s, size_t n, size_t dl) { (void)dl; return iosrt_strlcpy(d, s, n); }

/* ctype: Darwin inlines isalpha() & co as lookups into _DefaultRuneLocale */
#define CT_A 0x100L
#define CT_C 0x200L
#define CT_D 0x400L
#define CT_G 0x800L
#define CT_L 0x1000L
#define CT_P 0x2000L
#define CT_S 0x4000L
#define CT_U 0x8000L
#define CT_X 0x10000L
#define CT_B 0x20000L
#define CT_R 0x40000L
static uint32_t rune_bits(int c) {
    if (c < 0 || c > 255) return 0;
    uint32_t m = 0;
    if (c < 128) {
        if (isalpha(c)) m |= CT_A; if (iscntrl(c)) m |= CT_C; if (isdigit(c)) m |= CT_D | (uint32_t)(c - '0');
        if (isgraph(c)) m |= CT_G; if (islower(c)) m |= CT_L; if (ispunct(c)) m |= CT_P; if (isspace(c)) m |= CT_S;
        if (isupper(c)) m |= CT_U; if (isxdigit(c)) m |= CT_X; if (c == ' ' || c == '\t') m |= CT_B; if (isprint(c)) m |= CT_R;
    }
    return m;
}
struct darwin_rune_locale {
    char magic[8]; char encoding[32]; void *sgetrune, *sputrune; int32_t invalid_rune;
    uint32_t runetype[256]; int32_t maplower[256]; int32_t mapupper[256];
    char rest[256];
};
struct darwin_rune_locale iosrt_DefaultRuneLocale;
static void ctype_init(void) {
    memcpy(iosrt_DefaultRuneLocale.magic, "RuneMagA", 8);
    strcpy(iosrt_DefaultRuneLocale.encoding, "NONE");
    for (int c = 0; c < 256; c++) {
        iosrt_DefaultRuneLocale.runetype[c] = rune_bits(c);
        iosrt_DefaultRuneLocale.maplower[c] = c < 128 ? tolower(c) : c;
        iosrt_DefaultRuneLocale.mapupper[c] = c < 128 ? toupper(c) : c;
    }
}
unsigned long iosrt_maskrune(int c, unsigned long mask) { return rune_bits(c) & mask; }
int iosrt_toupper(int c) { return c >= 0 && c < 128 ? toupper(c) : c; }
int iosrt_tolower(int c) { return c >= 0 && c < 128 ? tolower(c) : c; }

int iosrt_NSGetExecutablePath(char *buf, uint32_t *size) {
    const char *p = rt_main_image ? rt_main_image->path : "/app";
    size_t n = strlen(p) + 1;
    if (*size < n) { *size = (uint32_t)n; return -1; }
    memcpy(buf, p, n); return 0;
}
static int g_argc; static char **g_argv; static char **g_environ;
int *iosrt_NSGetArgc(void) { return &g_argc; }
char ***iosrt_NSGetArgv(void) { return &g_argv; }
char ***iosrt_NSGetEnviron(void) { return &g_environ; }
void rt_set_args(int argc, char **argv, char **envp) { g_argc = argc; g_argv = argv; g_environ = envp; }

/* dlopen/dlsym for guests resolve against the HLE export table */
void *rt_dlsym_name(const char *cname);
void *iosrt_dlopen(const char *path, int mode) { (void)mode; (void)path; return (void *)(uintptr_t)0x1; }
void *iosrt_dlsym(void *h, const char *name) { (void)h; return rt_dlsym_name(name); }
int iosrt_dlclose(void *h) { (void)h; return 0; }
const char *iosrt_dlerror(void) { return "symbol not provided by iosrt"; }

/* libm Darwin extras */
double iosrt_exp10(double x) { return pow(10.0, x); }
float iosrt_exp10f(float x) { return powf(10.0f, x); }

/* ---------------------------------------------------------- libdispatch */
/* Queues: the main queue is drained by the guest main thread's run loop; private
 * serial queues own one worker thread and run items FIFO; concurrent/global queues
 * run each item on its own thread. dispatch_sync waits for its item on serial/main. */
struct rt_dq_item { void (*fn)(void *); void *ctx; void *block; struct rt_dq_item *next; struct rt_queue *q;
                    pthread_mutex_t *done_m; pthread_cond_t *done_c; volatile int *done; };
struct rt_queue { const char *label; int main; int concurrent; pthread_mutex_t m; struct rt_dq_item *head, *tail; int busy; };
static struct rt_dq_item *mq_head, *mq_tail;
static pthread_mutex_t mq_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t mq_cond = PTHREAD_COND_INITIALIZER;
struct rt_queue iosrt_dispatch_main_q = { "com.apple.main-thread", 1, 0, PTHREAD_MUTEX_INITIALIZER, NULL, NULL, 0 };
static struct rt_queue global_q = { "com.apple.root.default-qos", 0, 1, PTHREAD_MUTEX_INITIALIZER, NULL, NULL, 0 };
static __thread struct rt_queue *cur_queue;
int iosrt_dispatch_queue_attr_concurrent[4];

void *iosrt_dispatch_get_global_queue(long prio, unsigned long flags) { (void)prio; (void)flags; return &global_q; }
void *iosrt_dispatch_queue_create(const char *label, void *attr) {
    struct rt_queue *q = rt_xcalloc(1, sizeof *q);
    q->label = rt_xstrdup(label ? label : "");
    q->concurrent = attr == (void *)iosrt_dispatch_queue_attr_concurrent;
    pthread_mutex_init(&q->m, NULL);
    return q;
}
static void block_invoke(void *b) { ((void (*)(void *))((struct rt_block *)b)->invoke)(b); }
static void run_item(struct rt_dq_item *it) {
    struct rt_queue *saved = cur_queue; cur_queue = it->q;
    void *pool = objc_autoreleasePoolPush();
    if (it->block) { block_invoke(it->block); if (!it->done) _Block_release(it->block); }
    else it->fn(it->ctx);
    objc_autoreleasePoolPop(pool);
    cur_queue = saved;
    if (it->done) { pthread_mutex_lock(it->done_m); *it->done = 1; pthread_cond_broadcast(it->done_c); pthread_mutex_unlock(it->done_m); }
    else free(it);
}
static void *item_thread(void *p) { run_item(p); return NULL; }
static void *serial_worker(void *p) {
    struct rt_queue *q = p;
    for (;;) {
        pthread_mutex_lock(&q->m);
        struct rt_dq_item *it = q->head;
        if (!it) { q->busy = 0; pthread_mutex_unlock(&q->m); return NULL; }
        q->head = it->next; if (!q->head) q->tail = NULL;
        pthread_mutex_unlock(&q->m);
        run_item(it);
    }
}
struct spawn_arg { void *(*fn)(void *); void *arg; };
static void *spawn_tramp(void *p) {
    struct spawn_arg a = *(struct spawn_arg *)p; free(p);
    rt_thread_register();
    void *r = a.fn(a.arg);
    rt_thread_unregister();
    return r;
}
static void spawn(void *(*fn)(void *), void *arg) {
    struct spawn_arg *sa = rt_xmalloc(sizeof *sa); sa->fn = fn; sa->arg = arg; fn = spawn_tramp; arg = sa;
    pthread_t t; pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&a, 1 << 20);
    pthread_create(&t, &a, fn, arg); pthread_attr_destroy(&a);
}
static void enqueue(struct rt_queue *q, struct rt_dq_item *it) {
    if (!q) q = &global_q;
    it->q = q; it->next = NULL;
    if (q->main) {
        pthread_mutex_lock(&mq_lock);
        if (mq_tail) mq_tail->next = it; else mq_head = it;
        mq_tail = it;
        pthread_cond_broadcast(&mq_cond);
        pthread_mutex_unlock(&mq_lock);
    } else if (q->concurrent) {
        spawn(item_thread, it);
    } else {
        pthread_mutex_lock(&q->m);
        if (q->tail) q->tail->next = it; else q->head = it;
        q->tail = it;
        int start = !q->busy; q->busy = 1;
        pthread_mutex_unlock(&q->m);
        if (start) spawn(serial_worker, q);
    }
}
void iosrt_dispatch_async(struct rt_queue *q, void *block) {
    struct rt_dq_item *it = rt_xcalloc(1, sizeof *it); it->block = _Block_copy(block); enqueue(q, it);
}
void iosrt_dispatch_async_f(struct rt_queue *q, void *ctx, void (*fn)(void *)) {
    struct rt_dq_item *it = rt_xcalloc(1, sizeof *it); it->fn = fn; it->ctx = ctx; enqueue(q, it);
}
static void sync_item(struct rt_queue *q, void *block, void *ctx, void (*fn)(void *)) {
    if (!q) q = &global_q;
    int inline_run = q->concurrent || cur_queue == q || (q->main && rt_is_guest_main_thread());
    if (inline_run) {
        struct rt_queue *saved = cur_queue; cur_queue = q;
        if (block) block_invoke(block); else fn(ctx);
        cur_queue = saved;
        return;
    }
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER; pthread_cond_t c = PTHREAD_COND_INITIALIZER; volatile int done = 0;
    struct rt_dq_item it; memset(&it, 0, sizeof it);
    it.block = block; it.fn = fn; it.ctx = ctx; it.done_m = &m; it.done_c = &c; it.done = &done;
    enqueue(q, &it);
    pthread_mutex_lock(&m); while (!done) pthread_cond_wait(&c, &m); pthread_mutex_unlock(&m);
}
void iosrt_dispatch_sync(struct rt_queue *q, void *block) { sync_item(q, block, NULL, NULL); }
void iosrt_dispatch_sync_f(struct rt_queue *q, void *ctx, void (*fn)(void *)) { sync_item(q, NULL, ctx, fn); }
struct after_arg { int64_t ns; struct rt_queue *q; void *block; };
static void *after_thread(void *p) {
    struct after_arg *a = p;
    struct timespec ts = { (time_t)(a->ns / 1000000000), (long)(a->ns % 1000000000) };
    nanosleep(&ts, NULL);
    iosrt_dispatch_async(a->q, a->block);
    _Block_release(a->block);
    free(a);
    return NULL;
}
uint64_t iosrt_dispatch_time(uint64_t when, int64_t delta) {
    uint64_t base = when == 0 ? iosrt_mach_absolute_time() : when;
    return base + (uint64_t)delta;
}
void iosrt_dispatch_after(uint64_t when, struct rt_queue *q, void *block) {
    struct after_arg *a = rt_xmalloc(sizeof *a);
    int64_t now = (int64_t)iosrt_mach_absolute_time();
    a->ns = when == ~0ULL ? 0 : (int64_t)when - now; if (a->ns < 0) a->ns = 0;
    a->q = q; a->block = _Block_copy(block);
    spawn(after_thread, a);
}
/* once: predicate 0 = not run, 1 = running, ~0 = done. No global lock, so nested
 * dispatch_once calls on *different* predicates work (as on Darwin). */
static int once_begin(long *pred) {
    for (;;) {
        long v = __atomic_load_n(pred, __ATOMIC_ACQUIRE);
        if (v == ~0L) return 0;
        long z = 0;
        if (v == 0 && __atomic_compare_exchange_n(pred, &z, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return 1;
        sched_yield();
    }
}
void iosrt_dispatch_once(long *pred, void *block) { if (once_begin(pred)) { block_invoke(block); __atomic_store_n(pred, ~0L, __ATOMIC_RELEASE); } }
void iosrt_dispatch_once_f(long *pred, void *ctx, void (*fn)(void *)) { if (once_begin(pred)) { fn(ctx); __atomic_store_n(pred, ~0L, __ATOMIC_RELEASE); } }
/* semaphores */
struct rt_sema { pthread_mutex_t m; pthread_cond_t c; long v; };
void *iosrt_dispatch_semaphore_create(long v) { struct rt_sema *s = rt_xcalloc(1, sizeof *s); pthread_mutex_init(&s->m, NULL); pthread_cond_init(&s->c, NULL); s->v = v; return s; }
long iosrt_dispatch_semaphore_signal(struct rt_sema *s) { pthread_mutex_lock(&s->m); s->v++; pthread_cond_signal(&s->c); pthread_mutex_unlock(&s->m); return 0; }
long iosrt_dispatch_semaphore_wait(struct rt_sema *s, uint64_t timeout) {
    pthread_mutex_lock(&s->m);
    long r = 0;
    if (timeout == ~0ULL) { while (s->v <= 0) pthread_cond_wait(&s->c, &s->m); }
    else {
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
        int64_t ns = (int64_t)timeout - (int64_t)iosrt_mach_absolute_time(); if (ns < 0) ns = 0;
        ts.tv_sec += ns / 1000000000; ts.tv_nsec += ns % 1000000000;
        if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
        while (s->v <= 0) { if (pthread_cond_timedwait(&s->c, &s->m, &ts) == ETIMEDOUT) { r = 1; break; } }
    }
    if (!r) s->v--;
    pthread_mutex_unlock(&s->m);
    return r;
}
void iosrt_dispatch_retain(void *o) { (void)o; }
void iosrt_dispatch_release(void *o) { (void)o; }
const char *iosrt_dispatch_queue_get_label(struct rt_queue *q) { return q ? q->label : "com.apple.main-thread"; }

int rt_main_queue_pending(void) { return mq_head != NULL; }
void rt_runloop_drain_main_queue(void) {
    for (;;) {
        pthread_mutex_lock(&mq_lock);
        struct rt_dq_item *it = mq_head;
        if (it) { mq_head = it->next; if (!mq_head) mq_tail = NULL; }
        pthread_mutex_unlock(&mq_lock);
        if (!it) return;
        run_item(it);
    }
}
/* wait up to `ms` for main-queue work (used by run loops) */
void rt_main_queue_wait(int ms) {
    pthread_mutex_lock(&mq_lock);
    if (!mq_head) {
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += (long)ms * 1000000L; ts.tv_sec += ts.tv_nsec / 1000000000L; ts.tv_nsec %= 1000000000L;
        pthread_cond_timedwait(&mq_cond, &mq_lock, &ts);
    }
    pthread_mutex_unlock(&mq_lock);
}
_Noreturn void iosrt_dispatch_main(void) {
    for (;;) { rt_runloop_drain_main_queue(); rt_main_queue_wait(50); }
}

void rt_libsystem_init(void) {
    iosrt_stdinp = stdin; iosrt_stdoutp = stdout; iosrt_stderrp = stderr;
    ctype_init(); time_init();
}

/* ------------------------------------------------------- small extras */
size_t iosrt_malloc_good_size(size_t n) { return (n + 15) & ~(size_t)15; }
void iosrt_bzero(void *p, size_t n) { memset(p, 0, n); }
void *iosrt_signal(int sig, void *handler) { (void)sig; (void)handler; return NULL; }   /* Darwin signal numbers differ: ignored */
int iosrt_raise(int sig) { char b[64]; snprintf(b, sizeof b, "raise(%d)", sig); rt_guest_exit(128 + sig, b); }
uint32_t iosrt_mach_task_self_ = 0x103;
unsigned long iosrt_vm_page_size = 16384;
int iosrt_pthread_attr_setdetachstate(pthread_attr_t *a, int st) { return pthread_attr_setdetachstate(a, st == 2 ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE); }
int iosrt_pthread_setschedparam(pthread_t t, int pol, const void *p) { (void)t; (void)pol; (void)p; return 0; }
_Noreturn void iosrt_unwind_resume(void *exc) { (void)exc; rt_guest_exit(72, "exception unwinding (_Unwind_Resume) is not supported"); }
void iosrt_cxx_throw(void *obj, void *tinfo, void (*dest)(void *)) __attribute__((weak));
_Noreturn void iosrt_cxa_throw(void *exc, void *type, void (*dtor)(void *)) {
    if (iosrt_cxx_throw) iosrt_cxx_throw(exc, type, dtor);      /* richer message when C++ support is linked */
    rt_guest_exit(72, "uncaught C++ exception (exception unwinding is not supported yet)");
}
_Noreturn void iosrt_cxa_rethrow(void) { rt_guest_exit(72, "C++ rethrow (exception unwinding is not supported yet)"); }
_Noreturn void iosrt_terminate(void) { rt_guest_exit(134, "std::terminate called"); }

/* ------------------------------------------- libdispatch: more of the API */
struct qspec { struct rt_queue *q; const void *key; void *val; };
static struct qspec *qspecs; static size_t nqspecs, capqspecs;
static pthread_mutex_t qspec_lock = PTHREAD_MUTEX_INITIALIZER;
void iosrt_dispatch_queue_set_specific(struct rt_queue *q, const void *key, void *val, void (*dtor)(void *)) {
    (void)dtor;
    pthread_mutex_lock(&qspec_lock);
    for (size_t i = 0; i < nqspecs; i++) if (qspecs[i].q == q && qspecs[i].key == key) { qspecs[i].val = val; pthread_mutex_unlock(&qspec_lock); return; }
    if (nqspecs == capqspecs) { capqspecs = capqspecs ? capqspecs * 2 : 16; qspecs = realloc(qspecs, capqspecs * sizeof *qspecs); }
    qspecs[nqspecs++] = (struct qspec){ q, key, val };
    pthread_mutex_unlock(&qspec_lock);
}
void *iosrt_dispatch_queue_get_specific(struct rt_queue *q, const void *key) {
    void *r = NULL;
    pthread_mutex_lock(&qspec_lock);
    for (size_t i = 0; i < nqspecs; i++) if (qspecs[i].q == q && qspecs[i].key == key) { r = qspecs[i].val; break; }
    pthread_mutex_unlock(&qspec_lock);
    return r;
}
void *iosrt_dispatch_get_specific(const void *key) {
    struct rt_queue *q = cur_queue ? cur_queue : (rt_is_guest_main_thread() ? &iosrt_dispatch_main_q : NULL);
    return q ? iosrt_dispatch_queue_get_specific(q, key) : NULL;
}
void iosrt_dispatch_barrier_async(struct rt_queue *q, void *block) { iosrt_dispatch_async(q, block); }
void iosrt_dispatch_barrier_sync(struct rt_queue *q, void *block) { iosrt_dispatch_sync(q, block); }
void iosrt_dispatch_suspend(void *o) { (void)o; }
void iosrt_dispatch_resume_noop(void *o) { (void)o; }
void iosrt_dispatch_set_target_queue(void *o, void *q) { (void)o; (void)q; }
void *iosrt_dispatch_get_current_queue(void) { return cur_queue ? (void *)cur_queue : (void *)&iosrt_dispatch_main_q; }
void *iosrt_dispatch_queue_attr_make_with_qos_class(void *a, int qos, int prio) { (void)qos; (void)prio; return a; }
void iosrt_dispatch_apply(size_t n, void *q, void *block) {
    (void)q;
    for (size_t i = 0; i < n; i++) ((void (*)(void *, size_t))((struct rt_block *)block)->invoke)(block, i);
}
/* groups */
struct rt_group { pthread_mutex_t m; pthread_cond_t c; long n; void *notify_block; struct rt_queue *notify_q; };
void *iosrt_dispatch_group_create(void) { struct rt_group *g = rt_xcalloc(1, sizeof *g); pthread_mutex_init(&g->m, NULL); pthread_cond_init(&g->c, NULL); return g; }
void iosrt_dispatch_group_enter(struct rt_group *g) { pthread_mutex_lock(&g->m); g->n++; pthread_mutex_unlock(&g->m); }
void iosrt_dispatch_group_leave(struct rt_group *g) {
    pthread_mutex_lock(&g->m);
    void *nb = NULL; struct rt_queue *nq = NULL;
    if (--g->n <= 0) { g->n = 0; pthread_cond_broadcast(&g->c); nb = g->notify_block; nq = g->notify_q; g->notify_block = NULL; }
    pthread_mutex_unlock(&g->m);
    if (nb) { iosrt_dispatch_async(nq, nb); _Block_release(nb); }
}
long iosrt_dispatch_group_wait(struct rt_group *g, uint64_t timeout) {
    (void)timeout;
    pthread_mutex_lock(&g->m); while (g->n > 0) pthread_cond_wait(&g->c, &g->m); pthread_mutex_unlock(&g->m);
    return 0;
}
void iosrt_dispatch_group_notify(struct rt_group *g, struct rt_queue *q, void *block) {
    pthread_mutex_lock(&g->m);
    if (g->n == 0) { pthread_mutex_unlock(&g->m); iosrt_dispatch_async(q, block); return; }
    g->notify_block = _Block_copy(block); g->notify_q = q;
    pthread_mutex_unlock(&g->m);
}
struct grp_item { struct rt_group *g; void *block; };
static void grp_run(void *p) { struct grp_item *it = p; block_invoke(it->block); _Block_release(it->block); iosrt_dispatch_group_leave(it->g); free(it); }
void iosrt_dispatch_group_async(struct rt_group *g, struct rt_queue *q, void *block) {
    iosrt_dispatch_group_enter(g);
    struct grp_item *it = rt_xmalloc(sizeof *it); it->g = g; it->block = _Block_copy(block);
    iosrt_dispatch_async_f(q, it, grp_run);
}
/* sources: timers and DATA_ADD; others never fire */
struct rt_source { int kind; struct rt_queue *q; void *handler; void *cancel; uint64_t start, interval; volatile int running, cancelled; unsigned long data; pthread_t t; };
int iosrt_dispatch_source_type_timer[4], iosrt_dispatch_source_type_data_add[4], iosrt_dispatch_source_type_data_or[4],
    iosrt_dispatch_source_type_read[4], iosrt_dispatch_source_type_signal[4], iosrt_dispatch_source_type_memorypressure[4];
void *iosrt_dispatch_source_create(void *type, uintptr_t handle, unsigned long mask, struct rt_queue *q) {
    (void)handle; (void)mask;
    struct rt_source *s = rt_xcalloc(1, sizeof *s);
    s->kind = type == iosrt_dispatch_source_type_timer ? 1 : (type == iosrt_dispatch_source_type_data_add || type == iosrt_dispatch_source_type_data_or) ? 2 : 0;
    s->q = q ? q : &iosrt_dispatch_main_q;
    return s;
}
void iosrt_dispatch_source_set_event_handler(struct rt_source *s, void *b) { if (s->handler) _Block_release(s->handler); s->handler = b ? _Block_copy(b) : NULL; }
void iosrt_dispatch_source_set_cancel_handler(struct rt_source *s, void *b) { s->cancel = b ? _Block_copy(b) : NULL; }
void iosrt_dispatch_source_set_timer(struct rt_source *s, uint64_t start, uint64_t interval, uint64_t leeway) { (void)leeway; s->start = start; s->interval = interval; }
static void *timer_thread(void *p) {
    struct rt_source *s = p;
    uint64_t next = s->start == 0 ? iosrt_mach_absolute_time() : s->start;
    while (!s->cancelled) {
        uint64_t now = iosrt_mach_absolute_time();
        if (now < next) { uint64_t d = next - now; if (d > 50000000ULL) d = 50000000ULL; struct timespec ts = { 0, (long)d }; nanosleep(&ts, NULL); continue; }
        if (s->handler && !s->cancelled) iosrt_dispatch_async(s->q, s->handler);
        if (s->interval == 0 || s->interval == ~0ULL) break;
        next += s->interval;
    }
    return NULL;
}
void iosrt_dispatch_source_resume(struct rt_source *s) {
    if (s->running) return;
    s->running = 1;
    if (s->kind == 1) { pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED); pthread_create(&s->t, &a, timer_thread, s); pthread_attr_destroy(&a); }
}
void iosrt_dispatch_resume(void *o) {
    /* sources are the only resumable objects we model */
    struct rt_source *s = o;
    if (s && (s->kind == 1 || s->kind == 2) && !s->running) iosrt_dispatch_source_resume(s);
}
void iosrt_dispatch_source_merge_data(struct rt_source *s, unsigned long v) {
    s->data += v;
    if (s->handler && s->running) iosrt_dispatch_async(s->q, s->handler);
}
unsigned long iosrt_dispatch_source_get_data(struct rt_source *s) { unsigned long d = s->data; s->data = 0; return d; }
void iosrt_dispatch_source_cancel(struct rt_source *s) { s->cancelled = 1; if (s->cancel) iosrt_dispatch_async(s->q, s->cancel); }
long iosrt_dispatch_source_testcancel(struct rt_source *s) { return s->cancelled; }
/* pthread rwlocks with the Darwin static initializer */
#define DARWIN_RWLOCK_SIG_INIT 0x2DA8B3B4L
static void rw_fix(pthread_rwlock_t *l) {
    long sig; memcpy(&sig, l, sizeof sig);
    if (sig != DARWIN_RWLOCK_SIG_INIT) return;
    pthread_mutex_lock(&lazy_lock); memcpy(&sig, l, sizeof sig);
    if (sig == DARWIN_RWLOCK_SIG_INIT) pthread_rwlock_init(l, NULL);
    pthread_mutex_unlock(&lazy_lock);
}
int iosrt_pthread_rwlock_rdlock(pthread_rwlock_t *l) { rw_fix(l); return pthread_rwlock_rdlock(l); }
int iosrt_pthread_rwlock_wrlock(pthread_rwlock_t *l) { rw_fix(l); return pthread_rwlock_wrlock(l); }
int iosrt_pthread_rwlock_tryrdlock(pthread_rwlock_t *l) { rw_fix(l); return pthread_rwlock_tryrdlock(l); }
int iosrt_pthread_rwlock_trywrlock(pthread_rwlock_t *l) { rw_fix(l); return pthread_rwlock_trywrlock(l); }
int iosrt_pthread_rwlock_unlock(pthread_rwlock_t *l) { rw_fix(l); return pthread_rwlock_unlock(l); }
int iosrt_pthread_rwlock_destroy(pthread_rwlock_t *l) { rw_fix(l); return pthread_rwlock_destroy(l); }

static int once_begin2(long *pred) { return once_begin(pred); }
