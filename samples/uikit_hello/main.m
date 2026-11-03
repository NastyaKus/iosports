// iosrt sample: a small UIKit app — window, view controller, label, button
// with target/action, custom drawRect:, timer-driven updates.
#import <UIKit/UIKit.h>

@interface DotsView : UIView
@property (nonatomic) NSInteger count;
@end
@implementation DotsView
- (void)drawRect:(CGRect)rect {
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    CGContextSetRGBStrokeColor(ctx, 1, 1, 1, 0.5);
    CGContextSetLineWidth(ctx, 2);
    CGContextStrokeRect(ctx, CGRectInset(rect, 4, 4));
    for (NSInteger i = 0; i < self.count; i++) {
        CGContextSetRGBFillColor(ctx, 1.0, 0.4 + 0.1 * i, 0.2, 1.0);
        CGContextFillEllipseInRect(ctx, CGRectMake(12 + i * 36, 12, 28, 28));
    }
}
@end

@interface CounterViewController : UIViewController
@property (nonatomic, strong) UILabel *label;
@property (nonatomic, strong) DotsView *dots;
@property (nonatomic) NSInteger taps;
@end
@implementation CounterViewController
- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor colorWithRed:0.12 green:0.12 blue:0.18 alpha:1];
    CGRect b = self.view.bounds;
    UILabel *title = [[UILabel alloc] initWithFrame:CGRectMake(20, 60, b.size.width - 40, 40)];
    title.text = @"Hello from iOS code on Android";
    title.textColor = [UIColor whiteColor];
    title.font = [UIFont boldSystemFontOfSize:22];
    title.textAlignment = NSTextAlignmentCenter;
    [self.view addSubview:title];

    self.label = [[UILabel alloc] initWithFrame:CGRectMake(20, 120, b.size.width - 40, 30)];
    self.label.textColor = [UIColor colorWithWhite:0.8 alpha:1];
    self.label.textAlignment = NSTextAlignmentCenter;
    [self.view addSubview:self.label];

    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    button.frame = CGRectMake(b.size.width / 2 - 80, 180, 160, 50);
    button.backgroundColor = [UIColor colorWithRed:0.2 green:0.5 blue:1 alpha:1];
    [button setTitle:@"Tap me" forState:UIControlStateNormal];
    [button setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    [button addTarget:self action:@selector(tapped:) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:button];

    self.dots = [[DotsView alloc] initWithFrame:CGRectMake(20, 260, b.size.width - 40, 52)];
    self.dots.backgroundColor = [UIColor colorWithWhite:1 alpha:0.08];
    [self.view addSubview:self.dots];
    [self update];
}
- (void)update {
    self.label.text = [NSString stringWithFormat:@"taps: %ld (iOS %@, %@)", (long)self.taps,
                       [UIDevice currentDevice].systemVersion, NSStringFromCGRect([UIScreen mainScreen].bounds)];
    self.dots.count = self.taps % 9;
    [self.dots setNeedsDisplay];
}
- (void)tapped:(UIButton *)sender {
    self.taps++;
    NSLog(@"button tapped %ld", (long)self.taps);
    [self update];
}
@end

@interface AppDelegate : UIResponder <UIApplicationDelegate>
@property (nonatomic, strong) UIWindow *window;
@end
@implementation AppDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)opts {
    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.window.rootViewController = [CounterViewController new];
    [self.window makeKeyAndVisible];
    NSLog(@"launched, window %@", NSStringFromCGRect(self.window.frame));
    return YES;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
    }
}
