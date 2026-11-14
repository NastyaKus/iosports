/* DemoKit.framework: classes, a category, C functions and data used by the app */
#import <Foundation/Foundation.h>
#include <stdio.h>

extern int helper_value;
int helper_twice(int x);

int demokit_counter = 1;
int demokit_add(int a, int b) { demokit_counter++; return a + b; }
int demokit_helper(void) { return helper_twice(helper_value); }

__attribute__((constructor)) static void demokit_init(void) { printf("DemoKit: initializer (helper_value=%d)\n", helper_value); }

@interface DKGreeter : NSObject
+ (NSString *)kitName;
- (NSString *)greeting;
@end
@implementation DKGreeter
+ (void)load { printf("DemoKit: +[DKGreeter load]\n"); }
+ (NSString *)kitName { return [[NSBundle bundleForClass:self] objectForInfoDictionaryKey:@"CFBundleName"]; }
- (NSString *)greeting {
    NSString *p = [[NSBundle bundleForClass:[DKGreeter class]] pathForResource:@"greeting" ofType:@"txt"];
    return p ? [NSString stringWithContentsOfFile:p encoding:NSUTF8StringEncoding error:NULL] : @"(no greeting.txt)";
}
@end

@implementation NSString (DemoKit)
- (NSString *)dk_shout { return [[self uppercaseString] stringByAppendingString:@"!"]; }
@end
