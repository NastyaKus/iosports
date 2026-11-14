// kvc_demo: key-value coding with struct values (NSValue frames, points,
// transforms) and scalars on UIKit's own properties, UIView legacy animation
// delegates without explicit selectors, and inserting next to a sibling that
// lives in another view.
#import <UIKit/UIKit.h>
#include <stdio.h>

@interface Watcher : NSObject
@end
@implementation Watcher
- (void)animationWillStart:(NSString *)name context:(void *)ctx { printf("animationWillStart %s\n", name.UTF8String); }
- (void)animationDidStop:(NSString *)name finished:(NSNumber *)done context:(void *)ctx {
    printf("animationDidStop %s finished %d\n", name.UTF8String, done.boolValue);
    exit(0);
}
@end

@interface AppDelegate : UIResponder <UIApplicationDelegate>
@property (strong) UIWindow *window;
@property (strong) Watcher *watcher;
@end
@implementation AppDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)opts {
    self.window = [[UIWindow alloc] initWithFrame:CGRectMake(0, 0, 320, 480)];
    UIViewController *vc = [UIViewController new];
    self.window.rootViewController = vc;
    [self.window makeKeyAndVisible];
    UIView *root = vc.view;

    UIView *v = [UIView new];
    [root addSubview:v];
    [v setValue:[NSValue valueWithCGRect:CGRectMake(10, 20, 100, 50)] forKey:@"frame"];
    CGRect f = v.frame;
    printf("frame via KVC: %g,%g %gx%g\n", f.origin.x, f.origin.y, f.size.width, f.size.height);
    CGPoint c = [[v valueForKey:@"center"] CGPointValue];
    printf("center via KVC: %g,%g\n", c.x, c.y);
    [v setValue:@0.5 forKey:@"alpha"];
    [v setValue:@YES forKey:@"hidden"];
    [v setValue:@7 forKey:@"tag"];
    printf("alpha %.2f hidden %d tag %ld\n", v.alpha, v.hidden, (long)v.tag);
    [v.layer setValue:@0.25f forKey:@"opacity"];
    printf("layer opacity %.2f, alpha now %.2f\n", v.layer.opacity, v.alpha);
    [v.layer setValue:[NSValue valueWithCGPoint:CGPointMake(200, 300)] forKey:@"position"];
    printf("layer position %g,%g\n", v.layer.position.x, v.layer.position.y);
    CGRect b = [[v.layer valueForKey:@"bounds"] CGRectValue];
    printf("layer bounds %gx%g\n", b.size.width, b.size.height);

    // a sibling in another superview: the new view goes on top, like -addSubview:
    UIView *back = [UIView new], *other = [UIView new], *mine = [UIView new];
    [root addSubview:back];
    [self.window addSubview:other];
    [root insertSubview:mine belowSubview:other];
    printf("inserted below a foreign sibling: index %lu of %lu\n", (unsigned long)[root.subviews indexOfObject:mine], (unsigned long)root.subviews.count);

    // NSValue keys: equal values find each other in a dictionary (apps key per-touch state this way)
    NSMutableDictionary *byTouch = [NSMutableDictionary dictionary];
    byTouch[[NSValue valueWithNonretainedObject:v]] = @"note on";
    printf("lookup by an equal NSValue: %s\n", [byTouch[[NSValue valueWithNonretainedObject:v]] UTF8String]);

    // legacy animation: only a delegate, the default selectors are called
    self.watcher = [Watcher new];
    [UIView beginAnimations:@"fade" context:NULL];
    [UIView setAnimationDuration:0.1];
    [UIView setAnimationDelegate:self.watcher];
    v.alpha = 1;
    [UIView commitAnimations];
    return YES;
}
@end

int main(int argc, char **argv) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, @"AppDelegate"); }
}
