#pragma once
#ifdef __cplusplus
extern "C" {
#endif
/* Darwin arm64 layouts */
typedef struct _opaque_pthread_t *pthread_t;
typedef struct { long __sig; char __opaque[56]; } pthread_mutex_t;
typedef struct { long __sig; char __opaque[8]; } pthread_mutexattr_t;
typedef struct { long __sig; char __opaque[40]; } pthread_cond_t;
typedef struct { long __sig; char __opaque[56]; } pthread_attr_t;
typedef struct { long __sig; char __opaque[8]; } pthread_once_t;
typedef unsigned long pthread_key_t;
typedef struct { long __sig; char __opaque[8]; } pthread_condattr_t;
typedef struct { long __sig; char __opaque[192]; } pthread_rwlock_t;
typedef struct { long __sig; char __opaque[16]; } pthread_rwlockattr_t;
#include <sched.h>
#include <time.h>
#define PTHREAD_CREATE_JOINABLE 1
#define PTHREAD_CREATE_DETACHED 2
#define PTHREAD_MUTEX_NORMAL 0
#define PTHREAD_MUTEX_ERRORCHECK 1
#define PTHREAD_MUTEX_DEFAULT 0
#define PTHREAD_RWLOCK_INITIALIZER {0x2DA8B3B4, {0}}
#define PTHREAD_MUTEX_INITIALIZER {0x32AAABA7, {0}}
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER {0x32AAABA1, {0}}
#define PTHREAD_COND_INITIALIZER {0x3CB0B1BB, {0}}
#define PTHREAD_ONCE_INIT {0x30B1BCBA, {0}}
#define PTHREAD_MUTEX_RECURSIVE 2
int pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
int pthread_join(pthread_t, void **);
pthread_t pthread_self(void);
int pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int pthread_mutex_lock(pthread_mutex_t *);
int pthread_mutex_unlock(pthread_mutex_t *);
int pthread_mutexattr_init(pthread_mutexattr_t *);
int pthread_mutexattr_settype(pthread_mutexattr_t *, int);
int pthread_cond_wait(pthread_cond_t *, pthread_mutex_t *);
int pthread_cond_signal(pthread_cond_t *);
int pthread_cond_broadcast(pthread_cond_t *);
int pthread_once(pthread_once_t *, void (*)(void));
int pthread_main_np(void);
int pthread_detach(pthread_t);
int pthread_equal(pthread_t, pthread_t);
void pthread_exit(void *) __attribute__((noreturn));
int pthread_key_create(pthread_key_t *, void (*)(void *));
int pthread_key_delete(pthread_key_t);
void *pthread_getspecific(pthread_key_t);
int pthread_setspecific(pthread_key_t, const void *);
int pthread_mutex_trylock(pthread_mutex_t *);
int pthread_mutex_destroy(pthread_mutex_t *);
int pthread_mutexattr_destroy(pthread_mutexattr_t *);
int pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
int pthread_cond_destroy(pthread_cond_t *);
int pthread_cond_timedwait(pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
int pthread_attr_init(pthread_attr_t *);
int pthread_attr_destroy(pthread_attr_t *);
int pthread_attr_setdetachstate(pthread_attr_t *, int);
int pthread_attr_setstacksize(pthread_attr_t *, unsigned long);
int pthread_setname_np(const char *);
int pthread_rwlock_init(pthread_rwlock_t *, const pthread_rwlockattr_t *);
int pthread_rwlock_destroy(pthread_rwlock_t *);
int pthread_rwlock_rdlock(pthread_rwlock_t *);
int pthread_rwlock_wrlock(pthread_rwlock_t *);
int pthread_rwlock_unlock(pthread_rwlock_t *);
int pthread_rwlock_tryrdlock(pthread_rwlock_t *);
int pthread_rwlock_trywrlock(pthread_rwlock_t *);
#ifdef __cplusplus
}
#endif
