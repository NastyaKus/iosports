// iosrt sample: the graphics/UIKit stack — CoreGraphics drawing (gradients,
// paths, dashes, bitmap contexts), UIBezierPath, Core Animation layers and an
// animation, a paging scroll view, gestures, controls and an alert controller.
// Every interaction is logged so the headless test can check it.
#import <UIKit/UIKit.h>
#include <math.h>

@interface ShapesView : UIView
@end
@implementation ShapesView
- (void)drawRect:(CGRect)rect {
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    CGFloat w = self.bounds.size.width;
    // linear gradient card
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGFloat comps[] = { 0.20, 0.45, 0.95, 1,   0.65, 0.25, 0.85, 1 };
    CGFloat locs[] = { 0, 1 };
    CGGradientRef grad = CGGradientCreateWithColorComponents(space, comps, locs, 2);
    CGContextSaveGState(ctx);
    [[UIBezierPath bezierPathWithRoundedRect:CGRectMake(16, 0, w - 32, 120) cornerRadius:18] addClip];
    CGContextDrawLinearGradient(ctx, grad, CGPointMake(16, 0), CGPointMake(w - 16, 120), 0);
    CGContextRestoreGState(ctx);
    // radial "sun"
    CGFloat sun[] = { 1, 0.95, 0.5, 1,   1, 0.6, 0.1, 0 };
    CGGradientRef rg = CGGradientCreateWithColorComponents(space, sun, locs, 2);
    CGContextDrawRadialGradient(ctx, rg, CGPointMake(w - 70, 60), 0, CGPointMake(w - 70, 60), 46, 0);
    CGGradientRelease(rg); CGGradientRelease(grad); CGColorSpaceRelease(space);
    // a five-pointed star with UIBezierPath
    UIBezierPath *star = [UIBezierPath bezierPath];
    CGPoint c = CGPointMake(70, 190);
    for (int i = 0; i < 10; i++) {
        CGFloat r = (i % 2) ? 22 : 50, a = -M_PI_2 + i * M_PI / 5;
        CGPoint p = CGPointMake(c.x + r * cos(a), c.y + r * sin(a));
        if (i == 0) [star moveToPoint:p]; else [star addLineToPoint:p];
    }
    [star closePath];
    [[UIColor colorWithRed:1 green:0.8 blue:0.2 alpha:1] setFill];
    [star fill];
    [[UIColor whiteColor] setStroke];
    star.lineWidth = 2;
    [star stroke];
    // dashed arc with round caps
    CGContextSetRGBStrokeColor(ctx, 0.3, 0.9, 0.6, 1);
    CGContextSetLineWidth(ctx, 6);
    CGContextSetLineCap(ctx, kCGLineCapRound);
    CGFloat dash[] = { 2, 12 };
    CGContextSetLineDash(ctx, 0, dash, 2);
    CGContextBeginPath(ctx);
    CGContextAddArc(ctx, 190, 190, 44, 0, 1.5 * M_PI, 0);
    CGContextStrokePath(ctx);
}
@end

@interface DemoViewController : UIViewController <UIScrollViewDelegate>
@property (nonatomic, strong) UIScrollView *scroll;
@property (nonatomic, strong) UIPageControl *pages;
@property (nonatomic, strong) UILabel *status;
@property (nonatomic, strong) UISlider *slider;
@end

static UIImage *checkerImage(void) {
    const int n = 96;
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef bmp = CGBitmapContextCreate(NULL, n, n, 8, n * 4, space, kCGImageAlphaPremultipliedLast);
    for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x++) {
        if ((x + y) % 2) CGContextSetRGBFillColor(bmp, 0.95, 0.95, 0.95, 1); else CGContextSetRGBFillColor(bmp, 0.2, 0.2, 0.25, 1);
        CGContextFillRect(bmp, CGRectMake(x * 12, y * 12, 12, 12));
    }
    CGContextSetRGBFillColor(bmp, 0.9, 0.2, 0.3, 0.85);
    CGContextFillEllipseInRect(bmp, CGRectMake(24, 24, 48, 48));
    CGImageRef img = CGBitmapContextCreateImage(bmp);
    UIImage *ui = [UIImage imageWithCGImage:img scale:2 orientation:0];
    CGImageRelease(img); CGContextRelease(bmp); CGColorSpaceRelease(space);
    return ui;
}

@implementation DemoViewController
- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor colorWithRed:0.08 green:0.09 blue:0.13 alpha:1];
    CGRect b = self.view.bounds; CGFloat W = b.size.width;

    self.scroll = [[UIScrollView alloc] initWithFrame:CGRectMake(0, 40, W, 500)];
    self.scroll.pagingEnabled = YES;
    self.scroll.contentSize = CGSizeMake(W * 2, 500);
    self.scroll.delegate = self;
    [self.view addSubview:self.scroll];

    // ---- page 1: drawing and layers
    UILabel *t1 = [[UILabel alloc] initWithFrame:CGRectMake(16, 0, W - 32, 30)];
    t1.text = @"Core Graphics + Core Animation"; t1.textColor = [UIColor whiteColor]; t1.font = [UIFont boldSystemFontOfSize:18];
    [self.scroll addSubview:t1];
    ShapesView *shapes = [[ShapesView alloc] initWithFrame:CGRectMake(0, 40, W, 250)];
    shapes.backgroundColor = [UIColor clearColor];
    [self.scroll addSubview:shapes];

    CAShapeLayer *ring = [CAShapeLayer layer];
    CGMutablePathRef rp = CGPathCreateMutable();
    CGPathAddEllipseInRect(rp, NULL, CGRectMake(0, 0, 80, 80));
    ring.path = rp; CGPathRelease(rp);
    ring.frame = CGRectMake(W - 112, 190, 80, 80);
    ring.fillColor = [UIColor clearColor].CGColor;
    ring.strokeColor = [UIColor colorWithRed:0.3 green:0.8 blue:1 alpha:1].CGColor;
    ring.lineWidth = 10; ring.lineCap = kCALineCapRound; ring.strokeEnd = 0.7;
    [self.scroll.layer addSublayer:ring];

    CAGradientLayer *bar = [CAGradientLayer layer];
    bar.frame = CGRectMake(16, 310, W - 32, 24);
    bar.cornerRadius = 12; bar.masksToBounds = YES;
    bar.colors = @[ (id)[UIColor redColor].CGColor, (id)[UIColor yellowColor].CGColor, (id)[UIColor greenColor].CGColor ];
    bar.startPoint = CGPointMake(0, 0.5); bar.endPoint = CGPointMake(1, 0.5);
    [self.scroll.layer addSublayer:bar];

    CALayer *spinner = [CALayer layer];
    spinner.frame = CGRectMake(W / 2 - 25, 360, 50, 50);
    spinner.backgroundColor = [UIColor orangeColor].CGColor;
    spinner.cornerRadius = 8;
    CABasicAnimation *spin = [CABasicAnimation animationWithKeyPath:@"transform.rotation.z"];
    spin.fromValue = @0; spin.toValue = @(2 * M_PI); spin.duration = 2; spin.repeatCount = 1e9;
    [spinner addAnimation:spin forKey:@"spin"];
    [self.scroll.layer addSublayer:spinner];

    // ---- page 2: controls and gestures
    UILabel *t2 = [[UILabel alloc] initWithFrame:CGRectMake(W + 16, 0, W - 32, 30)];
    t2.text = @"UIKit controls"; t2.textColor = [UIColor whiteColor]; t2.font = [UIFont boldSystemFontOfSize:18];
    [self.scroll addSubview:t2];
    UIImageView *iv = [[UIImageView alloc] initWithImage:checkerImage()];
    iv.frame = CGRectMake(W + 40, 80, 120, 120);
    iv.userInteractionEnabled = YES;
    iv.layer.borderColor = [UIColor whiteColor].CGColor; iv.layer.borderWidth = 2;
    [iv addGestureRecognizer:[[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(imageTapped:)]];
    [self.scroll addSubview:iv];
    UIActivityIndicatorView *act = [[UIActivityIndicatorView alloc] initWithActivityIndicatorStyle:UIActivityIndicatorViewStyleWhiteLarge];
    act.center = CGPointMake(W + W - 80, 140);
    [act startAnimating];
    [self.scroll addSubview:act];

    self.slider = [[UISlider alloc] initWithFrame:CGRectMake(W + 40, 240, W - 80, 31)];
    self.slider.value = 0.25;
    self.slider.minimumTrackTintColor = [UIColor orangeColor];
    [self.slider addTarget:self action:@selector(sliderChanged:) forControlEvents:UIControlEventValueChanged];
    [self.scroll addSubview:self.slider];
    UISwitch *sw = [[UISwitch alloc] initWithFrame:CGRectMake(W + 40, 300, 51, 31)];
    [sw addTarget:self action:@selector(switchChanged:) forControlEvents:UIControlEventValueChanged];
    [self.scroll addSubview:sw];
    UIButton *alertButton = [UIButton buttonWithType:UIButtonTypeSystem];
    alertButton.frame = CGRectMake(W + W / 2 - 80, 360, 160, 44);
    [alertButton setTitle:@"Show alert" forState:UIControlStateNormal];
    [alertButton addTarget:self action:@selector(showAlert) forControlEvents:UIControlEventTouchUpInside];
    [self.scroll addSubview:alertButton];

    self.pages = [[UIPageControl alloc] initWithFrame:CGRectMake(0, 550, W, 30)];
    self.pages.numberOfPages = 2;
    [self.view addSubview:self.pages];
    self.status = [[UILabel alloc] initWithFrame:CGRectMake(16, 590, W - 32, 30)];
    self.status.textColor = [UIColor colorWithWhite:0.7 alpha:1];
    self.status.textAlignment = NSTextAlignmentCenter;
    self.status.text = @"swipe left for controls";
    [self.view addSubview:self.status];
}
- (void)scrollViewDidEndDecelerating:(UIScrollView *)sv {
    NSInteger page = (NSInteger)(sv.contentOffset.x / sv.bounds.size.width + 0.5);
    if (page != self.pages.currentPage) { self.pages.currentPage = page; NSLog(@"page %ld", (long)page); }
}
- (void)imageTapped:(UITapGestureRecognizer *)g {
    CGPoint p = [g locationInView:g.view];
    NSLog(@"image tapped at %.0f,%.0f", p.x, p.y);
    self.status.text = @"image tapped";
}
- (void)sliderChanged:(UISlider *)s { self.status.text = [NSString stringWithFormat:@"slider %.1f", s.value]; }
- (void)switchChanged:(UISwitch *)s { NSLog(@"switch %@", s.isOn ? @"on" : @"off"); self.status.text = s.isOn ? @"switch on" : @"switch off"; }
- (void)showAlert {
    NSLog(@"showing alert (slider %.1f)", self.slider.value);
    UIAlertController *a = [UIAlertController alertControllerWithTitle:@"iosrt" message:nil preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:^(UIAlertAction *act) {
        NSLog(@"alert action %@", act.title);
    }]];
    [self presentViewController:a animated:YES completion:nil];
}
@end

@interface DemoAppDelegate : NSObject <UIApplicationDelegate>
@property (nonatomic, strong) UIWindow *window;
@end
@implementation DemoAppDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)opts {
    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.window.rootViewController = [[DemoViewController alloc] initWithNibName:nil bundle:nil];
    [self.window makeKeyAndVisible];
    NSLog(@"launched");
    return YES;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, @"DemoAppDelegate"); }
}
