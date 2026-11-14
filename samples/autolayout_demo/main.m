// autolayout_demo: constraints made in code (constraintWithItem: and anchors) laid out
// by the runtime's solver; the frames are printed after each layout pass.
#import <UIKit/UIKit.h>
#include <stdio.h>

static void show(const char *name, UIView *v) {
    CGRect f = v.frame;
    printf("%-10s %g,%g %gx%g\n", name, f.origin.x, f.origin.y, f.size.width, f.size.height);
}

@interface AppDelegate : UIResponder <UIApplicationDelegate>
@property (strong) UIWindow *window;
@end
@implementation AppDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)opts {
    self.window = [[UIWindow alloc] initWithFrame:CGRectMake(0, 0, 320, 480)];
    UIViewController *vc = [UIViewController new];
    self.window.rootViewController = vc;
    [self.window makeKeyAndVisible];
    UIView *root = vc.view;

    // a box: 20 from the left and top, width half of the root minus 30, height 100
    UIView *box = [UIView new]; box.translatesAutoresizingMaskIntoConstraints = NO; [root addSubview:box];
    [root addConstraint:[NSLayoutConstraint constraintWithItem:box attribute:NSLayoutAttributeLeft relatedBy:NSLayoutRelationEqual toItem:root attribute:NSLayoutAttributeLeft multiplier:1 constant:20]];
    [root addConstraint:[NSLayoutConstraint constraintWithItem:box attribute:NSLayoutAttributeTop relatedBy:NSLayoutRelationEqual toItem:root attribute:NSLayoutAttributeTop multiplier:1 constant:20]];
    [root addConstraint:[NSLayoutConstraint constraintWithItem:box attribute:NSLayoutAttributeWidth relatedBy:NSLayoutRelationEqual toItem:root attribute:NSLayoutAttributeWidth multiplier:0.5 constant:-30]];
    NSLayoutConstraint *boxH = [NSLayoutConstraint constraintWithItem:box attribute:NSLayoutAttributeHeight relatedBy:NSLayoutRelationEqual toItem:nil attribute:NSLayoutAttributeNotAnAttribute multiplier:1 constant:100];
    [root addConstraint:boxH];

    // a second box with anchors: right of the first one with 10 spacing, same top and height, up to the right edge - 20
    UIView *box2 = [UIView new]; box2.translatesAutoresizingMaskIntoConstraints = NO; [root addSubview:box2];
    [NSLayoutConstraint activateConstraints:@[
        [box2.leadingAnchor constraintEqualToAnchor:box.trailingAnchor constant:10],
        [box2.topAnchor constraintEqualToAnchor:box.topAnchor],
        [box2.heightAnchor constraintEqualToAnchor:box.heightAnchor],
        [box2.trailingAnchor constraintEqualToAnchor:root.trailingAnchor constant:-20],
    ]];

    // a label centered under the boxes at its intrinsic size
    UILabel *label = [UILabel new]; label.text = @"Hello"; label.font = [UIFont systemFontOfSize:20];
    label.translatesAutoresizingMaskIntoConstraints = NO; [root addSubview:label];
    [label.centerXAnchor constraintEqualToAnchor:root.centerXAnchor].active = YES;
    [label.topAnchor constraintEqualToAnchor:box.bottomAnchor constant:15].active = YES;

    // an inequality and a priority conflict: width >= 200 (required) beats width == 50 (priority 500)
    UIView *bar = [UIView new]; bar.translatesAutoresizingMaskIntoConstraints = NO; [root addSubview:bar];
    NSLayoutConstraint *weak = [bar.widthAnchor constraintEqualToConstant:50]; weak.priority = 500;
    [NSLayoutConstraint activateConstraints:@[
        [bar.widthAnchor constraintGreaterThanOrEqualToConstant:200], weak,
        [bar.heightAnchor constraintEqualToConstant:8],
        [bar.centerXAnchor constraintEqualToAnchor:root.centerXAnchor],
        [bar.bottomAnchor constraintEqualToAnchor:root.bottomAnchor constant:-40],
    ]];

    // a view placed by its frame (autoresizing) that others refer to
    UIView *fixed = [[UIView alloc] initWithFrame:CGRectMake(100, 300, 50, 50)]; [root addSubview:fixed];
    UIView *follower = [UIView new]; follower.translatesAutoresizingMaskIntoConstraints = NO; [root addSubview:follower];
    [NSLayoutConstraint activateConstraints:@[
        [follower.leftAnchor constraintEqualToAnchor:fixed.rightAnchor constant:5],
        [follower.centerYAnchor constraintEqualToAnchor:fixed.centerYAnchor],
        [follower.widthAnchor constraintEqualToAnchor:fixed.widthAnchor multiplier:2],
        [follower.heightAnchor constraintEqualToConstant:10],
    ]];

    // visual format: a row with margins, a fixed width, a greater-or-equal width and top alignment
    UIView *va = [UIView new], *vb = [UIView new];
    va.translatesAutoresizingMaskIntoConstraints = NO; vb.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:va]; [root addSubview:vb];
    NSDictionary *views = @{ @"va": va, @"vb": vb }, *metrics = @{ @"margin": @30 };
    [root addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"H:|-margin-[va(==60)]-[vb(>=40@750)]-margin-|" options:(1 << NSLayoutAttributeTop) metrics:metrics views:views]];
    [root addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"V:[va(20)]-|" options:0 metrics:nil views:views]];
    [root addConstraints:[NSLayoutConstraint constraintsWithVisualFormat:@"V:[vb(==va)]" options:0 metrics:nil views:views]];

    [root layoutIfNeeded];
    printf("-- first layout\n");
    show("box", box); show("box2", box2); show("label", label); show("bar", bar); show("follower", follower);
    show("va", va); show("vb", vb);

    // changing a constant re-lays out
    boxH.constant = 60;
    fixed.frame = CGRectMake(10, 400, 30, 30);
    [root layoutIfNeeded];
    printf("-- after changes\n");
    show("box", box); show("box2", box2); show("label", label); show("follower", follower);
    printf("constraints on root: %lu\n", (unsigned long)root.constraints.count);
    exit(0);
    return YES;
}
@end

int main(int argc, char **argv) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, @"AppDelegate"); }
}
