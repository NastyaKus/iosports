// iosrt sample: a scripted UIWebView page (WebJS.app/html/guide.html, written
// by tests/make_webjs_bundle.py), the way apps build guided overlays:
//  - the page steps through <section>s with Next/Back click handlers;
//  - the app asks the page whether a point is "solid" through
//    stringByEvaluatingJavaScriptFromString: (elementFromPoint + a class);
//  - the page talks back through iframe loads of a custom scheme, which the
//    delegate sees in webView:shouldStartLoadWithRequest:navigationType:;
//  - timers, innerHTML, querySelectorAll, classList, absolute positioning.
#import <UIKit/UIKit.h>

@interface GuideController : UIViewController <UIWebViewDelegate>
@property (nonatomic, strong) UIWebView *web;
@end
@implementation GuideController
- (void)viewDidLoad {
    [super viewDidLoad];
    self.web = [[UIWebView alloc] initWithFrame:self.view.bounds];
    self.web.delegate = self;
    self.web.backgroundColor = [UIColor clearColor];
    self.web.opaque = NO;
    [self.view addSubview:self.web];
    [self.web loadRequest:[NSURLRequest requestWithURL:[[NSBundle mainBundle] URLForResource:@"guide" withExtension:@"html" subdirectory:@"html"]]];
}
- (NSString *)js:(NSString *)s { return [self.web stringByEvaluatingJavaScriptFromString:s]; }
- (BOOL)webView:(UIWebView *)wv shouldStartLoadWithRequest:(NSURLRequest *)req navigationType:(UIWebViewNavigationType)type {
    if ([req.URL.scheme isEqualToString:@"command"]) {
        NSLog(@"command from the page: %@ (type %ld), page says '%@'", req.URL.host, (long)type, [self js:@"document.getElementById('page').innerHTML"]);
        return NO;
    }
    NSLog(@"load %@ (type %ld)", req.URL.lastPathComponent, (long)type);
    return YES;
}
- (void)webViewDidFinishLoad:(UIWebView *)wv {
    NSLog(@"loaded '%@': %@ sections, %@ visible, step '%@'", [self js:@"document.title"], [self js:@"document.querySelectorAll('section').length"],
          [self js:@"[].filter.call(document.getElementsByTagName('section'),function(s){return getComputedStyle(s).display=='block'}).length"],
          [self js:@"document.getElementById('page').textContent"]);
    NSLog(@"solid at the card %@, at the hole %@, at the close button %@",
          [self js:@"Solid(160, 300)"], [self js:@"Solid(60, 60)"], [self js:@"Solid(290, 20)"]);
    NSLog(@"layout: card %@, close %@, json %@", [self js:@"var r=document.querySelector('.card').getBoundingClientRect(); r.left+','+r.top+' '+r.width+'x'+r.height"],
          [self js:@"var c=document.getElementById('close'); c.offsetLeft+','+c.offsetTop"],
          [self js:@"JSON.stringify({a:[1,2],b:document.body.classList.contains('ready')})"]);
}
@end

@interface GuideAppDelegate : UIResponder <UIApplicationDelegate>
@property (nonatomic, strong) UIWindow *window;
@end
@implementation GuideAppDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)opts {
    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.window.backgroundColor = [UIColor darkGrayColor];
    self.window.rootViewController = [GuideController new];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass([GuideAppDelegate class])); }
}
