#pragma once
#include <stddef.h>
#include <stdint.h>
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
size_t malloc_size(const void *);
void exit(int) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));
int atexit(void (*)(void));
int atoi(const char *);
long strtol(const char *, char **, int);
double strtod(const char *, char **);
double atof(const char *);
int abs(int);
long labs(long);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
void *bsearch(const void *, const void *, size_t, size_t, int (*)(const void *, const void *));
char *getenv(const char *);
int setenv(const char *, const char *, int);
int rand(void);
void srand(unsigned);
uint32_t arc4random(void);
uint32_t arc4random_uniform(uint32_t);
