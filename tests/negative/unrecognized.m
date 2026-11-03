// sending an unknown selector must terminate the guest with a clear message
#import <Foundation/Foundation.h>
int main(void) {
    @autoreleasepool {
        id s = @"str";
        NSLog(@"before");
        [s performSelector:NSSelectorFromString(@"noSuchMethod")];
    }
    return 0;
}
