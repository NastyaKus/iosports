// iosrt sample: UIWebView showing bundled pages (Web.app/html/*.html, written
// by tests/run_tests.sh): load, CSS, link navigation through the delegate,
// a link the delegate refuses, an unknown scheme, and goBack.
#import <UIKit/UIKit.h>

@interface WebController : UIViewController <UIWebViewDelegate>
@property (nonatomic, strong) UIWebView *web;
@property (nonatomic) BOOL wentBack;
@end
@implementation WebController
- (void)viewDidLoad {
    [super viewDidLoad];
    self.web = [[UIWebView alloc] initWithFrame:self.view.bounds];
    self.web.delegate = self;
    [self.view addSubview:self.web];
    NSURL *url = [[NSBundle mainBundle] URLForResource:@"index" withExtension:@"html" subdirectory:@"html"];
    NSLog(@"index found: %d, scroll view is subviews[0]: %d", url != nil, self.web.subviews[0] == self.web.scrollView);
    [self.web loadRequest:[NSURLRequest requestWithURL:url]];
}
- (BOOL)webView:(UIWebView *)wv shouldStartLoadWithRequest:(NSURLRequest *)req navigationType:(UIWebViewNavigationType)type {
    NSString *u = req.URL.absoluteString;
    BOOL ok = ![req.URL.scheme isEqualToString:@"blocked"];
    NSLog(@"should start %@ (type %ld) -> %@", req.URL.isFileURL ? req.URL.lastPathComponent : u, (long)type, ok ? @"YES" : @"NO");
    return ok;
}
- (void)webViewDidStartLoad:(UIWebView *)wv { NSLog(@"did start"); }
- (void)webViewDidFinishLoad:(UIWebView *)wv {
    NSString *title = [wv stringByEvaluatingJavaScriptFromString:@"document.title"];
    NSLog(@"did finish: title '%@', content height > 0: %d, can go back: %d", title, wv.scrollView.contentSize.height > 0, wv.canGoBack);
    if ([title isEqualToString:@"Second"] && !self.wentBack) { self.wentBack = YES; NSLog(@"going back"); [wv goBack]; }
}
- (void)webView:(UIWebView *)wv didFailLoadWithError:(NSError *)error {
    NSLog(@"did fail: %@ %ld, can go back: %d", error.domain, (long)error.code, wv.canGoBack);
}
@end

@interface WebAppDelegate : NSObject <UIApplicationDelegate>
@property (nonatomic, strong) UIWindow *window;
@end
@implementation WebAppDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)opts {
    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.window.rootViewController = [WebController new];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, @"WebAppDelegate"); }
}
