// iosrt sample: navigation inside a custom container, the way apps with their
// own chrome build it, plus UITableViewController and section headers/footers.
//  - ShellController (a UINavigationController subclass) replaces self.view in
//    viewDidLoad and re-parents the original view inside its own, then insets
//    it through subviews[0]: pushed and popped controllers must keep living in
//    that original container.
//  - SettingsController is a UITableViewController made with initWithStyle:,
//    with a footer title and a custom footer view.
#import <UIKit/UIKit.h>

static NSString *R(CGRect r) { return NSStringFromCGRect(r); }

@interface ShellView : UIView
@end
@implementation ShellView
@end

@interface ShellController : UINavigationController
@end
@implementation ShellController
- (void)viewDidLoad {
    [super viewDidLoad];
    UIView *original = self.view;
    ShellView *shell = [[ShellView alloc] initWithFrame:original.frame];
    self.view = shell;
    [shell addSubview:original];
    self.navigationBarHidden = YES;
    [self layoutContent];
}
- (void)layoutContent {
    UIView *content = self.view.subviews[0];
    content.frame = CGRectInset(self.view.bounds, 20, 20);
}
- (void)pushViewController:(UIViewController *)vc animated:(BOOL)animated {
    [super pushViewController:vc animated:NO];
    [self layoutContent];
}
@end

@interface DetailController : UIViewController
@end
@implementation DetailController
- (void)viewDidLoad {
    [super viewDidLoad];
    UIButton *b = [UIButton buttonWithType:UIButtonTypeSystem];
    b.frame = CGRectMake(0, 100, self.view.bounds.size.width, 50);
    [b setTitle:@"Back" forState:UIControlStateNormal];
    [b addTarget:self action:@selector(back) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:b];
}
- (void)viewDidAppear:(BOOL)a {
    [super viewDidAppear:a];
    NSLog(@"detail appeared, view %@ in %@", R(self.view.frame), R(self.view.superview.frame));
}
- (void)back { NSLog(@"detail: back"); [self.navigationController popViewControllerAnimated:YES]; }
@end

@interface SettingsController : UITableViewController
@property (nonatomic, weak) UIViewController *menu;
@end
@implementation SettingsController
- (NSInteger)numberOfSectionsInTableView:(UITableView *)tv { return 2; }
- (NSInteger)tableView:(UITableView *)tv numberOfRowsInSection:(NSInteger)s { return s == 0 ? 2 : 1; }
- (UITableViewCell *)tableView:(UITableView *)tv cellForRowAtIndexPath:(NSIndexPath *)ip {
    UITableViewCell *c = [tv dequeueReusableCellWithIdentifier:@"c"];
    if (!c) c = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:@"c"];
    c.textLabel.text = ip.section == 0 ? (ip.row == 0 ? @"Detail" : @"Done") : @"Info";
    return c;
}
- (NSString *)tableView:(UITableView *)tv titleForFooterInSection:(NSInteger)s {
    return s == 0 ? @"Footer text explaining the rows above." : nil;
}
- (CGFloat)tableView:(UITableView *)tv heightForFooterInSection:(NSInteger)s { return s == 1 ? 60 : UITableViewAutomaticDimension; }
- (UIView *)tableView:(UITableView *)tv viewForFooterInSection:(NSInteger)s {
    if (s != 1) return nil;
    UILabel *l = [[UILabel alloc] initWithFrame:CGRectZero];
    l.text = @"Custom footer view";
    l.tag = 99;
    return l;
}
- (void)viewDidAppear:(BOOL)a {
    [super viewDidAppear:a];
    UITableView *tv = self.tableView;
    NSLog(@"settings appeared: tableView is view %d, style %ld, %ld sections, frame %@ in %@", tv == self.view, (long)tv.style,
          (long)tv.numberOfSections, R(tv.frame), R(tv.superview.frame));
    NSLog(@"  rows %@ %@ %@", R([tv rectForRowAtIndexPath:[NSIndexPath indexPathForRow:0 inSection:0]]),
          R([tv rectForRowAtIndexPath:[NSIndexPath indexPathForRow:1 inSection:0]]),
          R([tv rectForRowAtIndexPath:[NSIndexPath indexPathForRow:0 inSection:1]]));
    UIView *footer = [tv viewWithTag:99];
    NSLog(@"  custom footer view %@, menu view on screen: %d", footer ? R(footer.frame) : @"missing", self.menu.view.window != nil);
}
- (void)tableView:(UITableView *)tv didSelectRowAtIndexPath:(NSIndexPath *)ip {
    NSLog(@"settings: selected %ld.%ld", (long)ip.section, (long)ip.row);
    if (ip.section == 0 && ip.row == 0) [self.navigationController pushViewController:[DetailController new] animated:YES];
    else if (ip.section == 0) [self.navigationController popViewControllerAnimated:YES];
}
@end

@interface MenuController : UIViewController
@end
@implementation MenuController
- (void)viewDidLoad {
    [super viewDidLoad];
    UIButton *b = [UIButton buttonWithType:UIButtonTypeSystem];
    b.frame = CGRectMake(0, 100, self.view.bounds.size.width, 50);
    [b setTitle:@"Settings" forState:UIControlStateNormal];
    [b addTarget:self action:@selector(settings) forControlEvents:UIControlEventTouchUpInside];
    [self.view addSubview:b];
}
- (void)viewDidAppear:(BOOL)a {
    [super viewDidAppear:a];
    UIView *content = self.view.superview;
    NSLog(@"menu appeared, view %@ in %@ (%@ inside %@)", R(self.view.frame), R(content.frame),
          content.superview == self.navigationController.view ? @"original container" : @"something else",
          NSStringFromClass([content.superview class]));
}
- (void)settings {
    SettingsController *s = [[SettingsController alloc] initWithStyle:UITableViewStyleGrouped];
    s.menu = self;
    NSLog(@"menu: push settings");
    [self.navigationController pushViewController:s animated:YES];
}
@end

@interface NavAppDelegate : NSObject <UIApplicationDelegate>
@property (nonatomic, strong) UIWindow *window;
@end
@implementation NavAppDelegate
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)opts {
    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    NSLog(@"screen %@", R(self.window.bounds));
    self.window.rootViewController = [[ShellController alloc] initWithRootViewController:[MenuController new]];
    [self.window makeKeyAndVisible];
    return YES;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool { return UIApplicationMain(argc, argv, nil, @"NavAppDelegate"); }
}
