// iosrt C++ support, part 1: glue between the runtime's libc++ build
// (std::__1 with Apple's configuration, tools/build_libcxx.sh) and the rest:
// libc++'s fatal-error hook and the std::logic_error/runtime_error
// constructors from std::__1::string that the NDK's libc++abi does not have.
// Exported to guests through the generated exports/cxx_gen.def.
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

// declared by the NDK's <stdexcept>, defined in no libc++ source we build
void __throw_runtime_error(const char *msg) {
    char b[512]; snprintf(b, sizeof b, "std::runtime_error: %s", msg ? msg : "");
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
