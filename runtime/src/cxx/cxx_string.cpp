// iosrt C++ support, part 1: the template-based pieces of Apple's libc++
// (std::__1, alternate string layout), instantiated from libc++ headers with
// the configuration in include/__config_site. Exported to guests through the
// generated exports/cxx_gen.def.
#include <string>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>

extern "C" void rt_cxx_fatal(const char *what);

#include <cstdarg>
namespace std { inline namespace __1 {

// libc++ hardening / -fno-exceptions error path
void __libcpp_verbose_abort(char const *fmt, ...) {
    char b[512]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    rt_cxx_fatal(b);
    abort();
}

// out-of-line members that Apple's libc++.dylib exports
template class basic_string<char>;
template class basic_string<wchar_t>;

static string fmt_u(unsigned long long v) { char b[32]; snprintf(b, sizeof b, "%llu", v); return string(b); }
static string fmt_s(long long v) { char b[32]; snprintf(b, sizeof b, "%lld", v); return string(b); }
string to_string(int v) { return fmt_s(v); }
string to_string(long v) { return fmt_s(v); }
string to_string(long long v) { return fmt_s(v); }
string to_string(unsigned v) { return fmt_u(v); }
string to_string(unsigned long v) { return fmt_u(v); }
string to_string(unsigned long long v) { return fmt_u(v); }
string to_string(double v) { char b[64]; snprintf(b, sizeof b, "%f", v); return string(b); }
string to_string(float v) { return to_string((double)v); }

// smallest prime >= n (hash table bucket counts)
size_t __next_prime(size_t n) {
    if (n <= 2) return 2;
    for (size_t c = n | 1;; c += 2) {
        bool prime = true;
        for (size_t d = 3; d * d <= c; d += 2) if (c % d == 0) { prime = false; break; }
        if (prime) return c;
    }
}

void __throw_system_error(int ev, const char *what) {
    char b[512]; snprintf(b, sizeof b, "std::system_error(%d): %s", ev, what ? what : "");
    rt_cxx_fatal(b);
    abort();
}

} }  // namespace std::__1

// std::runtime_error(const std::__1::string&): libc++abi only has the const char* form
extern "C" void iosrt_runtime_error_from_string(void *self, const std::string &s)
    __asm__("_ZNSt13runtime_errorC2ERKNSt3__112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEE");
extern "C" void iosrt_runtime_error_from_string(void *self, const std::string &s) { new (self) std::runtime_error(s.c_str()); }
extern "C" void iosrt_runtime_error_from_string1(void *self, const std::string &s)
    __asm__("_ZNSt13runtime_errorC1ERKNSt3__112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEE");
extern "C" void iosrt_runtime_error_from_string1(void *self, const std::string &s) { new (self) std::runtime_error(s.c_str()); }
extern "C" void iosrt_logic_error_from_string(void *self, const std::string &s)
    __asm__("_ZNSt11logic_errorC2ERKNSt3__112basic_stringIcNS0_11char_traitsIcEENS0_9allocatorIcEEEE");
extern "C" void iosrt_logic_error_from_string(void *self, const std::string &s) { new (self) std::logic_error(s.c_str()); }
