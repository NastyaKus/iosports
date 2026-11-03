/*
 * printf-family formatter that reads arguments from a Darwin variadic area
 * (8-byte stack slots). Supports %@ (Objective-C description) when objc != 0.
 */
#include "../internal.h"
#include <string.h>
#include <stdlib.h>

static void put_utf8(rt_sb *out, uint32_t c) {
    char b[4]; int n;
    if (c < 0x80) { b[0] = (char)c; n = 1; }
    else if (c < 0x800) { b[0] = (char)(0xc0 | c >> 6); b[1] = (char)(0x80 | (c & 0x3f)); n = 2; }
    else if (c < 0x10000) { b[0] = (char)(0xe0 | c >> 12); b[1] = (char)(0x80 | ((c >> 6) & 0x3f)); b[2] = (char)(0x80 | (c & 0x3f)); n = 3; }
    else { b[0] = (char)(0xf0 | c >> 18); b[1] = (char)(0x80 | ((c >> 12) & 0x3f)); b[2] = (char)(0x80 | ((c >> 6) & 0x3f)); b[3] = (char)(0x80 | (c & 0x3f)); n = 4; }
    rt_sb_append(out, b, (size_t)n);
}

static void pad_str(rt_sb *out, const char *s, size_t n, int width, int left) {
    int padn = width > (int)n ? width - (int)n : 0;
    if (!left) for (int i = 0; i < padn; i++) rt_sb_putc(out, ' ');
    rt_sb_append(out, s, n);
    if (left) for (int i = 0; i < padn; i++) rt_sb_putc(out, ' ');
}

void rt_format(rt_sb *out, const char *fmt, struct rt_va *va, int objc) {
    const char *p = fmt;
    while (*p) {
        if (*p != '%') {
            const char *q = strchr(p, '%');
            size_t n = q ? (size_t)(q - p) : strlen(p);
            rt_sb_append(out, p, n);
            p += n;
            continue;
        }
        const char *spec_start = p++;
        if (*p == '%') { rt_sb_putc(out, '%'); p++; continue; }
        /* positional arguments (%1$@) are not supported: skip the n$ part */
        const char *d = p; while (*d >= '0' && *d <= '9') d++;
        if (*d == '$') p = d + 1;
        char flags[8]; int nf = 0;
        while (strchr("-+ #0'", *p) && *p) { if (nf < 7 && *p != '\'') flags[nf++] = *p; p++; }
        flags[nf] = 0;
        int width = -1, prec = -1;
        if (*p == '*') { width = (int)(int64_t)rt_va_u64(va); p++; }
        else if (*p >= '0' && *p <= '9') { width = (int)strtol(p, (char **)&p, 10); }
        if (*p == '.') {
            p++;
            if (*p == '*') { prec = (int)(int64_t)rt_va_u64(va); p++; }
            else prec = (int)strtol(p, (char **)&p, 10);
        }
        int lng = 0; /* 0 int, 1 long/ll/q/z/j/t, -1 short, -2 char */
        for (;;) {
            if (*p == 'l' || *p == 'q' || *p == 'z' || *p == 'j' || *p == 't') { lng = 1; p++; }
            else if (*p == 'h') { lng = lng == -1 ? -2 : -1; p++; }
            else if (*p == 'L') { p++; }
            else break;
        }
        char conv = *p ? *p++ : 0;
        int left = strchr(flags, '-') != NULL;
        char spec[64];
        int sl = 0;
        spec[sl++] = '%';
        for (int i = 0; flags[i] && sl < 40; i++) spec[sl++] = flags[i];
        if (width >= 0) sl += snprintf(spec + sl, sizeof spec - (size_t)sl, "%d", width);
        if (prec >= 0) sl += snprintf(spec + sl, sizeof spec - (size_t)sl, ".%d", prec);
        char tmp[512];
        switch (conv) {
        case 'd': case 'i': {
            uint64_t raw = rt_va_u64(va);
            long long v = lng == 1 ? (long long)raw : lng == -1 ? (short)raw : lng == -2 ? (signed char)raw : (int)raw;
            snprintf(spec + sl, sizeof spec - (size_t)sl, "lld");
            snprintf(tmp, sizeof tmp, spec, v); rt_sb_puts(out, tmp);
            break;
        }
        case 'u': case 'o': case 'x': case 'X': case 'U': case 'O': {
            uint64_t raw = rt_va_u64(va);
            unsigned long long v = lng == 1 || conv == 'U' || conv == 'O' ? raw : lng == -1 ? (unsigned short)raw : lng == -2 ? (unsigned char)raw : (unsigned)raw;
            char c = conv == 'U' ? 'u' : conv == 'O' ? 'o' : conv;
            snprintf(spec + sl, sizeof spec - (size_t)sl, "ll%c", c);
            snprintf(tmp, sizeof tmp, spec, v); rt_sb_puts(out, tmp);
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': {
            double v = rt_va_dbl(va);
            snprintf(spec + sl, sizeof spec - (size_t)sl, "%c", conv);
            snprintf(tmp, sizeof tmp, spec, v); rt_sb_puts(out, tmp);
            break;
        }
        case 'c': {
            char ch = (char)rt_va_u64(va);
            pad_str(out, &ch, 1, width, left);
            break;
        }
        case 'C': {
            uint32_t ch = (uint32_t)rt_va_u64(va) & 0xffff;
            put_utf8(out, ch);
            break;
        }
        case 's': {
            const char *s = (const char *)(uintptr_t)rt_va_u64(va);
            if (!s) s = "(null)";
            size_t n = strlen(s);
            if (prec >= 0 && (size_t)prec < n) n = (size_t)prec;
            pad_str(out, s, n, width, left);
            break;
        }
        case 'S': {
            const uint16_t *s = (const uint16_t *)(uintptr_t)rt_va_u64(va);
            if (!s) { rt_sb_puts(out, "(null)"); break; }
            for (; *s; s++) put_utf8(out, *s);
            break;
        }
        case 'p': {
            uint64_t v = rt_va_u64(va);
            snprintf(tmp, sizeof tmp, "0x%llx", (unsigned long long)v);
            pad_str(out, tmp, strlen(tmp), width, left);
            break;
        }
        case '@': {
            id o = (id)(uintptr_t)rt_va_u64(va);
            if (!objc) { rt_sb_puts(out, "%@"); break; }
            char *s = rt_describe(o);
            size_t n = strlen(s);
            if (prec >= 0 && (size_t)prec < n) n = (size_t)prec;
            pad_str(out, s, n, width, left);
            free(s);
            break;
        }
        case 'n': (void)rt_va_u64(va); break;          /* ignored for safety */
        case 0: rt_sb_puts(out, spec_start); return;
        default:
            rt_sb_append(out, spec_start, (size_t)(p - spec_start));
            break;
        }
    }
}
