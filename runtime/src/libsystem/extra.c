/*
 * libSystem additions found by tracing GarageBand: more OSAtomic and the
 * OSQueue LIFO, Mach and named POSIX semaphores, thread policies, statfs with
 * Darwin's struct layout, and a few libc/libm functions whose Darwin and
 * host signatures differ.
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <time.h>
#include <sched.h>
#include <sys/statfs.h>
#include <sys/time.h>
#include <unistd.h>

int *iosrt___error(void);
#define SET_ERRNO(e) (*iosrt___error() = (e))

/* ------------------------------------------------------------ OSAtomic */
int64_t iosrt_OSAtomicAdd64Barrier(int64_t v, volatile int64_t *p) { return __atomic_add_fetch(p, v, __ATOMIC_SEQ_CST); }
int64_t iosrt_OSAtomicIncrement64Barrier(volatile int64_t *p) { return __atomic_add_fetch(p, 1, __ATOMIC_SEQ_CST); }
int64_t iosrt_OSAtomicDecrement64Barrier(volatile int64_t *p) { return __atomic_sub_fetch(p, 1, __ATOMIC_SEQ_CST); }
int32_t iosrt_OSAtomicOr32OrigBarrier(uint32_t m, volatile uint32_t *p) { return (int32_t)__atomic_fetch_or(p, m, __ATOMIC_SEQ_CST); }
int32_t iosrt_OSAtomicAnd32OrigBarrier(uint32_t m, volatile uint32_t *p) { return (int32_t)__atomic_fetch_and(p, m, __ATOMIC_SEQ_CST); }
int32_t iosrt_OSAtomicOr32Barrier(uint32_t m, volatile uint32_t *p) { return (int32_t)__atomic_or_fetch(p, m, __ATOMIC_SEQ_CST); }
int32_t iosrt_OSAtomicAnd32Barrier(uint32_t m, volatile uint32_t *p) { return (int32_t)__atomic_and_fetch(p, m, __ATOMIC_SEQ_CST); }
/* bit n counts from the most significant bit of the first byte */
long iosrt_OSAtomicTestAndSetBarrier(uint32_t n, volatile void *a) {
    volatile uint8_t *b = (volatile uint8_t *)a + (n >> 3); uint8_t m = (uint8_t)(0x80 >> (n & 7));
    return (__atomic_fetch_or(b, m, __ATOMIC_SEQ_CST) & m) != 0;
}
long iosrt_OSAtomicTestAndClearBarrier(uint32_t n, volatile void *a) {
    volatile uint8_t *b = (volatile uint8_t *)a + (n >> 3); uint8_t m = (uint8_t)(0x80 >> (n & 7));
    return (__atomic_fetch_and(b, (uint8_t)~m, __ATOMIC_SEQ_CST) & m) != 0;
}
/* OSQueueHead { void *opaque1; long opaque2; }: a LIFO of elements linked at `offset` */
static pthread_mutex_t q_lock = PTHREAD_MUTEX_INITIALIZER;
void iosrt_OSAtomicEnqueue(void **head, void *elem, size_t offset) {
    pthread_mutex_lock(&q_lock);
    *(void **)((char *)elem + offset) = head[0]; head[0] = elem;
    pthread_mutex_unlock(&q_lock);
}
void *iosrt_OSAtomicDequeue(void **head, size_t offset) {
    pthread_mutex_lock(&q_lock);
    void *e = head[0];
    if (e) head[0] = *(void **)((char *)e + offset);
    pthread_mutex_unlock(&q_lock);
    return e;
}

/* ----------------------------------------------------------- semaphores */
/* Mach semaphores are port names (32-bit); named POSIX semaphores are pointers. Both
 * are counting semaphores on a mutex + condition variable. */
struct rt_sem { pthread_mutex_t m; pthread_cond_t c; long count; long waiters; char *name; unsigned refs; };
static struct rt_sem *sem_new(long v) {
    struct rt_sem *s = rt_xcalloc(1, sizeof *s);
    pthread_mutex_init(&s->m, NULL); pthread_cond_init(&s->c, NULL); s->count = v; s->refs = 1;
    return s;
}
/* returns 0, or ETIMEDOUT when abs (CLOCK_REALTIME) passed */
static int sem_down(struct rt_sem *s, const struct timespec *abs) {
    int r = 0;
    pthread_mutex_lock(&s->m);
    s->waiters++;
    while (s->count <= 0 && r == 0) r = abs ? pthread_cond_timedwait(&s->c, &s->m, abs) : pthread_cond_wait(&s->c, &s->m);
    s->waiters--;
    if (s->count > 0) { s->count--; r = 0; }
    pthread_mutex_unlock(&s->m);
    return r;
}
static void sem_up(struct rt_sem *s, int all) {
    pthread_mutex_lock(&s->m);
    if (all) { if (s->waiters) { s->count += s->waiters; pthread_cond_broadcast(&s->c); } }
    else { s->count++; pthread_cond_signal(&s->c); }
    pthread_mutex_unlock(&s->m);
}
#define MAX_MACH_SEMS 4096
static struct rt_sem *mach_sems[MAX_MACH_SEMS];
static pthread_mutex_t mach_sems_lock = PTHREAD_MUTEX_INITIALIZER;
static struct rt_sem *mach_sem(uint32_t name) { return name >= 0x1000 && name < 0x1000 + MAX_MACH_SEMS ? mach_sems[name - 0x1000] : NULL; }
enum { KERN_SUCCESS = 0, KERN_INVALID_ARGUMENT = 4, KERN_RESOURCE_SHORTAGE = 6, KERN_OPERATION_TIMED_OUT = 49 };
int iosrt_semaphore_create(uint32_t task, uint32_t *out, int policy, int value) {
    (void)task; (void)policy;
    if (!out || value < 0) return KERN_INVALID_ARGUMENT;
    pthread_mutex_lock(&mach_sems_lock);
    for (int i = 0; i < MAX_MACH_SEMS; i++)
        if (!mach_sems[i]) { mach_sems[i] = sem_new(value); *out = 0x1000u + (uint32_t)i; pthread_mutex_unlock(&mach_sems_lock); return KERN_SUCCESS; }
    pthread_mutex_unlock(&mach_sems_lock);
    return KERN_RESOURCE_SHORTAGE;
}
int iosrt_semaphore_destroy(uint32_t task, uint32_t sem) {
    (void)task;
    pthread_mutex_lock(&mach_sems_lock);
    struct rt_sem *s = mach_sem(sem);
    if (s) mach_sems[sem - 0x1000] = NULL;   /* the object is kept: a waiter may still hold it */
    pthread_mutex_unlock(&mach_sems_lock);
    return s ? KERN_SUCCESS : KERN_INVALID_ARGUMENT;
}
int iosrt_semaphore_signal(uint32_t sem) { struct rt_sem *s = mach_sem(sem); if (!s) return KERN_INVALID_ARGUMENT; sem_up(s, 0); return KERN_SUCCESS; }
int iosrt_semaphore_signal_all(uint32_t sem) { struct rt_sem *s = mach_sem(sem); if (!s) return KERN_INVALID_ARGUMENT; sem_up(s, 1); return KERN_SUCCESS; }
int iosrt_semaphore_wait(uint32_t sem) { struct rt_sem *s = mach_sem(sem); if (!s) return KERN_INVALID_ARGUMENT; sem_down(s, NULL); return KERN_SUCCESS; }
/* mach_timespec_t { unsigned int tv_sec; clock_res_t tv_nsec; } passed by value */
int iosrt_semaphore_timedwait(uint32_t sem, uint64_t wait) {
    struct rt_sem *s = mach_sem(sem); if (!s) return KERN_INVALID_ARGUMENT;
    uint32_t sec = (uint32_t)wait; int32_t nsec = (int32_t)(wait >> 32);
    struct timespec abs; clock_gettime(CLOCK_REALTIME, &abs);
    abs.tv_sec += sec; abs.tv_nsec += nsec;
    if (abs.tv_nsec >= 1000000000L) { abs.tv_sec++; abs.tv_nsec -= 1000000000L; }
    return sem_down(s, &abs) ? KERN_OPERATION_TIMED_OUT : KERN_SUCCESS;
}
/* named POSIX semaphores (process-local) */
static struct rt_sem *named[64]; static int nnamed;
static pthread_mutex_t named_lock = PTHREAD_MUTEX_INITIALIZER;
void *iosrt_sem_open(const char *name, int oflag, ...) {
    /* the O_CREAT arguments (mode, value) are Darwin varargs on the stack and are not read:
     * a new semaphore starts at 0, which is what apps creating one for signalling pass */
    (void)oflag;
    pthread_mutex_lock(&named_lock);
    for (int i = 0; i < nnamed; i++) if (!strcmp(named[i]->name, name)) { named[i]->refs++; pthread_mutex_unlock(&named_lock); return named[i]; }
    struct rt_sem *s = NULL;
    if (nnamed < 64) { s = sem_new(0); s->name = rt_xstrdup(name); named[nnamed++] = s; }
    pthread_mutex_unlock(&named_lock);
    if (!s) { SET_ERRNO(23); return (void *)-1; }   /* ENFILE; SEM_FAILED */
    return s;
}
int iosrt_sem_close(void *s) { (void)s; return 0; }
int iosrt_sem_post(void *s) { if (!s) { SET_ERRNO(22); return -1; } sem_up(s, 0); return 0; }
int iosrt_sem_wait(void *s) { if (!s) { SET_ERRNO(22); return -1; } sem_down(s, NULL); return 0; }
int iosrt_sem_trywait(void *s) {
    struct rt_sem *x = s; if (!x) { SET_ERRNO(22); return -1; }
    pthread_mutex_lock(&x->m); int ok = x->count > 0; if (ok) x->count--; pthread_mutex_unlock(&x->m);
    if (!ok) { SET_ERRNO(35); return -1; }     /* EAGAIN */
    return 0;
}

/* -------------------------------------------------------------- threads */
int iosrt_thread_policy(uint32_t thread, int policy, void *base, unsigned count, int set_limit) { (void)thread; (void)policy; (void)base; (void)count; (void)set_limit; return KERN_SUCCESS; }
int iosrt_thread_policy_get(uint32_t thread, int flavor, int *info, unsigned *count, int *get_default) {
    (void)thread; (void)flavor;
    if (info && count) memset(info, 0, *count * sizeof(int));
    if (get_default) *get_default = 1;
    return KERN_SUCCESS;
}
/* Darwin: SCHED_OTHER 1; struct sched_param { int sched_priority; char opaque[4]; } */
int iosrt_pthread_getschedparam(pthread_t t, int *policy, int *param) {
    (void)t;
    if (policy) *policy = 1;
    if (param) { param[0] = 31; param[1] = 0; }
    return 0;
}
int iosrt_pthread_cancel(pthread_t t) { (void)t; return 0; }
void *iosrt_pthread_from_mach_thread_np(uint32_t port) { (void)port; return NULL; }
int iosrt_pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *t);
int iosrt_pthread_cond_timedwait_relative_np(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *rel) {
    struct timespec abs; clock_gettime(CLOCK_REALTIME, &abs);
    if (rel) { abs.tv_sec += rel->tv_sec; abs.tv_nsec += rel->tv_nsec; if (abs.tv_nsec >= 1000000000L) { abs.tv_sec++; abs.tv_nsec -= 1000000000L; } }
    return iosrt_pthread_cond_timedwait(c, m, &abs);
}
int iosrt_pthread_getname_np(pthread_t t, char *buf, size_t n) { return pthread_getname_np(t, buf, n); }

/* --------------------------------------------------------------- statfs */
struct darwin_statfs {
    uint32_t f_bsize; int32_t f_iosize;
    uint64_t f_blocks, f_bfree, f_bavail, f_files, f_ffree;
    int32_t f_fsid[2]; uint32_t f_owner, f_type, f_flags, f_fssubtype;
    char f_fstypename[16], f_mntonname[1024], f_mntfromname[1024];
    uint32_t f_reserved[8];
};
static void to_darwin_statfs(const struct statfs *h, struct darwin_statfs *d, const char *path) {
    memset(d, 0, sizeof *d);
    d->f_bsize = (uint32_t)h->f_bsize; d->f_iosize = (int32_t)h->f_bsize;
    d->f_blocks = h->f_blocks; d->f_bfree = h->f_bfree; d->f_bavail = h->f_bavail;
    d->f_files = h->f_files; d->f_ffree = h->f_ffree;
    d->f_type = 17; strcpy(d->f_fstypename, "apfs");
    snprintf(d->f_mntonname, sizeof d->f_mntonname, "%s", path && path[0] == '/' ? "/private/var" : "/");
    strcpy(d->f_mntfromname, "/dev/disk0s1s2");
}
int iosrt_statfs(const char *path, struct darwin_statfs *out) {
    struct statfs h;
    if (statfs(path, &h) != 0) { SET_ERRNO(errno == ENOENT ? 2 : 5); return -1; }
    to_darwin_statfs(&h, out, path);
    return 0;
}
int iosrt_fstatfs(int fd, struct darwin_statfs *out) {
    struct statfs h;
    if (fstatfs(fd, &h) != 0) { SET_ERRNO(9); return -1; }
    to_darwin_statfs(&h, out, "/");
    return 0;
}

/* --------------------------------------------------------- libc / libm */
/* long double is double on Darwin arm64 */
double iosrt_logl(double x) { return log(x); }
double iosrt_logbl(double x) { return logb(x); }
double iosrt_scalbnl(double x, int n) { return scalbn(x, n); }
double iosrt___cospi(double x) { return cos(M_PI * x); }
double iosrt___sinpi(double x) { return sin(M_PI * x); }
float iosrt___cospif(float x) { return cosf((float)M_PI * x); }
float iosrt___sinpif(float x) { return sinf((float)M_PI * x); }
int iosrt___isnand(double x) { return isnan(x); }
int iosrt___isnanf(float x) { return isnan(x); }
int iosrt___isinfd(double x) { return isinf(x) ? (x > 0 ? 1 : -1) : 0; }
int iosrt_getpagesize(void) { return 16384; }
int iosrt_issetugid(void) { return 0; }
int iosrt_getdtablesize(void) { return (int)sysconf(_SC_OPEN_MAX); }
int iosrt_madvise(void *a, size_t n, int advice) { (void)a; (void)n; (void)advice; return 0; }
int iosrt_kdebug_signpost(uint32_t code, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) { (void)code; (void)a; (void)b; (void)c; (void)d; return 0; }
int iosrt___mb_cur_max = 1;
/* setlocale: the C locale stays in effect; queries answer "C" */
char *iosrt_setlocale(int cat, const char *loc) { (void)cat; (void)loc; static char c[] = "C"; return c; }
void iosrt_srandomdev(void) { srandom((unsigned)time(NULL) ^ (unsigned)getpid()); }
void iosrt_arc4random_stir(void) {}
/* 128-bit division from the compiler runtime */
__int128 iosrt___divti3(__int128 a, __int128 b) { return b ? a / b : 0; }
