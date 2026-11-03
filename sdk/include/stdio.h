/* iosrt mini-SDK: minimal stdio declarations (Darwin ABI). */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#include <stdarg.h>
typedef struct __sFILE FILE;
extern FILE *__stdinp, *__stdoutp, *__stderrp;
#define stdin __stdinp
#define stdout __stdoutp
#define stderr __stderrp
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
int printf(const char *, ...) __attribute__((format(printf, 1, 2)));
int fprintf(FILE *, const char *, ...) __attribute__((format(printf, 2, 3)));
int sprintf(char *, const char *, ...);
int snprintf(char *, size_t, const char *, ...) __attribute__((format(printf, 3, 4)));
int asprintf(char **, const char *, ...);
int vprintf(const char *, va_list);
int vfprintf(FILE *, const char *, va_list);
int vsnprintf(char *, size_t, const char *, va_list);
int sscanf(const char *, const char *, ...);
int puts(const char *);
int putchar(int);
int fputs(const char *, FILE *);
int fputc(int, FILE *);
size_t fwrite(const void *, size_t, size_t, FILE *);
size_t fread(void *, size_t, size_t, FILE *);
FILE *fopen(const char *, const char *);
int fclose(FILE *);
int fflush(FILE *);
int fseek(FILE *, long, int);
long ftell(FILE *);
char *fgets(char *, int, FILE *);
void perror(const char *);
#ifdef __cplusplus
}
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef long fpos_t;
#define BUFSIZ 1024
#define FILENAME_MAX 1024
#define FOPEN_MAX 20
#define L_tmpnam 1024
#define TMP_MAX 308915776
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
int vsprintf(char *, const char *, va_list);
int vscanf(const char *, va_list); int vsscanf(const char *, const char *, va_list); int vfscanf(FILE *, const char *, va_list);
int scanf(const char *, ...); int fscanf(FILE *, const char *, ...);
int getc(FILE *); int getchar(void); int putc(int, FILE *); int ungetc(int, FILE *); int fgetc(FILE *);
char *gets(char *);
int fgetpos(FILE *, fpos_t *); int fsetpos(FILE *, const fpos_t *);
void rewind(FILE *); void clearerr(FILE *); int feof(FILE *); int ferror(FILE *);
FILE *freopen(const char *, const char *, FILE *); FILE *tmpfile(void); char *tmpnam(char *);
int remove(const char *); int rename(const char *, const char *);
void setbuf(FILE *, char *); int setvbuf(FILE *, char *, int, size_t);
#ifdef __cplusplus
}
#endif
