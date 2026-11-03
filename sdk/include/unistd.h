#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
typedef long ssize_t;
typedef int pid_t;
typedef unsigned int useconds_t;
ssize_t write(int, const void *, size_t);
ssize_t read(int, void *, size_t);
int close(int);
int usleep(useconds_t);
unsigned sleep(unsigned);
pid_t getpid(void);
int access(const char *, int);
int unlink(const char *);
#ifdef __cplusplus
}
#endif
