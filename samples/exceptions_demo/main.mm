// exceptions_demo: C++ and Objective-C exceptions unwinding through guest frames
// (compact unwind + LSDA), cleanups, rethrow, and frames of the runtime itself.
#import <Foundation/Foundation.h>
#include <stdexcept>
#include <stdio.h>

static int g_dtors;
struct Guard { const char *name; explicit Guard(const char *n) : name(n) {} ~Guard() { g_dtors++; } };
struct Base { virtual ~Base() {} virtual const char *who() const { return "Base"; } int tag = 1; };
struct Mid : Base { const char *who() const override { return "Mid"; } };
struct Leaf : Mid { const char *who() const override { return "Leaf"; } int extra = 42; };
struct Other { virtual ~Other() {} int o = 7; };
struct Multi : Other, Base { const char *who() const override { return "Multi"; } };

__attribute__((noinline)) static void deep(int n, int what) {
    Guard g("deep");
    if (n > 0) { deep(n - 1, what); return; }
    switch (what) {
    case 0: throw 7;
    case 1: throw Leaf();
    case 2: throw std::runtime_error("boom");
    case 3: { static Leaf leaf; throw &leaf; }
    case 4: throw Multi();
    default: throw 3.5;
    }
}

int main(int argc, char **argv) {
    @autoreleasepool {
        // 1. int through 5 frames with cleanups
        g_dtors = 0;
        try { deep(4, 0); } catch (int v) { printf("1 caught int %d, dtors=%d\n", v, g_dtors); }
        // 2. derived object caught as a base reference
        try { deep(2, 1); } catch (const Base &b) { printf("2 caught %s via Base&\n", b.who()); }
        // 3. std::runtime_error as std::exception
        try { deep(1, 2); } catch (const std::exception &e) { printf("3 caught std::exception: %s\n", e.what()); }
        // 4. pointer to derived caught as pointer to base
        try { deep(0, 3); } catch (Mid *m) { printf("4 caught %s via Mid*\n", m->who()); }
        // 5. second base of a multiply inherited class (adjusted pointer)
        try { deep(0, 4); } catch (Base &b) { printf("5 caught %s via second base, tag=%d\n", b.who(), b.tag); }
        // 6. no matching clause in the inner frame, catch (...) outside
        try { try { deep(0, 5); } catch (int) { printf("6 wrong handler\n"); } } catch (...) { printf("6 caught (...)\n"); }
        // 7. rethrow from a handler
        try { try { deep(0, 0); } catch (int v) { printf("7 inner %d, rethrowing\n", v); throw; } } catch (int v) { printf("7 outer %d\n", v); }
        // 8. a new exception thrown inside a handler
        try { try { throw 1; } catch (int) { throw std::logic_error("from handler"); } } catch (const std::logic_error &e) { printf("8 caught %s\n", e.what()); }
        // 9. Objective-C @throw / @catch / @finally
        @try { @throw [NSException exceptionWithName:@"DemoException" reason:@"objc throw" userInfo:nil]; }
        @catch (NSException *e) { printf("9 caught %s: %s\n", e.name.UTF8String, e.reason.UTF8String); }
        @finally { printf("9 finally\n"); }
        // 10. raised by the runtime (Foundation code), caught by the app
        @try { [NSException raise:@"RuntimeRaise" format:@"value %d", 5]; }
        @catch (NSException *e) { printf("10 caught %s: %s\n", e.name.UTF8String, e.reason.UTF8String); }
        // 11. thrown inside a block that the runtime calls, caught outside the runtime frame
        @try {
            [@[@1, @2, @3] enumerateObjectsUsingBlock:^(id obj, NSUInteger i, BOOL *stop) {
                if (i == 1) @throw [NSException exceptionWithName:@"InBlock" reason:@"from block" userInfo:nil];
            }];
        } @catch (NSException *e) { printf("11 caught %s through the runtime\n", e.name.UTF8String); }
        // 12. @catch (id) and a nested @try
        @try { @try { @throw @"a string"; } @finally { printf("12 inner finally\n"); } }
        @catch (id o) { printf("12 caught id %s\n", [[o description] UTF8String]); }
        printf("done\n");
    }
    return 0;
}
