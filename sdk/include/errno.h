#pragma once
#ifdef __cplusplus
extern "C" {
#endif
int *__error(void);
#define errno (*__error())
#define EPERM 1
#define ENOENT 2
#define EINTR 4
#define EIO 5
#define EBADF 9
#define EAGAIN 35
#define ENOMEM 12
#define EACCES 13
#define EEXIST 17
#define EINVAL 22
#define ERANGE 34
#define EDOM 33
#define EILSEQ 92
#define ENOSYS 78
#define ETIMEDOUT 60
#define EDEADLK 11
#define EBUSY 16
#define ENOTSUP 45
#define EOVERFLOW 84
#ifdef __cplusplus
}
#endif
