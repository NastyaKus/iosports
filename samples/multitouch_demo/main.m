// multitouch_demo: several fingers at once. A pad with multipleTouchEnabled gets every
// finger (like piano keys), a plain view only the first one, and a pinch recognizer
// follows two fingers. Touches come from the headless script (pdown/pmove/pup:id,x,y).
#import <UIKit/UIKit.h>
#include <stdio.h>

@interface Pad : UIView
@property (copy) NSString *name;
@end
@implementation Pad
- (void)report:(const char *)what touches:(NSSet *)touches event:(UIEvent *)ev {
    for (UITouch *t in touches) {
        CGPoint p = [t locationInView:self];
        printf("%s %s at %g,%g (fingers on %s: %lu, in event: %lu)\n", self.name.UTF8String, what, p.x, p.y,
               self.name.UTF8String, (unsigned long)[ev touchesForView:self].count, (unsigned long)ev.allTouches.count);
    }
}
- (void)touchesBegan:(NSSet *)t withEvent:(UIEvent *)e { [self report:"began" touches:t event:e]; }
- (void)touchesMoved:(NSSet *)t withEvent:(UIEvent *)e { [self report:"moved" touches:t event:e]; }
- (void)touchesEnded:(NSSet *)t withEvent:(UIEvent *)e { [self report:"ended" touches:t event:e]; }
- (void)touchesCancelled:(NSSet *)t withEvent:(UIEvent *)e { [self report:"cancelled" touches:t event:e]; }
@end

@interface AppDelegate : UIResponder <UIApplicationDelegate>
@property (strong) UIWindow *window;
@end
@implementation AppDelegate
- (void)pinched:(UIPinchGestureRecognizer *)g {
    static const char *names[] = { "possible", "began", "changed", "ended", "cancelled", "failed" };
    printf("pinch %s: scale %.2f, fingers %lu\n", names[g.state], g.scale, (unsigned long)g.numberOfTouches);
    if (g.state == UIGestureRecognizerStateEnded) { printf("done\n"); exit(0); }
}
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)opts {
    self.window = [[UIWindow alloc] initWithFrame:CGRectMake(0, 0, 320, 480)];
    UIViewController *vc = [UIViewController new];
    self.window.rootViewController = vc;
    [self.window makeKeyAndVisible];

    Pad *keys = [[Pad alloc] initWithFrame:CGRectMake(0, 0, 320, 100)];
    keys.name = @"keys"; keys.multipleTouchEnabled = YES;
    [vc.view addSubview:keys];

    Pad *single = [[Pad alloc] initWithFrame:CGRectMake(0, 120, 320, 100)];
    single.name = @"single";                 // multipleTouchEnabled defaults to NO
    [vc.view addSubview:single];

    Pad *zoom = [[Pad alloc] initWithFrame:CGRectMake(0, 240, 320, 240)];
    zoom.name = @"zoom"; zoom.multipleTouchEnabled = YES;
    UIPinchGestureRecognizer *pinch = [[UIPinchGestureRecognizer alloc] initWithTarget:self action:@selector(pinched:)];
    [zoom addGestureRecognizer:pinch];
    [vc.view addSubview:zoom];
    return YES;
}
@end

int main(int argc, char **argv) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, @"AppDelegate"); }
}
