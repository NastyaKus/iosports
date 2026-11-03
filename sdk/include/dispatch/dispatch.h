#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct dispatch_queue_s *dispatch_queue_t;
typedef struct dispatch_semaphore_s *dispatch_semaphore_t;
typedef uint64_t dispatch_time_t;
typedef long dispatch_once_t;
typedef void (^dispatch_block_t)(void);
extern struct dispatch_queue_s _dispatch_main_q;
#define dispatch_get_main_queue() (&_dispatch_main_q)
#define DISPATCH_TIME_NOW (0ull)
#define DISPATCH_TIME_FOREVER (~0ull)
#define NSEC_PER_SEC 1000000000ull
#define NSEC_PER_MSEC 1000000ull
#define DISPATCH_QUEUE_PRIORITY_DEFAULT 0
dispatch_queue_t dispatch_get_global_queue(long, unsigned long);
dispatch_queue_t dispatch_queue_create(const char *, void *);
void dispatch_async(dispatch_queue_t, dispatch_block_t);
void dispatch_sync(dispatch_queue_t, dispatch_block_t);
void dispatch_after(dispatch_time_t, dispatch_queue_t, dispatch_block_t);
dispatch_time_t dispatch_time(dispatch_time_t, int64_t);
void dispatch_once(dispatch_once_t *, dispatch_block_t);
dispatch_semaphore_t dispatch_semaphore_create(long);
long dispatch_semaphore_signal(dispatch_semaphore_t);
long dispatch_semaphore_wait(dispatch_semaphore_t, dispatch_time_t);
void dispatch_main(void) __attribute__((noreturn));
#ifdef __cplusplus
}
#endif
