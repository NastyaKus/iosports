/* Demo.bundle (MH_BUNDLE): loaded at run time through NSBundle and dlopen */
#import <Foundation/Foundation.h>
#include <stdio.h>
int plugin_value(void) { return 1234; }
__attribute__((constructor)) static void plugin_init(void) { printf("plugin: initializer\n"); }
@interface DemoPlugin : NSObject
- (NSString *)name;
@end
@implementation DemoPlugin
- (NSString *)name { return @"DemoPlugin"; }
@end
