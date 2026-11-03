#pragma once
#ifdef __cplusplus
extern "C" {
#endif
typedef unsigned int __uint32_t;
#define _CTYPE_A 0x00000100L
#define _CTYPE_C 0x00000200L
#define _CTYPE_D 0x00000400L
#define _CTYPE_G 0x00000800L
#define _CTYPE_L 0x00001000L
#define _CTYPE_P 0x00002000L
#define _CTYPE_S 0x00004000L
#define _CTYPE_U 0x00008000L
#define _CTYPE_X 0x00010000L
#define _CTYPE_B 0x00020000L
#define _CTYPE_R 0x00040000L
int isalnum(int); int isalpha(int); int isblank(int); int iscntrl(int); int isdigit(int); int isgraph(int);
int islower(int); int isprint(int); int ispunct(int); int isspace(int); int isupper(int); int isxdigit(int);
int tolower(int); int toupper(int);
static inline int isascii(int c) { return (c & ~0x7f) == 0; }
#ifdef __cplusplus
}
#endif
