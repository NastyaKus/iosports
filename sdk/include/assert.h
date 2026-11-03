#pragma once
void __assert_rtn(const char *, const char *, int, const char *) __attribute__((noreturn));
#define assert(e) ((e) ? (void)0 : __assert_rtn(__func__, __FILE__, __LINE__, #e))
