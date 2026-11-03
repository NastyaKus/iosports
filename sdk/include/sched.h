#pragma once
#ifdef __cplusplus
extern "C" {
#endif
struct sched_param { int sched_priority; char __opaque[4]; };
int sched_yield(void);
int sched_get_priority_min(int);
int sched_get_priority_max(int);
#ifdef __cplusplus
}
#endif
