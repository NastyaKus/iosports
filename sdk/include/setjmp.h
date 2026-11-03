/* iosrt mini-SDK: <setjmp.h> with Darwin arm64 buffer sizes */
#ifndef _SETJMP_H
#define _SETJMP_H
#define _JBLEN 48
typedef int jmp_buf[_JBLEN];
typedef int sigjmp_buf[_JBLEN + 1];
#ifdef __cplusplus
extern "C" {
#endif
int setjmp(jmp_buf env) __attribute__((returns_twice));
void longjmp(jmp_buf env, int val) __attribute__((noreturn));
int _setjmp(jmp_buf env) __attribute__((returns_twice));
void _longjmp(jmp_buf env, int val) __attribute__((noreturn));
int sigsetjmp(sigjmp_buf env, int savemask) __attribute__((returns_twice));
void siglongjmp(sigjmp_buf env, int val) __attribute__((noreturn));
#ifdef __cplusplus
}
#endif
#endif
