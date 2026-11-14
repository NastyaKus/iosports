/* dylib_demo: an app that links its own framework (which links a dylib through
 * @rpath) and loads a plug-in bundle at run time. */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>

extern int demokit_counter;
int demokit_add(int a, int b);
int demokit_helper(void);
int demokit_bump(void);
int main_bump(void);

typedef struct { const char *dli_fname; void *dli_fbase; const char *dli_sname; void *dli_saddr; } Dl_info;
int dladdr(const void *, Dl_info *);
void *dlopen(const char *, int);
void *dlsym(void *, const char *);
unsigned _dyld_image_count(void);

@interface DKGreeter : NSObject
+ (NSString *)kitName;
- (NSString *)greeting;
@end
@interface NSString (DemoKit)
- (NSString *)dk_shout;
@end

/* an app class whose superclass lives in the framework */
@interface Fancy : DKGreeter
@end
@implementation Fancy
+ (void)load { printf("main: +[Fancy load]\n"); }
- (NSString *)greeting { return [NSString stringWithFormat:@"%@ (fancy)", [super greeting]]; }
@end

static const char *base(const char *p) { const char *s = p ? strrchr(p, '/') : NULL; return s ? s + 1 : (p ? p : "(null)"); }

int main(int argc, char **argv) {
    @autoreleasepool {
        int sum = demokit_add(2, 3);
        printf("main: demokit_add(2,3)=%d counter=%d\n", sum, demokit_counter);
        printf("main: helper through DemoKit=%d\n", demokit_helper());
        printf("main: %s\n", [[[Fancy new] greeting] UTF8String]);
        printf("main: kit name=%s\n", [[DKGreeter kitName] UTF8String]);
        printf("main: %s\n", [[@"hello" dk_shout] UTF8String]);
        int a = main_bump(), b = demokit_bump();
        printf("main: shared inline counter %d %d\n", a, b);
        Dl_info info;
        if (dladdr((const void *)demokit_add, &info)) printf("main: dladdr -> %s %s\n", base(info.dli_fname), info.dli_sname);
        printf("main: images=%u\n", _dyld_image_count());

        NSBundle *kit = [NSBundle bundleWithIdentifier:@"org.iosrt.demokit"];
        printf("main: bundleWithIdentifier -> %s\n", kit ? [[[kit bundlePath] lastPathComponent] UTF8String] : "nil");

        NSString *pp = [[[NSBundle mainBundle] builtInPlugInsPath] stringByAppendingPathComponent:@"Demo.bundle"];
        NSBundle *pb = [NSBundle bundleWithPath:pp];
        printf("main: plugin loaded=%d\n", (int)[pb isLoaded]);
        Class pc = [pb principalClass];
        printf("main: principal class %s says %s, loaded=%d\n", [NSStringFromClass(pc) UTF8String],
               [[[pc new] performSelector:@selector(name)] UTF8String], (int)[pb isLoaded]);
        void *h = dlopen("@executable_path/PlugIns/Demo.bundle/Demo", 2);
        int (*pv)(void) = h ? (int (*)(void))dlsym(h, "plugin_value") : NULL;
        printf("main: dlsym plugin_value -> %d\n", pv ? pv() : -1);
        printf("main: images=%u\n", _dyld_image_count());
    }
    return 0;
}
