/*
 * Foundation HLE, part 5: locale/time-zone/calendar/formatters, JSON and
 * property-list serialization, keyed archiving (minimal), NSScanner,
 * NSSortDescriptor, NSAttributedString, NSFileHandle, NSOrderedSet and
 * assorted methods on existing classes that real apps call at startup.
 */
#include "../internal.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#define IM(c, s, f) rt_native_method(c, s, (void *)(f), "")
#define CM(c, s, f) rt_native_cmethod(c, s, (void *)(f), "")
#define NEWCLASS(N, SUPER, SZ) rt_native_class(RT_CLASS(N), &iosrt_meta_##N, #N, SUPER, SZ)

RT_DECLARE_CLASS(NSObject);
RT_DECLARE_CLASS(NSString);
RT_DECLARE_CLASS(NSMutableString);
RT_DECLARE_CLASS(NSNumber);
RT_DECLARE_CLASS(NSArray);
RT_DECLARE_CLASS(NSMutableArray);
RT_DECLARE_CLASS(NSDictionary);
RT_DECLARE_CLASS(NSMutableDictionary);
RT_DECLARE_CLASS(NSSet);
RT_DECLARE_CLASS(NSDate);
RT_DECLARE_CLASS(NSData);
RT_DECLARE_CLASS(NSMutableData);
RT_DECLARE_CLASS(NSURL);
RT_DECLARE_CLASS(NSFileManager);
RT_DECLARE_CLASS(NSUserDefaults);
RT_DECLARE_CLASS(NSNull);
RT_DEFINE_CLASS(NSLocale);
RT_DEFINE_CLASS(NSTimeZone);
RT_DEFINE_CLASS(NSCalendar);
RT_DEFINE_CLASS(NSDateComponents);
RT_DEFINE_CLASS(NSFormatter);
RT_DEFINE_CLASS(NSDateFormatter);
RT_DEFINE_CLASS(NSNumberFormatter);
RT_DEFINE_CLASS(NSCoder);
RT_DEFINE_CLASS(NSKeyedArchiver);
RT_DEFINE_CLASS(NSKeyedUnarchiver);
RT_DEFINE_CLASS(NSJSONSerialization);
RT_DEFINE_CLASS(NSPropertyListSerialization);
RT_DEFINE_CLASS(NSScanner);
RT_DEFINE_CLASS(NSSortDescriptor);
RT_DEFINE_CLASS(NSAttributedString);
RT_DEFINE_CLASS(NSMutableAttributedString);
RT_DEFINE_CLASS(NSFileHandle);
RT_DEFINE_CLASS(NSOrderedSet);
RT_DEFINE_CLASS(NSMutableOrderedSet);
RT_DEFINE_CLASS(NSHTTPCookieStorage);
RT_DEFINE_CLASS(NSURLRequest);
RT_DEFINE_CLASS(NSMutableURLRequest);
RT_DEFINE_CLASS(NSURLSession);
RT_DEFINE_CLASS(NSURLSessionConfiguration);
RT_DEFINE_CLASS(NSURLCache);

id rt_dict_new(id *vals, id *keys, size_t n);
void rt_dict_set(id dict, id key, id val);
id rt_data_new(const void *b, size_t n);
const void *rt_data_bytes(id d, size_t *n);
id rt_set_new(id *objs, size_t n);
void rt_soft_error(const char *msg);
const char *rt_home_dir(void);
static const double kRef1970 = 978307200.0;
static id nsnull(void) { return RT_SEND0(id, (id)RT_CLASS(NSNull), "null"); }
static id singleton(Class c, id *slot) { if (!*slot) *slot = rt_alloc_instance(c, 0); return *slot; }
static double date_ref(id d) { return d ? RT_SEND0(double, d, "timeIntervalSinceReferenceDate") : 0; }
static id date_with_ref(double t) { return RT_SEND1(id, (id)RT_CLASS(NSDate), "dateWithTimeIntervalSinceReferenceDate:", double, t); }
static id self_copy(id self, SEL s, void *z) { (void)s; (void)z; return objc_retain(self); }
static rt_bool yes_err(id self, SEL s, id a, id b, id *err) { (void)self; (void)s; (void)a; (void)b; if (err) *err = NULL; return 1; }

/* ============================================================== locale & time */
static id g_locale, g_tz, g_cal;
static id lo_current(Class c, SEL s) { (void)s; return singleton(c, &g_locale); }
static id lo_withId(Class c, SEL s, id ident) { (void)ident; return lo_current(c, s); }
static id lo_initWithId(id self, SEL s, id ident) { (void)s; (void)ident; return self; }
static id lo_ident(id self, SEL s) { (void)self; (void)s; return rt_str("en_US"); }
static id lo_objectForKey(id self, SEL s, id key) {
    (void)self; (void)s; const char *k = key ? rt_str_utf8(key) : "";
    if (strstr(k, "DecimalSeparator")) return rt_str(".");
    if (strstr(k, "GroupingSeparator")) return rt_str(",");
    if (strstr(k, "LanguageCode")) return rt_str("en");
    if (strstr(k, "CountryCode")) return rt_str("US");
    if (strstr(k, "Identifier")) return rt_str("en_US");
    if (strstr(k, "UsesMetric")) return rt_num_int(1);
    if (strstr(k, "CurrencyCode")) return rt_str("USD");
    if (strstr(k, "CurrencySymbol")) return rt_str("$");
    return NULL;
}
static id lo_preferredLanguages(Class c, SEL s) { (void)c; (void)s; id e = rt_str("en"); return objc_autorelease(rt_array_new(&e, 1)); }
static id lo_displayName(id self, SEL s, id k, id v) { (void)self; (void)s; (void)k; return v; }
static id tz_local(Class c, SEL s) { (void)s; return singleton(c, &g_tz); }
static id tz_withName(Class c, SEL s, id n) { (void)n; return tz_local(c, s); }
static id tz_withOffset(Class c, SEL s, long o) { (void)o; return tz_local(c, s); }
static long tz_secondsFromGMT(id self, SEL s) { (void)self; (void)s; time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm); return tm.tm_gmtoff; }
static long tz_secondsFromGMTForDate(id self, SEL s, id d) { (void)d; return tz_secondsFromGMT(self, s); }
static id tz_name(id self, SEL s) { (void)self; (void)s; const char *z = getenv("TZ"); return rt_str(z && *z ? z : "GMT"); }
static id tz_abbr(id self, SEL s) { (void)self; (void)s; time_t t = time(NULL); struct tm tm; localtime_r(&t, &tm); return rt_str(tm.tm_zone ? tm.tm_zone : "GMT"); }
static rt_bool tz_isDST(id self, SEL s) { (void)self; (void)s; return 0; }

struct nsdc { Class isa; long era, year, month, day, hour, minute, second, nanosecond, weekday, weekdayOrdinal, week; };
#define DC_UNDEF 0x7fffffffffffffffL
static id dc_init(id self, SEL s) {
    (void)s; struct nsdc *d = (struct nsdc *)self;
    d->era = d->year = d->month = d->day = d->hour = d->minute = d->second = d->nanosecond = d->weekday = d->weekdayOrdinal = d->week = DC_UNDEF;
    return self;
}
#define DCPROP(name, field) \
    static long dc_get_##name(id self, SEL s) { (void)s; return ((struct nsdc *)self)->field; } \
    static void dc_set_##name(id self, SEL s, long v) { (void)s; ((struct nsdc *)self)->field = v; }
DCPROP(era, era) DCPROP(year, year) DCPROP(month, month) DCPROP(day, day) DCPROP(hour, hour) DCPROP(minute, minute)
DCPROP(second, second) DCPROP(nanosecond, nanosecond) DCPROP(weekday, weekday) DCPROP(week, week)
static id cal_current(Class c, SEL s) { (void)s; return singleton(c, &g_cal); }
static id cal_initWithId(id self, SEL s, id ident) { (void)s; (void)ident; return self; }
static id cal_components(id self, SEL s, unsigned long units, id date) {
    (void)self; (void)s; (void)units;
    id r = dc_init(rt_alloc_instance(RT_CLASS(NSDateComponents), 0), NULL); struct nsdc *d = (struct nsdc *)r;
    double t = date_ref(date) + kRef1970; time_t tt = (time_t)floor(t); struct tm tm; localtime_r(&tt, &tm);
    d->era = 1; d->year = tm.tm_year + 1900; d->month = tm.tm_mon + 1; d->day = tm.tm_mday; d->hour = tm.tm_hour;
    d->minute = tm.tm_min; d->second = tm.tm_sec; d->weekday = tm.tm_wday + 1; d->nanosecond = (long)((t - floor(t)) * 1e9);
    return objc_autorelease(r);
}
static id cal_dateFromComponents(id self, SEL s, id comps) {
    (void)self; (void)s; struct nsdc *d = (struct nsdc *)comps; struct tm tm; memset(&tm, 0, sizeof tm);
    tm.tm_year = (d->year == DC_UNDEF ? 2001 : (int)d->year) - 1900; tm.tm_mon = (d->month == DC_UNDEF ? 1 : (int)d->month) - 1;
    tm.tm_mday = d->day == DC_UNDEF ? 1 : (int)d->day; tm.tm_hour = d->hour == DC_UNDEF ? 0 : (int)d->hour;
    tm.tm_min = d->minute == DC_UNDEF ? 0 : (int)d->minute; tm.tm_sec = d->second == DC_UNDEF ? 0 : (int)d->second; tm.tm_isdst = -1;
    return date_with_ref((double)mktime(&tm) - kRef1970);
}
static id cal_dateByAdding(id self, SEL s, id comps, id date, unsigned long opts) {
    (void)self; (void)s; (void)opts; struct nsdc *d = (struct nsdc *)comps; double t = date_ref(date);
    if (d->day != DC_UNDEF) t += 86400.0 * (double)d->day;
    if (d->hour != DC_UNDEF) t += 3600.0 * (double)d->hour;
    if (d->minute != DC_UNDEF) t += 60.0 * (double)d->minute;
    if (d->second != DC_UNDEF) t += (double)d->second;
    return date_with_ref(t);
}
static long cal_component(id self, SEL s, unsigned long unit, id date) {
    id c = cal_components(self, s, unit, date); struct nsdc *d = (struct nsdc *)c;
    switch (unit) { case 4: return d->year; case 8: return d->month; case 16: return d->day; case 32: return d->hour; case 64: return d->minute; case 128: return d->second; case 512: return d->weekday; default: return 0; }
}
static id cal_ident(id self, SEL s) { (void)self; (void)s; return rt_str("gregorian"); }
static void noop_obj(id self, SEL s, id o) { (void)self; (void)s; (void)o; }
static void noop_long(id self, SEL s, long v) { (void)self; (void)s; (void)v; }

/* ================================================================= formatters */
struct nsfmt { Class isa; id format; long style, timeStyle, maxFrac, minFrac; };
static void df_setFormat(id self, SEL s, id f) { (void)s; struct nsfmt *x = (struct nsfmt *)self; objc_retain(f); objc_release(x->format); x->format = f; }
static id df_format(id self, SEL s) { (void)s; return ((struct nsfmt *)self)->format; }
static void df_setStyle(id self, SEL s, long v) { (void)s; ((struct nsfmt *)self)->style = v; }
static void df_setTimeStyle(id self, SEL s, long v) { (void)s; ((struct nsfmt *)self)->timeStyle = v; }
/* Unicode date pattern -> strftime */
static void pattern_to_strftime(const char *p, rt_sb *out) {
    while (*p) {
        char c = *p; int n = 0; while (p[n] == c) n++;
        if (c == '\'') { p++; while (*p && *p != '\'') rt_sb_putc(out, *p++); if (*p) p++; continue; }
        const char *r = NULL;
        switch (c) {
        case 'y': r = n == 2 ? "%y" : "%Y"; break;
        case 'M': r = n >= 4 ? "%B" : n == 3 ? "%b" : "%m"; break;
        case 'd': r = "%d"; break;
        case 'H': r = "%H"; break;
        case 'h': r = "%I"; break;
        case 'm': r = "%M"; break;
        case 's': r = "%S"; break;
        case 'a': r = "%p"; break;
        case 'E': r = n >= 4 ? "%A" : "%a"; break;
        case 'Z': case 'z': case 'x': case 'X': r = "%z"; break;
        case 'S': r = "000"; break;
        default: r = NULL;
        }
        if (r) { rt_sb_puts(out, r); p += n; } else rt_sb_putc(out, *p++);
    }
}
static id df_stringFromDate(id self, SEL s, id date) {
    (void)s; struct nsfmt *x = (struct nsfmt *)self;
    double t = date_ref(date) + kRef1970; time_t tt = (time_t)t; struct tm tm; localtime_r(&tt, &tm);
    rt_sb fmt; rt_sb_init(&fmt);
    if (x->format) pattern_to_strftime(rt_str_utf8(x->format), &fmt);
    else rt_sb_puts(&fmt, x->style && x->timeStyle ? "%m/%d/%y, %H:%M" : x->timeStyle ? "%H:%M" : "%m/%d/%y");
    char b[256]; strftime(b, sizeof b, fmt.buf ? fmt.buf : "", &tm); free(fmt.buf);
    return rt_str(b);
}
static id df_dateFromString(id self, SEL s, id str) {
    (void)s; struct nsfmt *x = (struct nsfmt *)self; if (!str) return NULL;
    rt_sb fmt; rt_sb_init(&fmt);
    pattern_to_strftime(x->format ? rt_str_utf8(x->format) : "yyyy-MM-dd", &fmt);
    struct tm tm; memset(&tm, 0, sizeof tm); tm.tm_isdst = -1;
    char *end = strptime(rt_str_utf8(str), fmt.buf ? fmt.buf : "", &tm); free(fmt.buf);
    if (!end) return NULL;
    return date_with_ref((double)mktime(&tm) - kRef1970);
}
static id nf_stringFromNumber(id self, SEL s, id n) {
    (void)s; struct nsfmt *x = (struct nsfmt *)self; char b[64];
    if (!n) return NULL;
    if (x->maxFrac > 0) snprintf(b, sizeof b, "%.*f", (int)x->maxFrac, RT_SEND0(double, n, "doubleValue"));
    else return RT_SEND0(id, n, "stringValue");
    return rt_str(b);
}
static id nf_numberFromString(id self, SEL s, id str) { (void)self; (void)s; if (!str) return NULL; char *e; double d = strtod(rt_str_utf8(str), &e); if (e == rt_str_utf8(str)) return NULL; return rt_num_dbl(d); }
static void nf_setMax(id self, SEL s, unsigned long v) { (void)s; ((struct nsfmt *)self)->maxFrac = (long)v; }
static void nf_setMin(id self, SEL s, unsigned long v) { (void)s; ((struct nsfmt *)self)->minFrac = (long)v; }

/* ============================================================== keyed archiving */
static id ka_archivedData(Class c, SEL s, id root) { (void)c; (void)s; (void)root; return objc_autorelease(rt_data_new("", 0)); }
static rt_bool ka_archiveToFile(Class c, SEL s, id root, id path) { (void)c; (void)s; (void)root; (void)path; return 0; }
static id ku_unarchive(Class c, SEL s, id data) { (void)c; (void)s; (void)data; return NULL; }
static id coder_nil(id self, SEL s, id k) { (void)self; (void)s; (void)k; return NULL; }
static long coder_zero(id self, SEL s, id k) { (void)self; (void)s; (void)k; return 0; }
static double coder_zerod(id self, SEL s, id k) { (void)self; (void)s; (void)k; return 0; }
static rt_bool coder_no(id self, SEL s, id k) { (void)self; (void)s; (void)k; return 0; }
static void coder_encode(id self, SEL s, id o, id k) { (void)self; (void)s; (void)o; (void)k; }

/* ========================================================================= JSON */
struct jp { const char *p, *e; int err; };
static void jws(struct jp *j) { while (j->p < j->e && isspace((unsigned char)*j->p)) j->p++; }
static id jvalue(struct jp *j, int mutable_, int depth);
static id jstring(struct jp *j) {
    rt_sb sb; rt_sb_init(&sb); j->p++;
    while (j->p < j->e && *j->p != '"') {
        char c = *j->p++;
        if (c == '\\' && j->p < j->e) {
            char d = *j->p++;
            switch (d) {
            case 'n': rt_sb_putc(&sb, '\n'); break; case 't': rt_sb_putc(&sb, '\t'); break; case 'r': rt_sb_putc(&sb, '\r'); break;
            case 'b': rt_sb_putc(&sb, '\b'); break; case 'f': rt_sb_putc(&sb, '\f'); break;
            case 'u': {
                unsigned cp = 0; for (int i = 0; i < 4 && j->p < j->e; i++) { char h = *j->p++; cp = cp * 16 + (unsigned)(isdigit((unsigned char)h) ? h - '0' : (tolower((unsigned char)h) - 'a' + 10)); }
                if (cp >= 0xd800 && cp < 0xdc00 && j->p + 6 <= j->e && j->p[0] == '\\' && j->p[1] == 'u') {
                    unsigned lo = 0; j->p += 2; for (int i = 0; i < 4; i++) { char h = *j->p++; lo = lo * 16 + (unsigned)(isdigit((unsigned char)h) ? h - '0' : (tolower((unsigned char)h) - 'a' + 10)); }
                    cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
                }
                char b[4]; int n;
                if (cp < 0x80) { b[0] = (char)cp; n = 1; } else if (cp < 0x800) { b[0] = (char)(0xc0 | cp >> 6); b[1] = (char)(0x80 | (cp & 0x3f)); n = 2; }
                else if (cp < 0x10000) { b[0] = (char)(0xe0 | cp >> 12); b[1] = (char)(0x80 | ((cp >> 6) & 0x3f)); b[2] = (char)(0x80 | (cp & 0x3f)); n = 3; }
                else { b[0] = (char)(0xf0 | cp >> 18); b[1] = (char)(0x80 | ((cp >> 12) & 0x3f)); b[2] = (char)(0x80 | ((cp >> 6) & 0x3f)); b[3] = (char)(0x80 | (cp & 0x3f)); n = 4; }
                rt_sb_append(&sb, b, (size_t)n); break;
            }
            default: rt_sb_putc(&sb, d);
            }
        } else rt_sb_putc(&sb, c);
    }
    if (j->p < j->e) j->p++; else j->err = 1;
    char *t = rt_sb_take(&sb); id r = rt_str(t); free(t); return r;
}
static id jvalue(struct jp *j, int mut, int depth) {
    jws(j);
    if (j->p >= j->e || depth > 256) { j->err = 1; return NULL; }
    char c = *j->p;
    if (c == '{') {
        j->p++; id d = objc_autorelease(rt_dict_new(NULL, NULL, 0));
        jws(j); if (j->p < j->e && *j->p == '}') { j->p++; return d; }
        for (;;) {
            jws(j); if (j->p >= j->e || *j->p != '"') { j->err = 1; return NULL; }
            id k = jstring(j); jws(j);
            if (j->p >= j->e || *j->p != ':') { j->err = 1; return NULL; }
            j->p++; id v = jvalue(j, mut, depth + 1); if (j->err) return NULL;
            rt_dict_set(d, k, v); jws(j);
            if (j->p < j->e && *j->p == ',') { j->p++; continue; }
            if (j->p < j->e && *j->p == '}') { j->p++; break; }
            j->err = 1; return NULL;
        }
        return mut ? d : RT_SEND0(id, RT_SEND0(id, d, "copy"), "autorelease");
    }
    if (c == '[') {
        j->p++; id a = RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "array");
        jws(j); if (j->p < j->e && *j->p == ']') { j->p++; return a; }
        for (;;) {
            id v = jvalue(j, mut, depth + 1); if (j->err) return NULL;
            RT_SEND1(void, a, "addObject:", id, v); jws(j);
            if (j->p < j->e && *j->p == ',') { j->p++; continue; }
            if (j->p < j->e && *j->p == ']') { j->p++; break; }
            j->err = 1; return NULL;
        }
        return a;
    }
    if (c == '"') return jstring(j);
    if (!strncmp(j->p, "true", 4)) { j->p += 4; return RT_SEND1(id, (id)RT_CLASS(NSNumber), "numberWithBool:", long, 1); }
    if (!strncmp(j->p, "false", 5)) { j->p += 5; return RT_SEND1(id, (id)RT_CLASS(NSNumber), "numberWithBool:", long, 0); }
    if (!strncmp(j->p, "null", 4)) { j->p += 4; return nsnull(); }
    char *end; double d = strtod(j->p, &end);
    if (end == j->p) { j->err = 1; return NULL; }
    int isint = 1; for (const char *q = j->p; q < end; q++) if (*q == '.' || *q == 'e' || *q == 'E') isint = 0;
    j->p = end;
    return isint ? rt_num_int((long long)d) : rt_num_dbl(d);
}
static id js_parse(Class c, SEL s, id data, unsigned long opts, id *err) {
    (void)c; (void)s; if (err) *err = NULL; if (!data) return NULL;
    size_t n; const char *b = rt_data_bytes(data, &n);
    struct jp j = { b, b + n, 0 };
    id r = jvalue(&j, (opts & 1) != 0, 0);
    if (j.err) { if (err) *err = RT_SEND2(id, (id)rt_class_named("NSError"), "errorWithDomain:code:userInfo:", id, rt_str("NSCocoaErrorDomain"), long, 3840) ; return NULL; }
    return r;
}
static void jwrite(rt_sb *sb, id o, int pretty, int depth);
static void jindent(rt_sb *sb, int pretty, int depth) { if (pretty) { rt_sb_putc(sb, '\n'); for (int i = 0; i < depth; i++) rt_sb_puts(sb, "  "); } }
static void jwrite_str(rt_sb *sb, const char *s) {
    rt_sb_putc(sb, '"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { rt_sb_putc(sb, '\\'); rt_sb_putc(sb, (char)c); }
        else if (c == '\n') rt_sb_puts(sb, "\\n"); else if (c == '\t') rt_sb_puts(sb, "\\t"); else if (c == '\r') rt_sb_puts(sb, "\\r");
        else if (c < 0x20) rt_sb_printf(sb, "\\u%04x", c);
        else rt_sb_putc(sb, (char)c);
    }
    rt_sb_putc(sb, '"');
}
static void jwrite(rt_sb *sb, id o, int pretty, int depth) {
    if (!o || o == nsnull()) { rt_sb_puts(sb, "null"); return; }
    if (rt_is_string(o)) { jwrite_str(sb, rt_str_utf8(o)); return; }
    if (rt_is_kind_of(o, RT_CLASS(NSNumber))) {
        const char *t = RT_SEND0(const char *, o, "objCType");
        if (t[0] == 'c') rt_sb_puts(sb, RT_SEND0(long, o, "boolValue") & 0xff ? "true" : "false");
        else rt_sb_puts(sb, rt_str_utf8(RT_SEND0(id, o, "stringValue")));
        return;
    }
    if (rt_is_kind_of(o, RT_CLASS(NSDictionary))) {
        id keys = RT_SEND0(id, o, "allKeys"); size_t n = rt_array_count(keys);
        rt_sb_putc(sb, '{');
        for (size_t i = 0; i < n; i++) {
            id k = rt_array_at(keys, i);
            if (i) rt_sb_putc(sb, ',');
            jindent(sb, pretty, depth + 1);
            jwrite_str(sb, rt_str_utf8(k)); rt_sb_puts(sb, pretty ? " : " : ":");
            jwrite(sb, rt_dict_get(o, k), pretty, depth + 1);
        }
        if (n) jindent(sb, pretty, depth);
        rt_sb_putc(sb, '}'); return;
    }
    if (rt_is_kind_of(o, RT_CLASS(NSArray))) {
        size_t n = rt_array_count(o); rt_sb_putc(sb, '[');
        for (size_t i = 0; i < n; i++) { if (i) rt_sb_putc(sb, ','); jindent(sb, pretty, depth + 1); jwrite(sb, rt_array_at(o, i), pretty, depth + 1); }
        if (n) jindent(sb, pretty, depth);
        rt_sb_putc(sb, ']'); return;
    }
    char *d = rt_describe(o); jwrite_str(sb, d); free(d);
}
static id js_write(Class c, SEL s, id obj, unsigned long opts, id *err) {
    (void)c; (void)s; if (err) *err = NULL;
    rt_sb sb; rt_sb_init(&sb); jwrite(&sb, obj, (opts & 1) != 0, 0);
    id r = objc_autorelease(rt_data_new(sb.buf, sb.len)); free(sb.buf); return r;
}
static rt_bool js_valid(Class c, SEL s, id o) { (void)c; (void)s; return o && (rt_is_kind_of(o, RT_CLASS(NSDictionary)) || rt_is_kind_of(o, RT_CLASS(NSArray))); }

/* =========================================================== property lists */
static id pl_parse(Class c, SEL s, id data, unsigned long opts, long *fmt, id *err) {
    (void)c; (void)s; (void)opts; if (err) *err = NULL; if (fmt) *fmt = 100; if (!data) return NULL;
    size_t n; const uint8_t *b = rt_data_bytes(data, &n);
    return objc_autorelease(rt_plist_parse(b, n));
}
static id pl_parseOld(Class c, SEL s, id data, unsigned long opts, long *fmt, id *err) { return pl_parse(c, s, data, opts, fmt, err); }
static void plx(rt_sb *sb, id o, int depth) {
    for (int i = 0; i < depth; i++) rt_sb_putc(sb, '\t');
    if (!o) { rt_sb_puts(sb, "<string></string>\n"); return; }
    if (rt_is_string(o)) {
        rt_sb_puts(sb, "<string>");
        for (const char *p = rt_str_utf8(o); *p; p++) { if (*p == '<') rt_sb_puts(sb, "&lt;"); else if (*p == '>') rt_sb_puts(sb, "&gt;"); else if (*p == '&') rt_sb_puts(sb, "&amp;"); else rt_sb_putc(sb, *p); }
        rt_sb_puts(sb, "</string>\n"); return;
    }
    if (rt_is_kind_of(o, RT_CLASS(NSNumber))) {
        const char *t = RT_SEND0(const char *, o, "objCType");
        if (t[0] == 'c') rt_sb_puts(sb, RT_SEND0(long, o, "boolValue") & 0xff ? "<true/>\n" : "<false/>\n");
        else if (t[0] == 'd') rt_sb_printf(sb, "<real>%s</real>\n", rt_str_utf8(RT_SEND0(id, o, "stringValue")));
        else rt_sb_printf(sb, "<integer>%s</integer>\n", rt_str_utf8(RT_SEND0(id, o, "stringValue")));
        return;
    }
    if (rt_is_kind_of(o, RT_CLASS(NSDictionary))) {
        rt_sb_puts(sb, "<dict>\n"); id keys = RT_SEND0(id, o, "allKeys");
        for (size_t i = 0; i < rt_array_count(keys); i++) {
            id k = rt_array_at(keys, i);
            for (int d = 0; d <= depth; d++) rt_sb_putc(sb, '\t');
            rt_sb_printf(sb, "<key>%s</key>\n", rt_str_utf8(k));
            plx(sb, rt_dict_get(o, k), depth + 1);
        }
        for (int i = 0; i < depth; i++) rt_sb_putc(sb, '\t');
        rt_sb_puts(sb, "</dict>\n"); return;
    }
    if (rt_is_kind_of(o, RT_CLASS(NSArray))) {
        rt_sb_puts(sb, "<array>\n");
        for (size_t i = 0; i < rt_array_count(o); i++) plx(sb, rt_array_at(o, i), depth + 1);
        for (int i = 0; i < depth; i++) rt_sb_putc(sb, '\t');
        rt_sb_puts(sb, "</array>\n"); return;
    }
    if (rt_is_kind_of(o, RT_CLASS(NSData))) {
        static const char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        size_t n; const uint8_t *b = rt_data_bytes(o, &n); rt_sb_puts(sb, "<data>");
        for (size_t i = 0; i < n; i += 3) {
            uint32_t v = (uint32_t)b[i] << 16 | (i + 1 < n ? (uint32_t)b[i + 1] << 8 : 0) | (i + 2 < n ? b[i + 2] : 0);
            rt_sb_putc(sb, b64[v >> 18 & 63]); rt_sb_putc(sb, b64[v >> 12 & 63]);
            rt_sb_putc(sb, i + 1 < n ? b64[v >> 6 & 63] : '='); rt_sb_putc(sb, i + 2 < n ? b64[v & 63] : '=');
        }
        rt_sb_puts(sb, "</data>\n"); return;
    }
    if (rt_is_kind_of(o, RT_CLASS(NSDate))) { rt_sb_puts(sb, "<date>2001-01-01T00:00:00Z</date>\n"); return; }
    char *d = rt_describe(o); rt_sb_printf(sb, "<string>%s</string>\n", d); free(d);
}
id rt_plist_xml(id o) {
    rt_sb sb; rt_sb_init(&sb);
    rt_sb_puts(&sb, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n<plist version=\"1.0\">\n");
    plx(&sb, o, 0); rt_sb_puts(&sb, "</plist>\n");
    id r = objc_autorelease(rt_data_new(sb.buf, sb.len)); free(sb.buf); return r;
}
static id pl_data(Class c, SEL s, id o, long fmt, unsigned long opts, id *err) { (void)c; (void)s; (void)fmt; (void)opts; if (err) *err = NULL; return rt_plist_xml(o); }
static rt_bool coll_writeToFile(id self, SEL s, id path, long atomic) {
    (void)s; (void)atomic; id d = rt_plist_xml(self); size_t n; const void *b = rt_data_bytes(d, &n);
    FILE *f = fopen(rt_str_utf8(path), "wb"); if (!f) return 0; fwrite(b, 1, n, f); fclose(f); return 1;
}
static id coll_withContentsOfFile(Class c, SEL s, id path) {
    (void)s; if (!path) return NULL;
    id d = RT_SEND1(id, (id)RT_CLASS(NSData), "dataWithContentsOfFile:", id, path); if (!d) return NULL;
    size_t n; const uint8_t *b = rt_data_bytes(d, &n); id r = rt_plist_parse(b, n);
    if (!r || !rt_is_kind_of(r, c == RT_CLASS(NSMutableArray) || c == RT_CLASS(NSArray) ? RT_CLASS(NSArray) : RT_CLASS(NSDictionary))) { objc_release(r); return NULL; }
    id m = RT_SEND0(id, r, "mutableCopy"); objc_release(r); return objc_autorelease(m);
}

/* ======================================================================== NSScanner */
struct nsscan { Class isa; id str; unsigned long loc; };
static id sc_make(Class c, SEL s, id str) { (void)s; id o = rt_alloc_instance(c, 0); ((struct nsscan *)o)->str = objc_retain(str); return objc_autorelease(o); }
static id sc_init(id self, SEL s, id str) { (void)s; ((struct nsscan *)self)->str = objc_retain(str); return self; }
static const char *sc_cur(id self) { struct nsscan *x = (struct nsscan *)self; const char *u = rt_str_utf8(x->str); size_t n = strlen(u); return u + (x->loc < n ? x->loc : n); }
static void sc_skip(id self) { const char *p = sc_cur(self); const char *q = p; while (*q && isspace((unsigned char)*q)) q++; ((struct nsscan *)self)->loc += (unsigned long)(q - p); }
static rt_bool sc_int(id self, SEL s, int *v) { (void)s; sc_skip(self); const char *p = sc_cur(self); char *e; long r = strtol(p, &e, 10); if (e == p) return 0; if (v) *v = (int)r; ((struct nsscan *)self)->loc += (unsigned long)(e - p); return 1; }
static rt_bool sc_integer(id self, SEL s, long *v) { (void)s; sc_skip(self); const char *p = sc_cur(self); char *e; long r = strtol(p, &e, 10); if (e == p) return 0; if (v) *v = r; ((struct nsscan *)self)->loc += (unsigned long)(e - p); return 1; }
static rt_bool sc_double(id self, SEL s, double *v) { (void)s; sc_skip(self); const char *p = sc_cur(self); char *e; double r = strtod(p, &e); if (e == p) return 0; if (v) *v = r; ((struct nsscan *)self)->loc += (unsigned long)(e - p); return 1; }
static rt_bool sc_float(id self, SEL s, float *v) { double d; if (!sc_double(self, s, &d)) return 0; if (v) *v = (float)d; return 1; }
static rt_bool sc_hex(id self, SEL s, unsigned *v) { (void)s; sc_skip(self); const char *p = sc_cur(self); char *e; unsigned long r = strtoul(p, &e, 16); if (e == p) return 0; if (v) *v = (unsigned)r; ((struct nsscan *)self)->loc += (unsigned long)(e - p); return 1; }
static rt_bool sc_string(id self, SEL s, id str, id *out) { (void)s; sc_skip(self); const char *p = sc_cur(self), *w = rt_str_utf8(str); size_t n = strlen(w); if (strncmp(p, w, n)) return 0; ((struct nsscan *)self)->loc += n; if (out) *out = str; return 1; }
static rt_bool sc_upTo(id self, SEL s, id str, id *out) {
    (void)s; const char *p = sc_cur(self), *w = rt_str_utf8(str); const char *f = strstr(p, w); size_t n = f ? (size_t)(f - p) : strlen(p);
    if (!n) return 0; char *t = strndup(p, n); if (out) *out = rt_str(t); free(t); ((struct nsscan *)self)->loc += n; return 1;
}
static rt_bool sc_atEnd(id self, SEL s) { (void)s; sc_skip(self); return *sc_cur(self) == 0; }
static unsigned long sc_loc(id self, SEL s) { (void)s; return ((struct nsscan *)self)->loc; }
static void sc_setLoc(id self, SEL s, unsigned long l) { (void)s; ((struct nsscan *)self)->loc = l; }

/* ================================================================== NSSortDescriptor */
struct nssd { Class isa; id key; long asc; SEL sel; };
static id sd_make(Class c, SEL s, id key, long asc) { (void)s; id o = rt_alloc_instance(c, 0); struct nssd *d = (struct nssd *)o; d->key = objc_retain(key); d->asc = asc & 0xff; d->sel = rt_sel("compare:"); return objc_autorelease(o); }
static id sd_makeSel(Class c, SEL s, id key, long asc, SEL sel) { id o = sd_make(c, s, key, asc); ((struct nssd *)o)->sel = sel; return o; }
static id sd_init(id self, SEL s, id key, long asc) { (void)s; struct nssd *d = (struct nssd *)self; d->key = objc_retain(key); d->asc = asc & 0xff; d->sel = rt_sel("compare:"); return self; }
static id sd_key(id self, SEL s) { (void)s; return ((struct nssd *)self)->key; }
static rt_bool sd_asc(id self, SEL s) { (void)s; return ((struct nssd *)self)->asc; }
static long sd_compare(id self, SEL s, id a, id b) {
    (void)s; struct nssd *d = (struct nssd *)self;
    id va = d->key ? RT_SEND1(id, a, "valueForKeyPath:", id, d->key) : a, vb = d->key ? RT_SEND1(id, b, "valueForKeyPath:", id, d->key) : b;
    long r = va ? ((long (*)(id, SEL, id))objc_msgSend)(va, d->sel, vb) : (vb ? -1 : 0);
    return d->asc ? r : -r;
}
static long descs_cmp(id a, id b, void *ctx) { id descs = ctx; for (size_t i = 0; i < rt_array_count(descs); i++) { long r = sd_compare(rt_array_at(descs, i), NULL, a, b); if (r) return r; } return 0; }
static id arr_sortedDescs(id self, SEL s, id descs) { (void)s; return RT_SEND2(id, self, "sortedArrayUsingFunction:context:", void *, (void *)descs_cmp, void *, (void *)descs); }
static void marr_sortDescs(id self, SEL s, id descs) { id r = arr_sortedDescs(self, s, descs); RT_SEND0(void, self, "removeAllObjects"); RT_SEND1(void, self, "addObjectsFromArray:", id, r); }

/* =============================================================== NSAttributedString */
struct nsas { Class isa; id str; id attrs; };
static id as_initStr(id self, SEL s, id str) { (void)s; struct nsas *a = (struct nsas *)self; a->str = RT_SEND0(id, RT_SEND1(id, (id)RT_CLASS(NSMutableString), "stringWithString:", id, str ? str : rt_str("")), "retain"); return self; }
static id as_initStrAttrs(id self, SEL s, id str, id attrs) { as_initStr(self, s, str); ((struct nsas *)self)->attrs = objc_retain(attrs); return self; }
static id as_init(id self, SEL s) { return as_initStr(self, s, NULL); }
static id as_string(id self, SEL s) { (void)s; return ((struct nsas *)self)->str; }
static unsigned long as_length(id self, SEL s) { (void)s; return RT_SEND0(unsigned long, ((struct nsas *)self)->str, "length"); }
static id as_attrsAt(id self, SEL s, unsigned long i, NSRange *r) { (void)s; (void)i; struct nsas *a = (struct nsas *)self; if (r) { r->location = 0; r->length = as_length(self, s); } return a->attrs ? a->attrs : objc_autorelease(rt_dict_new(NULL, NULL, 0)); }
static id as_attrAt(id self, SEL s, id name, unsigned long i, NSRange *r) { id d = as_attrsAt(self, s, i, r); return rt_dict_get(d, name); }
static void as_append(id self, SEL s, id other) { (void)s; RT_SEND1(void, ((struct nsas *)self)->str, "appendString:", id, RT_SEND0(id, other, "string")); }
static void as_addAttr(id self, SEL s, id name, id value, NSRange r) { (void)s; (void)r; struct nsas *a = (struct nsas *)self; if (!a->attrs) a->attrs = rt_dict_new(NULL, NULL, 0); if (name && value) rt_dict_set(a->attrs, name, value); }
static void as_addAttrs(id self, SEL s, id attrs, NSRange r) { id keys = RT_SEND0(id, attrs, "allKeys"); for (size_t i = 0; i < rt_array_count(keys); i++) as_addAttr(self, s, rt_array_at(keys, i), rt_dict_get(attrs, rt_array_at(keys, i)), r); }
static void as_replace(id self, SEL s, NSRange r, id str) { (void)s; RT_SEND2(void, ((struct nsas *)self)->str, "replaceCharactersInRange:withString:", NSRange, r, id, str); }
static id as_copy(id self, SEL s, void *z) { (void)s; (void)z; struct nsas *a = (struct nsas *)self; return as_initStrAttrs(rt_alloc_instance(RT_CLASS(NSAttributedString), 0), NULL, a->str, a->attrs); }
static id as_mcopy(id self, SEL s, void *z) { (void)s; (void)z; struct nsas *a = (struct nsas *)self; return as_initStrAttrs(rt_alloc_instance(RT_CLASS(NSMutableAttributedString), 0), NULL, a->str, a->attrs); }
static id as_desc(id self, SEL s) { (void)s; return ((struct nsas *)self)->str; }

/* ===================================================================== NSFileHandle */
struct nsfh { Class isa; int fd; long own; };
static id fh_make(Class c, int fd, long own) { if (fd < 0) return NULL; id o = rt_alloc_instance(c, 0); ((struct nsfh *)o)->fd = fd; ((struct nsfh *)o)->own = own; return objc_autorelease(o); }
static id fh_read(Class c, SEL s, id path) { (void)s; return path ? fh_make(c, open(rt_str_utf8(path), O_RDONLY), 1) : NULL; }
static id fh_write(Class c, SEL s, id path) { (void)s; return path ? fh_make(c, open(rt_str_utf8(path), O_WRONLY), 1) : NULL; }
static id fh_update(Class c, SEL s, id path) { (void)s; return path ? fh_make(c, open(rt_str_utf8(path), O_RDWR), 1) : NULL; }
static id fh_stdout(Class c, SEL s) { (void)s; return fh_make(c, 1, 0); }
static id fh_stderr(Class c, SEL s) { (void)s; return fh_make(c, 2, 0); }
static id fh_readToEnd(id self, SEL s) {
    (void)s; struct nsfh *f = (struct nsfh *)self; rt_sb sb; rt_sb_init(&sb); char b[8192]; ssize_t n;
    while ((n = read(f->fd, b, sizeof b)) > 0) rt_sb_append(&sb, b, (size_t)n);
    id r = objc_autorelease(rt_data_new(sb.buf ? sb.buf : "", sb.len)); free(sb.buf); return r;
}
static id fh_readLen(id self, SEL s, unsigned long len) {
    (void)s; struct nsfh *f = (struct nsfh *)self; char *b = rt_xmalloc(len + 1); ssize_t n = read(f->fd, b, len); id r = objc_autorelease(rt_data_new(b, n > 0 ? (size_t)n : 0)); free(b); return r;
}
static void fh_writeData(id self, SEL s, id d) {
    (void)s; struct nsfh *f = (struct nsfh *)self; size_t n; const void *b = rt_data_bytes(d, &n);
    if (f->fd == 1 || f->fd == 2) rt_out(f->fd, b, n); else if (write(f->fd, b, n) < 0) { /* ignore */ }
}
static unsigned long long fh_seekEnd(id self, SEL s) { (void)s; return (unsigned long long)lseek(((struct nsfh *)self)->fd, 0, SEEK_END); }
static void fh_seek(id self, SEL s, unsigned long long o) { (void)s; lseek(((struct nsfh *)self)->fd, (off_t)o, SEEK_SET); }
static unsigned long long fh_offset(id self, SEL s) { (void)s; return (unsigned long long)lseek(((struct nsfh *)self)->fd, 0, SEEK_CUR); }
static void fh_truncate(id self, SEL s, unsigned long long o) { (void)s; if (ftruncate(((struct nsfh *)self)->fd, (off_t)o)) { /* ignore */ } }
static void fh_close(id self, SEL s) { (void)s; struct nsfh *f = (struct nsfh *)self; if (f->own && f->fd > 2) close(f->fd); f->fd = -1; }
static void fh_sync(id self, SEL s) { (void)s; fsync(((struct nsfh *)self)->fd); }
static void fh_dealloc(id self, SEL s) { fh_close(self, s); rt_dealloc_instance(self); }

/* ===================================================== additions to existing classes */
static id set_withObjects(Class c, SEL s, id first, const uint64_t *rest) {
    (void)s; size_t n = 0, cap = 8; id *v = rt_xmalloc(cap * sizeof(id));
    for (id o = first; o; o = (id)(uintptr_t)rest[n - 1]) { if (n == cap) { cap *= 2; v = realloc(v, cap * sizeof(id)); } v[n++] = o; }
    id r = rt_set_new(v, n); free(v);
    if (c != RT_CLASS(NSSet)) { id m = RT_SEND0(id, r, "mutableCopy"); objc_release(r); r = m; }
    return objc_autorelease(r);
}
id rt_NSSet_setWithObjects(Class c, SEL s, id first, const uint64_t *rest) { return set_withObjects(c, s, first, rest); }
id rt_NSSet_initWithObjects(id self, SEL s, id first, const uint64_t *rest) { Class c = self->isa; rt_dealloc_instance(self); return objc_retain(set_withObjects(c, s, first, rest)); }
static id set_initWithArray(id self, SEL s, id arr) {
    (void)s; Class c = self->isa; rt_dealloc_instance(self);
    size_t n = rt_array_count(arr); id *v = rt_xmalloc((n + 1) * sizeof(id));
    for (size_t i = 0; i < n; i++) v[i] = rt_array_at(arr, i);
    id r = rt_set_new(v, n); free(v);
    if (c != RT_CLASS(NSSet)) { id m = RT_SEND0(id, r, "mutableCopy"); objc_release(r); r = m; }
    return r;
}
static id set_byAdding(id self, SEL s, id o) { (void)s; id m = RT_SEND0(id, self, "mutableCopy"); if (o) RT_SEND1(void, m, "addObject:", id, o); id r = RT_SEND0(id, m, "copy"); objc_release(m); return objc_autorelease(r); }
static id set_byAddingArray(id self, SEL s, id a) { (void)s; id m = RT_SEND0(id, self, "mutableCopy"); for (size_t i = 0; i < rt_array_count(a); i++) RT_SEND1(void, m, "addObject:", id, rt_array_at(a, i)); id r = RT_SEND0(id, m, "copy"); objc_release(m); return objc_autorelease(r); }
static void tz_reset(Class c, SEL s) { (void)c; (void)s; }
static id bundle_withPath(Class c, SEL s, id path) { (void)path; return RT_SEND0(id, (id)c, "mainBundle"); (void)s; }
static id set_withSet(Class c, SEL s, id other) { return objc_autorelease(set_initWithArray(rt_alloc_instance(c, 0), s, RT_SEND0(id, other, "allObjects"))); }
static rt_bool data_writeOpts(id self, SEL s, id path, unsigned long opts, id *err) { (void)opts; if (err) *err = NULL; return RT_SEND2(long, self, "writeToFile:atomically:", id, path, long, 1) & 0xff ? 1 : 0; (void)s; }
static rt_bool data_writeURL(id self, SEL s, id url, long atomic) { (void)atomic; return data_writeOpts(self, s, RT_SEND0(id, url, "path"), 0, NULL); }
static id data_withContentsOfURL(Class c, SEL s, id url) { (void)s; return url ? RT_SEND1(id, (id)c, "dataWithContentsOfFile:", id, RT_SEND0(id, url, "path")) : NULL; }
static id data_withContentsOfFileOpts(Class c, SEL s, id path, unsigned long o, id *err) { (void)s; (void)o; if (err) *err = NULL; return RT_SEND1(id, (id)c, "dataWithContentsOfFile:", id, path); }
static id data_base64(id self, SEL s, unsigned long o) {
    (void)s; (void)o; static const char *b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t n; const uint8_t *b = rt_data_bytes(self, &n); rt_sb sb; rt_sb_init(&sb);
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)b[i] << 16 | (i + 1 < n ? (uint32_t)b[i + 1] << 8 : 0) | (i + 2 < n ? b[i + 2] : 0);
        rt_sb_putc(&sb, b64[v >> 18 & 63]); rt_sb_putc(&sb, b64[v >> 12 & 63]); rt_sb_putc(&sb, i + 1 < n ? b64[v >> 6 & 63] : '='); rt_sb_putc(&sb, i + 2 < n ? b64[v & 63] : '=');
    }
    char *t = rt_sb_take(&sb); id r = rt_str(t); free(t); return r;
}
static const char *str_fsrep(id self, SEL s) { (void)s; return rt_str_utf8(self); }
static rt_bool str_getFSRep(id self, SEL s, char *buf, unsigned long max) { (void)s; const char *u = rt_str_utf8(self); size_t n = strlen(u); if (n + 1 > max) return 0; memcpy(buf, u, n + 1); return 1; }
static rt_bool str_isAbsolute(id self, SEL s) { (void)s; return rt_str_utf8(self)[0] == '/'; }
static id str_expandTilde(id self, SEL s) {
    (void)s; const char *u = rt_str_utf8(self); if (u[0] != '~') return self;
    char b[4200]; snprintf(b, sizeof b, "%s%s", rt_home_dir(), u + 1); return rt_str(b);
}
static id str_pathComponents(id self, SEL s) {
    (void)s; const char *u = rt_str_utf8(self); id a = RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "array");
    if (*u == '/') RT_SEND1(void, a, "addObject:", id, rt_str("/"));
    char *t = rt_xstrdup(u), *save = NULL;
    for (char *p = strtok_r(t, "/", &save); p; p = strtok_r(NULL, "/", &save)) RT_SEND1(void, a, "addObject:", id, rt_str(p));
    free(t); return a;
}
static id str_percentEscapes(id self, SEL s, unsigned long enc) {
    (void)s; (void)enc; const unsigned char *u = (const unsigned char *)rt_str_utf8(self); rt_sb sb; rt_sb_init(&sb);
    for (; *u; u++) { if (isalnum(*u) || strchr("-_.~/:?=&#+@!$'()*,;", *u)) rt_sb_putc(&sb, (char)*u); else rt_sb_printf(&sb, "%%%02X", *u); }
    char *t = rt_sb_take(&sb); id r = rt_str(t); free(t); return r;
}
static id str_percentAllowed(id self, SEL s, id set) { (void)set; return str_percentEscapes(self, s, 4); }
static id str_removePercent(id self, SEL s) {
    (void)s; const char *u = rt_str_utf8(self); rt_sb sb; rt_sb_init(&sb);
    for (; *u; u++) { if (*u == '%' && isxdigit((unsigned char)u[1]) && isxdigit((unsigned char)u[2])) { char h[3] = { u[1], u[2], 0 }; rt_sb_putc(&sb, (char)strtol(h, NULL, 16)); u += 2; } else rt_sb_putc(&sb, *u); }
    char *t = rt_sb_take(&sb); id r = rt_str(t); free(t); return r;
}
static id str_padding(id self, SEL s, unsigned long len, id pad, unsigned long start) {
    (void)s; (void)start; const char *u = rt_str_utf8(self), *p = pad ? rt_str_utf8(pad) : " "; size_t n = strlen(u);
    rt_sb sb; rt_sb_init(&sb); rt_sb_append(&sb, u, n < len ? n : len);
    for (size_t i = n, k = 0; i < len && *p; i++, k++) rt_sb_putc(&sb, p[k % strlen(p)]);
    char *t = rt_sb_take(&sb); id r = rt_str(t); free(t); return r;
}
static id str_componentsBySet(id self, SEL s, id set) {
    (void)s; (void)set; const char *u = rt_str_utf8(self); id a = RT_SEND0(id, (id)RT_CLASS(NSMutableArray), "array");
    const char *st = u;
    for (const char *p = u;; p++) { if (!*p || isspace((unsigned char)*p) || *p == ',') { char *t = strndup(st, (size_t)(p - st)); RT_SEND1(void, a, "addObject:", id, rt_str(t)); free(t); if (!*p) break; st = p + 1; } }
    return a;
}
static NSRange str_rangeOptsRange(id self, SEL s, id str, unsigned long opts, NSRange r) {
    id sub = RT_SEND1(id, self, "substringWithRange:", NSRange, r);
    NSRange f = RT_SEND2(NSRange, sub, "rangeOfString:options:", id, str, unsigned long, opts);
    if (f.length) f.location += r.location;
    (void)s; return f;
}
static unsigned long str_lengthOfBytesUTF8(id self, SEL s, unsigned long e) { (void)s; (void)e; return strlen(rt_str_utf8(self)); }
static long str_localizedCaseInsensitiveCompare(id self, SEL s, id o) { (void)s; return RT_SEND1(long, self, "caseInsensitiveCompare:", id, o); }
static id str_initWithData(id self, SEL s, id data, unsigned long enc) {
    (void)s; if (!data) { objc_release(self); return NULL; }
    size_t n; const void *b = rt_data_bytes(data, &n);
    return ((id (*)(id, SEL, const void *, unsigned long, unsigned long))objc_msgSend)(self, rt_sel("initWithBytes:length:encoding:"), b, n, enc);
}
static id str_initWithContentsOfFile(id self, SEL s, id path, unsigned long enc, id *err) {
    (void)s; objc_release(self);
    id r = RT_SEND2(id, (id)RT_CLASS(NSString), "stringWithContentsOfFile:encoding:error:", id, path, unsigned long, enc);
    if (err) *err = NULL; return objc_retain(r);
}
static id str_stringWithContentsOfFileUsed(Class c, SEL s, id path, unsigned long *enc, id *err) { (void)s; if (enc) *enc = 4; return RT_SEND2(id, (id)c, "stringWithContentsOfFile:encoding:error:", id, path, id *, err); }
static id date_copy(id self, SEL s, void *z) { return self_copy(self, s, z); }
static id date_earlier(id self, SEL s, id o) { (void)s; return date_ref(self) <= date_ref(o) ? self : o; }
static id date_later(id self, SEL s, id o) { (void)s; return date_ref(self) >= date_ref(o) ? self : o; }
static rt_bool date_isEqual(id self, SEL s, id o) { (void)s; return o && rt_is_kind_of(o, RT_CLASS(NSDate)) && date_ref(self) == date_ref(o); }
static unsigned long date_hash(id self, SEL s) { (void)s; return (unsigned long)(long long)date_ref(self); }
static id fm_attrsFS(id self, SEL s, id path, id *err) {
    (void)self; (void)s; if (err) *err = NULL; struct statvfs v; if (statvfs(path ? rt_str_utf8(path) : "/", &v)) return NULL;
    id keys[2] = { rt_str("NSFileSystemSize"), rt_str("NSFileSystemFreeSize") };
    id vals[2] = { rt_num_int((long long)v.f_blocks * (long long)v.f_frsize), rt_num_int((long long)v.f_bavail * (long long)v.f_frsize) };
    return objc_autorelease(rt_dict_new(vals, keys, 2));
}
static id fm_attrsItem(id self, SEL s, id path, id *err) {
    (void)self; (void)s; if (err) *err = NULL; struct stat st; if (!path || stat(rt_str_utf8(path), &st)) return NULL;
    id keys[3] = { rt_str("NSFileSize"), rt_str("NSFileType"), rt_str("NSFileModificationDate") };
    id vals[3] = { rt_num_int(st.st_size), rt_str(S_ISDIR(st.st_mode) ? "NSFileTypeDirectory" : "NSFileTypeRegular"), date_with_ref((double)st.st_mtime - kRef1970) };
    return objc_autorelease(rt_dict_new(vals, keys, 3));
}
static rt_bool fm_copy(id self, SEL s, id from, id to, id *err) {
    (void)self; (void)s; if (err) *err = NULL;
    id d = RT_SEND1(id, (id)RT_CLASS(NSData), "dataWithContentsOfFile:", id, from); if (!d) return 0;
    return RT_SEND2(long, d, "writeToFile:atomically:", id, to, long, 1) & 0xff ? 1 : 0;
}
static rt_bool fm_move(id self, SEL s, id from, id to, id *err) { (void)self; (void)s; if (err) *err = NULL; return rename(rt_str_utf8(from), rt_str_utf8(to)) == 0; }
static id fm_urls(id self, SEL s, unsigned long dir, unsigned long mask) {
    (void)self; (void)s; extern id iosrt_NSSearchPathForDirectoriesInDomains(unsigned long, unsigned long, long);
    id paths = iosrt_NSSearchPathForDirectoriesInDomains(dir, mask, 1);
    id u = RT_SEND1(id, (id)RT_CLASS(NSURL), "fileURLWithPath:", id, rt_array_at(paths, 0));
    return objc_autorelease(rt_array_new(&u, 1));
}
static id fm_currentDir(id self, SEL s) { (void)self; (void)s; char b[4096]; return rt_str(getcwd(b, sizeof b) ? b : "/"); }
static rt_bool fm_isWritable(id self, SEL s, id path) { (void)self; (void)s; return path && access(rt_str_utf8(path), W_OK) == 0; }
static id fm_enumerator(id self, SEL s, id path) { return RT_SEND0(id, RT_SEND2(id, self, "contentsOfDirectoryAtPath:error:", id, path, id *, NULL), "objectEnumerator"); (void)s; }
static id ud_dictRep(id self, SEL s) { (void)s; id d = RT_SEND1(id, self, "valueForKey:", id, rt_str("__dict")); return d; }
static id ud_valueForKey(id self, SEL s, id k) { (void)s; return RT_SEND1(id, self, "objectForKey:", id, k); }
static void ud_setValueForKey(id self, SEL s, id v, id k) { (void)s; RT_SEND2(void, self, "setObject:forKey:", id, v, id, k); }
static id url_setResource(id self, SEL s, id v, id k, id *err) { (void)self; (void)s; (void)v; (void)k; if (err) *err = NULL; return (id)1; }
static id url_getResource(id self, SEL s, id *v, id k, id *err) { (void)self; (void)s; (void)k; if (v) *v = NULL; if (err) *err = NULL; return (id)1; }

/* ============================================================== URL loading stubs */
struct nsurlreq { Class isa; id url; id method; id headers; id body; double timeout; };
static id rq_withURL(Class c, SEL s, id url) { (void)s; id o = rt_alloc_instance(c, 0); struct nsurlreq *r = (struct nsurlreq *)o; r->url = objc_retain(url); r->method = objc_retain(rt_str("GET")); r->headers = rt_dict_new(NULL, NULL, 0); r->timeout = 60; return objc_autorelease(o); }
static id rq_withURLc(Class c, SEL s, id url, unsigned long pol, double t) { (void)pol; id r = rq_withURL(c, s, url); ((struct nsurlreq *)r)->timeout = t; return r; }
static id rq_initWithURL(id self, SEL s, id url) { (void)s; struct nsurlreq *r = (struct nsurlreq *)self; r->url = objc_retain(url); r->method = objc_retain(rt_str("GET")); r->headers = rt_dict_new(NULL, NULL, 0); return self; }
static id rq_url(id self, SEL s) { (void)s; return ((struct nsurlreq *)self)->url; }
static id rq_method(id self, SEL s) { (void)s; return ((struct nsurlreq *)self)->method; }
static void rq_setMethod(id self, SEL s, id m) { (void)s; struct nsurlreq *r = (struct nsurlreq *)self; objc_retain(m); objc_release(r->method); r->method = m; }
static void rq_setBody(id self, SEL s, id b) { (void)s; struct nsurlreq *r = (struct nsurlreq *)self; objc_retain(b); objc_release(r->body); r->body = b; }
static id rq_body(id self, SEL s) { (void)s; return ((struct nsurlreq *)self)->body; }
static void rq_setHeader(id self, SEL s, id v, id k) { (void)s; struct nsurlreq *r = (struct nsurlreq *)self; if (k && v) rt_dict_set(r->headers, k, v); }
static id rq_headers(id self, SEL s) { (void)s; return ((struct nsurlreq *)self)->headers; }
static void rq_setTimeout(id self, SEL s, double t) { (void)s; ((struct nsurlreq *)self)->timeout = t; }
static id g_session, g_cookies, g_urlcache;
static id ses_shared(Class c, SEL s) { (void)s; return singleton(c, &g_session); }
static id ses_withConfig(Class c, SEL s, id cfg) { (void)cfg; return ses_shared(c, s); }
static id ses_withConfigDelegate(Class c, SEL s, id cfg, id del, id q) { (void)cfg; (void)del; (void)q; return ses_shared(c, s); }
/* Networking is not implemented: tasks complete immediately with an error (offline). */
static void ses_fail_async(void *blk) {
    if (!blk) return;
    id err = RT_SEND2(id, (id)rt_class_named("NSError"), "errorWithDomain:code:userInfo:", id, rt_str("NSURLErrorDomain"), long, -1009);
    ((void (*)(void *, id, id, id))((struct rt_block *)blk)->invoke)(blk, NULL, NULL, err);
}
struct ses_task { Class isa; void *blk; long started; };
static void task_resume(id self, SEL s) {
    (void)s; struct ses_task *t = (struct ses_task *)self; if (t->started) return; t->started = 1;
    void *b = t->blk; t->blk = NULL;
    if (b) { ses_fail_async(b); _Block_release(b); }
}
static void task_cancel(id self, SEL s) { (void)self; (void)s; }
static id ses_task_make(void *blk) { id o = rt_alloc_instance(RT_CLASS(NSObject), sizeof(struct ses_task) - sizeof(void *)); ((struct ses_task *)o)->blk = blk ? _Block_copy(blk) : NULL; return objc_autorelease(o); }
static id ses_dataTaskReq(id self, SEL s, id req, void *blk) { (void)self; (void)s; (void)req; return ses_task_make(blk); }
static id ses_cfg(Class c, SEL s) { (void)s; return objc_autorelease(rt_alloc_instance(c, 0)); }
static id ck_shared(Class c, SEL s) { (void)s; return singleton(c, &g_cookies); }
static id ck_cookies(id self, SEL s) { (void)self; (void)s; return objc_autorelease(rt_array_new(NULL, 0)); }
static id uc_shared(Class c, SEL s) { (void)s; return singleton(c, &g_urlcache); }

/* ===================================================================== NSOrderedSet */
static id os_make(Class c, SEL s) { (void)s; return RT_SEND0(id, RT_SEND0(id, (id)c, "alloc"), "init"); }

void rt_foundation_constants_init(void);
void rt_foundation_more2_init(void) {
    Class o = RT_CLASS(NSObject);
    rt_foundation_constants_init();
    NEWCLASS(NSLocale, o, 0);
    Class lo = RT_CLASS(NSLocale);
    CM(lo, "currentLocale", lo_current); CM(lo, "systemLocale", lo_current); CM(lo, "autoupdatingCurrentLocale", lo_current);
    CM(lo, "localeWithLocaleIdentifier:", lo_withId); CM(lo, "preferredLanguages", lo_preferredLanguages);
    IM(lo, "initWithLocaleIdentifier:", lo_initWithId); IM(lo, "localeIdentifier", lo_ident); IM(lo, "objectForKey:", lo_objectForKey);
    IM(lo, "displayNameForKey:value:", lo_displayName);
    NEWCLASS(NSTimeZone, o, 0);
    Class tz = RT_CLASS(NSTimeZone);
    CM(tz, "localTimeZone", tz_local); CM(tz, "systemTimeZone", tz_local); CM(tz, "defaultTimeZone", tz_local);
    CM(tz, "timeZoneWithName:", tz_withName); CM(tz, "timeZoneWithAbbreviation:", tz_withName); CM(tz, "timeZoneForSecondsFromGMT:", tz_withOffset);
    IM(tz, "secondsFromGMT", tz_secondsFromGMT); IM(tz, "secondsFromGMTForDate:", tz_secondsFromGMTForDate);
    IM(tz, "name", tz_name); IM(tz, "abbreviation", tz_abbr); IM(tz, "isDaylightSavingTime", tz_isDST);
    NEWCLASS(NSCalendar, o, 0);
    Class cal = RT_CLASS(NSCalendar);
    CM(cal, "currentCalendar", cal_current); CM(cal, "autoupdatingCurrentCalendar", cal_current);
    IM(cal, "initWithCalendarIdentifier:", cal_initWithId); IM(cal, "components:fromDate:", cal_components);
    IM(cal, "dateFromComponents:", cal_dateFromComponents); IM(cal, "dateByAddingComponents:toDate:options:", cal_dateByAdding);
    IM(cal, "component:fromDate:", cal_component); IM(cal, "calendarIdentifier", cal_ident);
    IM(cal, "setTimeZone:", noop_obj); IM(cal, "setLocale:", noop_obj); IM(cal, "setFirstWeekday:", noop_long);
    NEWCLASS(NSDateComponents, o, sizeof(struct nsdc) - sizeof(void *));
    Class dc = RT_CLASS(NSDateComponents);
    IM(dc, "init", dc_init);
#define DCREG(n, N) IM(dc, #n, dc_get_##n); IM(dc, "set" #N ":", dc_set_##n);
    DCREG(era, Era) DCREG(year, Year) DCREG(month, Month) DCREG(day, Day) DCREG(hour, Hour) DCREG(minute, Minute)
    DCREG(second, Second) DCREG(nanosecond, Nanosecond) DCREG(weekday, Weekday) DCREG(week, Week)
    NEWCLASS(NSFormatter, o, sizeof(struct nsfmt) - sizeof(void *));
    NEWCLASS(NSDateFormatter, RT_CLASS(NSFormatter), 0);
    NEWCLASS(NSNumberFormatter, RT_CLASS(NSFormatter), 0);
    Class df = RT_CLASS(NSDateFormatter), nf = RT_CLASS(NSNumberFormatter);
    IM(df, "setDateFormat:", df_setFormat); IM(df, "dateFormat", df_format); IM(df, "setDateStyle:", df_setStyle);
    IM(df, "setTimeStyle:", df_setTimeStyle); IM(df, "stringFromDate:", df_stringFromDate); IM(df, "dateFromString:", df_dateFromString);
    IM(df, "setLocale:", noop_obj); IM(df, "setTimeZone:", noop_obj); IM(df, "setCalendar:", noop_obj);
    IM(df, "setFormatterBehavior:", noop_long); IM(df, "setDoesRelativeDateFormatting:", noop_long);
    IM(nf, "stringFromNumber:", nf_stringFromNumber); IM(nf, "numberFromString:", nf_numberFromString);
    IM(nf, "setMaximumFractionDigits:", nf_setMax); IM(nf, "setMinimumFractionDigits:", nf_setMin);
    IM(nf, "setNumberStyle:", noop_long); IM(nf, "setLocale:", noop_obj); IM(nf, "setMinimumIntegerDigits:", noop_long);
    IM(nf, "setPositiveFormat:", noop_obj); IM(nf, "setGroupingSeparator:", noop_obj); IM(nf, "setUsesGroupingSeparator:", noop_long);

    NEWCLASS(NSCoder, o, 0);
    Class cd = RT_CLASS(NSCoder);
    IM(cd, "decodeObjectForKey:", coder_nil); IM(cd, "decodeObjectOfClass:forKey:", coder_nil);
    IM(cd, "decodeIntegerForKey:", coder_zero); IM(cd, "decodeIntForKey:", coder_zero); IM(cd, "decodeInt32ForKey:", coder_zero);
    IM(cd, "decodeInt64ForKey:", coder_zero); IM(cd, "decodeBoolForKey:", coder_no); IM(cd, "decodeDoubleForKey:", coder_zerod);
    IM(cd, "decodeFloatForKey:", coder_zerod); IM(cd, "containsValueForKey:", coder_no); IM(cd, "allowsKeyedCoding", coder_no);
    IM(cd, "encodeObject:forKey:", coder_encode); IM(cd, "encodeInteger:forKey:", coder_encode); IM(cd, "encodeBool:forKey:", coder_encode);
    IM(cd, "encodeDouble:forKey:", coder_encode); IM(cd, "encodeInt:forKey:", coder_encode); IM(cd, "encodeFloat:forKey:", coder_encode);
    NEWCLASS(NSKeyedArchiver, cd, 0);
    NEWCLASS(NSKeyedUnarchiver, cd, 0);
    CM(RT_CLASS(NSKeyedArchiver), "archivedDataWithRootObject:", ka_archivedData); CM(RT_CLASS(NSKeyedArchiver), "archiveRootObject:toFile:", ka_archiveToFile);
    CM(RT_CLASS(NSKeyedUnarchiver), "unarchiveObjectWithData:", ku_unarchive); CM(RT_CLASS(NSKeyedUnarchiver), "unarchiveObjectWithFile:", ku_unarchive);

    NEWCLASS(NSJSONSerialization, o, 0);
    CM(RT_CLASS(NSJSONSerialization), "JSONObjectWithData:options:error:", js_parse);
    CM(RT_CLASS(NSJSONSerialization), "dataWithJSONObject:options:error:", js_write);
    CM(RT_CLASS(NSJSONSerialization), "isValidJSONObject:", js_valid);
    NEWCLASS(NSPropertyListSerialization, o, 0);
    CM(RT_CLASS(NSPropertyListSerialization), "propertyListWithData:options:format:error:", pl_parse);
    CM(RT_CLASS(NSPropertyListSerialization), "propertyListFromData:mutabilityOption:format:errorDescription:", pl_parseOld);
    CM(RT_CLASS(NSPropertyListSerialization), "dataWithPropertyList:format:options:error:", pl_data);
    IM(RT_CLASS(NSDictionary), "writeToFile:atomically:", coll_writeToFile); IM(RT_CLASS(NSArray), "writeToFile:atomically:", coll_writeToFile);
    CM(RT_CLASS(NSDictionary), "dictionaryWithContentsOfFile:", coll_withContentsOfFile); CM(RT_CLASS(NSArray), "arrayWithContentsOfFile:", coll_withContentsOfFile);

    NEWCLASS(NSScanner, o, sizeof(struct nsscan) - sizeof(void *));
    Class sc = RT_CLASS(NSScanner);
    CM(sc, "scannerWithString:", sc_make); IM(sc, "initWithString:", sc_init); IM(sc, "scanInt:", sc_int); IM(sc, "scanInteger:", sc_integer);
    IM(sc, "scanDouble:", sc_double); IM(sc, "scanFloat:", sc_float); IM(sc, "scanHexInt:", sc_hex); IM(sc, "scanString:intoString:", sc_string);
    IM(sc, "scanUpToString:intoString:", sc_upTo); IM(sc, "isAtEnd", sc_atEnd); IM(sc, "scanLocation", sc_loc); IM(sc, "setScanLocation:", sc_setLoc);
    IM(sc, "setCharactersToBeSkipped:", noop_obj);

    NEWCLASS(NSSortDescriptor, o, sizeof(struct nssd) - sizeof(void *));
    Class sd = RT_CLASS(NSSortDescriptor);
    CM(sd, "sortDescriptorWithKey:ascending:", sd_make); CM(sd, "sortDescriptorWithKey:ascending:selector:", sd_makeSel);
    IM(sd, "initWithKey:ascending:", sd_init); IM(sd, "key", sd_key); IM(sd, "ascending", sd_asc); IM(sd, "compareObject:toObject:", sd_compare);
    IM(RT_CLASS(NSArray), "sortedArrayUsingDescriptors:", arr_sortedDescs); IM(RT_CLASS(NSMutableArray), "sortUsingDescriptors:", marr_sortDescs);

    NEWCLASS(NSAttributedString, o, sizeof(struct nsas) - sizeof(void *));
    NEWCLASS(NSMutableAttributedString, RT_CLASS(NSAttributedString), 0);
    Class as = RT_CLASS(NSAttributedString), mas = RT_CLASS(NSMutableAttributedString);
    IM(as, "init", as_init); IM(as, "initWithString:", as_initStr); IM(as, "initWithString:attributes:", as_initStrAttrs);
    IM(as, "string", as_string); IM(as, "length", as_length); IM(as, "attributesAtIndex:effectiveRange:", as_attrsAt);
    IM(as, "attribute:atIndex:effectiveRange:", as_attrAt); IM(as, "copyWithZone:", as_copy); IM(as, "mutableCopyWithZone:", as_mcopy);
    IM(as, "description", as_desc);
    IM(mas, "appendAttributedString:", as_append); IM(mas, "addAttribute:value:range:", as_addAttr); IM(mas, "addAttributes:range:", as_addAttrs);
    IM(mas, "setAttributes:range:", as_addAttrs); IM(mas, "replaceCharactersInRange:withString:", as_replace); IM(mas, "mutableString", as_string);
    IM(mas, "beginEditing", noop_obj); IM(mas, "endEditing", noop_obj);

    NEWCLASS(NSFileHandle, o, sizeof(struct nsfh) - sizeof(void *));
    Class fh = RT_CLASS(NSFileHandle);
    CM(fh, "fileHandleForReadingAtPath:", fh_read); CM(fh, "fileHandleForWritingAtPath:", fh_write); CM(fh, "fileHandleForUpdatingAtPath:", fh_update);
    CM(fh, "fileHandleWithStandardOutput", fh_stdout); CM(fh, "fileHandleWithStandardError", fh_stderr);
    IM(fh, "readDataToEndOfFile", fh_readToEnd); IM(fh, "readDataOfLength:", fh_readLen); IM(fh, "writeData:", fh_writeData);
    IM(fh, "seekToEndOfFile", fh_seekEnd); IM(fh, "seekToFileOffset:", fh_seek); IM(fh, "offsetInFile", fh_offset);
    IM(fh, "truncateFileAtOffset:", fh_truncate); IM(fh, "closeFile", fh_close); IM(fh, "synchronizeFile", fh_sync); IM(fh, "dealloc", fh_dealloc);

    NEWCLASS(NSOrderedSet, RT_CLASS(NSArray), 0);
    NEWCLASS(NSMutableOrderedSet, RT_CLASS(NSMutableArray), 0);
    CM(RT_CLASS(NSOrderedSet), "orderedSet", os_make); CM(RT_CLASS(NSMutableOrderedSet), "orderedSet", os_make);

    NEWCLASS(NSURLRequest, o, sizeof(struct nsurlreq) - sizeof(void *));
    NEWCLASS(NSMutableURLRequest, RT_CLASS(NSURLRequest), 0);
    Class rq = RT_CLASS(NSURLRequest), mrq = RT_CLASS(NSMutableURLRequest);
    CM(rq, "requestWithURL:", rq_withURL); CM(rq, "requestWithURL:cachePolicy:timeoutInterval:", rq_withURLc);
    IM(rq, "initWithURL:", rq_initWithURL); IM(rq, "URL", rq_url); IM(rq, "HTTPMethod", rq_method); IM(rq, "HTTPBody", rq_body);
    IM(rq, "allHTTPHeaderFields", rq_headers);
    IM(mrq, "setHTTPMethod:", rq_setMethod); IM(mrq, "setHTTPBody:", rq_setBody); IM(mrq, "setValue:forHTTPHeaderField:", rq_setHeader);
    IM(mrq, "addValue:forHTTPHeaderField:", rq_setHeader); IM(mrq, "setTimeoutInterval:", rq_setTimeout); IM(mrq, "setCachePolicy:", noop_long);
    NEWCLASS(NSURLSession, o, 0);
    NEWCLASS(NSURLSessionConfiguration, o, 0);
    Class ses = RT_CLASS(NSURLSession), cfg = RT_CLASS(NSURLSessionConfiguration);
    CM(ses, "sharedSession", ses_shared); CM(ses, "sessionWithConfiguration:", ses_withConfig);
    CM(ses, "sessionWithConfiguration:delegate:delegateQueue:", ses_withConfigDelegate);
    IM(ses, "dataTaskWithRequest:completionHandler:", ses_dataTaskReq); IM(ses, "dataTaskWithURL:completionHandler:", ses_dataTaskReq);
    IM(ses, "uploadTaskWithRequest:fromData:completionHandler:", ses_dataTaskReq);
    IM(ses, "invalidateAndCancel", noop_obj); IM(ses, "finishTasksAndInvalidate", noop_obj);
    IM(o, "resume", task_resume); IM(o, "cancel", task_cancel);   /* minimal task protocol on the anonymous task objects */
    CM(cfg, "defaultSessionConfiguration", ses_cfg); CM(cfg, "ephemeralSessionConfiguration", ses_cfg);
    CM(cfg, "backgroundSessionConfigurationWithIdentifier:", ses_cfg);
    IM(cfg, "setTimeoutIntervalForRequest:", noop_long); IM(cfg, "setHTTPAdditionalHeaders:", noop_obj);
    IM(cfg, "setURLCache:", noop_obj); IM(cfg, "setRequestCachePolicy:", noop_long); IM(cfg, "setHTTPMaximumConnectionsPerHost:", noop_long);
    NEWCLASS(NSHTTPCookieStorage, o, 0);
    CM(RT_CLASS(NSHTTPCookieStorage), "sharedHTTPCookieStorage", ck_shared); IM(RT_CLASS(NSHTTPCookieStorage), "cookies", ck_cookies);
    IM(RT_CLASS(NSHTTPCookieStorage), "cookiesForURL:", ck_cookies); IM(RT_CLASS(NSHTTPCookieStorage), "setCookieAcceptPolicy:", noop_long);
    NEWCLASS(NSURLCache, o, 0);
    CM(RT_CLASS(NSURLCache), "sharedURLCache", uc_shared); CM(RT_CLASS(NSURLCache), "setSharedURLCache:", noop_obj);
    IM(RT_CLASS(NSURLCache), "removeAllCachedResponses", noop_obj);

    /* additions to existing classes */
    extern char iosrt_imp_setWithObjects[];
    CM(RT_CLASS(NSSet), "setWithObjects:", iosrt_imp_setWithObjects);
    extern char iosrt_imp_setInitWithObjects[];
    IM(RT_CLASS(NSSet), "initWithObjects:", iosrt_imp_setInitWithObjects); IM(RT_CLASS(NSSet), "initWithArray:", set_initWithArray);
    CM(RT_CLASS(NSSet), "setWithSet:", set_withSet);
    IM(RT_CLASS(NSSet), "setByAddingObject:", set_byAdding); IM(RT_CLASS(NSSet), "setByAddingObjectsFromArray:", set_byAddingArray);
    CM(RT_CLASS(NSTimeZone), "resetSystemTimeZone", tz_reset);
    CM(rt_class_named("NSBundle"), "bundleWithPath:", bundle_withPath); CM(rt_class_named("NSBundle"), "bundleWithIdentifier:", bundle_withPath);
    IM(RT_CLASS(NSData), "writeToFile:options:error:", data_writeOpts); IM(RT_CLASS(NSData), "writeToURL:atomically:", data_writeURL);
    CM(RT_CLASS(NSData), "dataWithContentsOfURL:", data_withContentsOfURL); CM(RT_CLASS(NSData), "dataWithContentsOfFile:options:error:", data_withContentsOfFileOpts);
    IM(RT_CLASS(NSData), "base64EncodedStringWithOptions:", data_base64);
    Class st = RT_CLASS(NSString);
    IM(st, "fileSystemRepresentation", str_fsrep); IM(st, "getFileSystemRepresentation:maxLength:", str_getFSRep);
    IM(st, "isAbsolutePath", str_isAbsolute); IM(st, "stringByExpandingTildeInPath", str_expandTilde);
    IM(st, "stringByStandardizingPath", str_expandTilde); IM(st, "stringByResolvingSymlinksInPath", str_expandTilde);
    IM(st, "pathComponents", str_pathComponents); IM(st, "stringByAddingPercentEscapesUsingEncoding:", str_percentEscapes);
    IM(st, "stringByAddingPercentEncodingWithAllowedCharacters:", str_percentAllowed);
    IM(st, "stringByRemovingPercentEncoding", str_removePercent); IM(st, "stringByReplacingPercentEscapesUsingEncoding:", str_removePercent);
    IM(st, "stringByPaddingToLength:withString:startingAtIndex:", str_padding);
    IM(st, "componentsSeparatedByCharactersInSet:", str_componentsBySet); IM(st, "rangeOfString:options:range:", str_rangeOptsRange);
    IM(st, "lengthOfBytesUsingEncoding:", str_lengthOfBytesUTF8); IM(st, "localizedCaseInsensitiveCompare:", str_localizedCaseInsensitiveCompare);
    IM(st, "initWithData:encoding:", str_initWithData); IM(st, "initWithContentsOfFile:encoding:error:", str_initWithContentsOfFile);
    CM(st, "stringWithContentsOfFile:usedEncoding:error:", str_stringWithContentsOfFileUsed);
    Class dt = RT_CLASS(NSDate);
    IM(dt, "copyWithZone:", date_copy); IM(dt, "earlierDate:", date_earlier); IM(dt, "laterDate:", date_later);
    IM(dt, "isEqual:", date_isEqual); IM(dt, "isEqualToDate:", date_isEqual); IM(dt, "hash", date_hash);
    Class fm = RT_CLASS(NSFileManager);
    IM(fm, "attributesOfFileSystemForPath:error:", fm_attrsFS); IM(fm, "attributesOfItemAtPath:error:", fm_attrsItem);
    IM(fm, "copyItemAtPath:toPath:error:", fm_copy); IM(fm, "moveItemAtPath:toPath:error:", fm_move);
    IM(fm, "URLsForDirectory:inDomains:", fm_urls); IM(fm, "currentDirectoryPath", fm_currentDir);
    IM(fm, "isWritableFileAtPath:", fm_isWritable); IM(fm, "isReadableFileAtPath:", fm_isWritable); IM(fm, "isDeletableFileAtPath:", fm_isWritable);
    IM(fm, "enumeratorAtPath:", fm_enumerator); IM(fm, "setAttributes:ofItemAtPath:error:", yes_err);
    Class ud = RT_CLASS(NSUserDefaults);
    IM(ud, "valueForKey:", ud_valueForKey); IM(ud, "setValue:forKey:", ud_setValueForKey); (void)ud_dictRep;
    IM(RT_CLASS(NSURL), "setResourceValue:forKey:error:", url_setResource); IM(RT_CLASS(NSURL), "getResourceValue:forKey:error:", url_getResource);
}

/* string constants exported as data symbols */
#define STRCONSTS X(NSCocoaErrorDomain) X(NSURLErrorDomain) X(NSPOSIXErrorDomain) X(NSOSStatusErrorDomain) X(NSMachErrorDomain) \
    X(NSLocalizedDescriptionKey) X(NSUnderlyingErrorKey) X(NSFileType) X(NSFileSize) X(NSFileModificationDate) X(NSFileTypeDirectory) \
    X(NSFileTypeRegular) X(NSFileSystemFreeSize) X(NSFileSystemSize) X(NSURLIsExcludedFromBackupKey) X(NSFileProtectionKey) \
    X(NSFileProtectionNone) X(NSLocaleDecimalSeparator) X(NSLocaleCountryCode) X(NSLocaleLanguageCode) X(NSLocaleIdentifier) \
    X(NSLocaleCurrencyCode) X(NSMallocException)
#define X(n) id iosrt_##n;
STRCONSTS
#undef X
void rt_foundation_constants_init(void) {
#define X(n) iosrt_##n = rt_str_new(#n);
    STRCONSTS
#undef X
}
