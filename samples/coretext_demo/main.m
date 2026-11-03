// iosrt sample: CoreText. Measures and draws lines with mixed attributes,
// lays out a wrapped paragraph in a frame, truncates a line, and reads
// runs and glyph positions. Drawing happens in a UIView's drawRect: after
// the usual flip into CoreText's y-up coordinates.
#import <UIKit/UIKit.h>
#import <CoreText/CoreText.h>

static NSAttributedString *mixed(void) {
    CTFontRef regular = CTFontCreateWithName((CFStringRef)@"Helvetica", 18, NULL);
    CTFontRef bold = CTFontCreateCopyWithSymbolicTraits(regular, 0, NULL, kCTFontTraitBold, kCTFontTraitBold);
    NSMutableAttributedString *s = [[NSMutableAttributedString alloc] initWithString:@"Death Worm eats the desert"
                                                                          attributes:@{(id)kCTFontAttributeName: (__bridge id)regular}];
    [s addAttributes:@{(id)kCTFontAttributeName: (__bridge id)bold, (id)kCTForegroundColorAttributeName: (id)[UIColor redColor].CGColor}
               range:NSMakeRange(0, 10)];
    CFRelease(regular); CFRelease(bold);
    return s;
}

@interface TextView : UIView
@end
@implementation TextView
- (void)drawRect:(CGRect)rect {
    CGContextRef ctx = UIGraphicsGetCurrentContext();
    CGContextTranslateCTM(ctx, 0, self.bounds.size.height);
    CGContextScaleCTM(ctx, 1, -1);
    CGContextSetTextMatrix(ctx, CGAffineTransformIdentity);

    CTLineRef line = CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)mixed());
    CGContextSetTextPosition(ctx, 20, self.bounds.size.height - 60);
    CTLineDraw(line, ctx);
    CGPoint after = CGContextGetTextPosition(ctx);
    NSLog(@"line drawn, pen advanced by the line width: %d", fabs(after.x - 20 - CTLineGetTypographicBounds(line, NULL, NULL, NULL)) < 0.01);
    CFRelease(line);

    NSString *para = @"CoreText lays this paragraph out in a frame: words wrap at spaces and lines stack from the top of the path.";
    CTParagraphStyleSetting set[] = { { kCTParagraphStyleSpecifierAlignment, sizeof(CTTextAlignment), &(CTTextAlignment){ kCTTextAlignmentCenter } } };
    CTParagraphStyleRef ps = CTParagraphStyleCreate(set, 1);
    CTFontRef f = CTFontCreateWithName((CFStringRef)@"Helvetica", 14, NULL);
    NSAttributedString *as = [[NSAttributedString alloc] initWithString:para attributes:@{(id)kCTFontAttributeName: (__bridge id)f, (id)kCTParagraphStyleAttributeName: (__bridge id)ps}];
    CTFramesetterRef fs = CTFramesetterCreateWithAttributedString((__bridge CFAttributedStringRef)as);
    CFRange fit;
    CGSize sz = CTFramesetterSuggestFrameSizeWithConstraints(fs, CFRangeMake(0, 0), NULL, CGSizeMake(240, CGFLOAT_MAX), &fit);
    CGPathRef path = CGPathCreateWithRect(CGRectMake(40, 120, 240, 200), NULL);
    CTFrameRef frame = CTFramesetterCreateFrame(fs, CFRangeMake(0, 0), path, NULL);
    CFArrayRef lines = CTFrameGetLines(frame);
    CGPoint origins[16]; CTFrameGetLineOrigins(frame, CFRangeMake(0, 0), origins);
    NSLog(@"frame: %ld lines, suggested height > 3 lines: %d, fits whole string: %d, first line centered: %d, lines go down: %d",
          (long)CFArrayGetCount(lines), sz.height > 3 * 14, fit.length == (CFIndex)para.length,
          origins[0].x > 0, CFArrayGetCount(lines) > 1 && origins[1].y < origins[0].y);
    CTFrameDraw(frame, ctx);
    CFRelease(frame); CGPathRelease(path); CFRelease(fs); CFRelease(f); CFRelease(ps);
}
@end

@interface CTController : UIViewController
@end
@implementation CTController
- (void)viewDidLoad {
    [super viewDidLoad];
    CTLineRef line = CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)mixed());
    CGFloat asc, desc, lead;
    double w = CTLineGetTypographicBounds(line, &asc, &desc, &lead);
    CFArrayRef runs = CTLineGetGlyphRuns(line);
    CTRunRef first = CFArrayGetValueAtIndex(runs, 0), second = CFArrayGetValueAtIndex(runs, 1);
    CTFontRef bf = (__bridge CTFontRef)[(__bridge NSDictionary *)CTRunGetAttributes(first) objectForKey:(id)kCTFontAttributeName];
    CGPoint p[3]; CTRunGetPositions(second, CFRangeMake(0, 3), p);
    NSLog(@"line: width > 0 %d, ascent > descent %d, %ld runs, first run %ld glyphs bold %d, second run starts at %ld, x increasing %d",
          w > 0, asc > desc, (long)CFArrayGetCount(runs), (long)CTRunGetGlyphCount(first), (CTFontGetSymbolicTraits(bf) & kCTFontTraitBold) != 0,
          (long)CTRunGetStringRange(second).location, p[1].x > p[0].x && p[2].x > p[1].x);
    CFIndex idx = CTLineGetStringIndexForPosition(line, CGPointMake(CTLineGetOffsetForStringIndex(line, 5, NULL) + 1, 0));
    CTLineRef cut = CTLineCreateTruncatedLine(line, w / 2, kCTLineTruncationEnd, NULL);
    NSLog(@"hit test round trip index %ld; truncated line narrower %d, %ld glyphs", (long)idx,
          CTLineGetTypographicBounds(cut, NULL, NULL, NULL) <= w / 2 + 0.5, (long)CTLineGetGlyphCount(cut));
    CTFontRef f = CTFontCreateWithName((CFStringRef)@"Helvetica", 20, NULL);
    unichar ch[2] = { 'W', 'i' }; CGGlyph g[2]; CGSize adv[2];
    CTFontGetGlyphsForCharacters(f, ch, g, 2);
    CTFontGetAdvancesForGlyphs(f, 0, g, adv, 2);
    NSLog(@"glyphs: W wider than i %d, size %.0f, xHeight below ascent %d", adv[0].width > adv[1].width, CTFontGetSize(f), CTFontGetXHeight(f) < CTFontGetAscent(f));
    CFRelease(f); CFRelease(cut); CFRelease(line);
    TextView *tv = [[TextView alloc] initWithFrame:self.view.bounds];
    tv.backgroundColor = [UIColor whiteColor];
    [self.view addSubview:tv];
}
@end

@interface CTAppDelegate : UIResponder <UIApplicationDelegate>
@property (nonatomic, strong) UIWindow *window;
@end
@implementation CTAppDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)opts {
    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.window.rootViewController = [CTController new];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass([CTAppDelegate class])); }
}
