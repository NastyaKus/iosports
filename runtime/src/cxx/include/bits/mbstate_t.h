/* iosrt: Darwin's mbstate_t for the runtime's libc++ build (shadows bionic's
 * <bits/mbstate_t.h>, which is an 8-byte struct). Apple's libc++ ABI uses
 * the 128-byte union __mbstate_t: fpos<mbstate_t> (std::streampos, returned
 * by value from tellg/seekoff) has its size, and symbols mangle it as
 * "11__mbstate_t". bionic's conversion functions only use the first bytes. */
#pragma once
#include <sys/cdefs.h>
typedef union {
    char __mbstate8[128];
    long long _mbstateL;
} __mbstate_t;
typedef __mbstate_t mbstate_t;
