/* iosrt mini-SDK: the UIKit subset implemented by the runtime. */
#pragma once
#import <Foundation/Foundation.h>
#include <CoreGraphics/CoreGraphics.h>
@class UIWindow, UIView, UIViewController, UIEvent, UITouch, UIApplication;
typedef NS_ENUM(NSInteger, NSTextAlignment) { NSTextAlignmentLeft = 0, NSTextAlignmentCenter = 1, NSTextAlignmentRight = 2 };
typedef NS_OPTIONS(NSUInteger, UIControlEvents) { UIControlEventTouchDown = 1 << 0, UIControlEventTouchUpInside = 1 << 6, UIControlEventValueChanged = 1 << 12 };
typedef NS_OPTIONS(NSUInteger, UIControlState) { UIControlStateNormal = 0, UIControlStateHighlighted = 1 };
typedef NS_ENUM(NSInteger, UIButtonType) { UIButtonTypeCustom = 0, UIButtonTypeSystem = 1 };

@interface UIColor : NSObject
+ (UIColor *)colorWithRed:(CGFloat)r green:(CGFloat)g blue:(CGFloat)b alpha:(CGFloat)a;
+ (UIColor *)colorWithWhite:(CGFloat)w alpha:(CGFloat)a;
+ (UIColor *)whiteColor; + (UIColor *)blackColor; + (UIColor *)redColor; + (UIColor *)greenColor; + (UIColor *)blueColor;
+ (UIColor *)yellowColor; + (UIColor *)orangeColor; + (UIColor *)purpleColor; + (UIColor *)grayColor;
+ (UIColor *)darkGrayColor; + (UIColor *)lightGrayColor; + (UIColor *)clearColor;
- (UIColor *)colorWithAlphaComponent:(CGFloat)a;
@property (readonly) CGColorRef CGColor;
- (void)set; - (void)setFill; - (void)setStroke;
@end
@interface UIFont : NSObject
+ (UIFont *)systemFontOfSize:(CGFloat)s;
+ (UIFont *)boldSystemFontOfSize:(CGFloat)s;
@property (readonly) CGFloat pointSize;
@end
@interface UIResponder : NSObject
- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event;
- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event;
- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event;
@property (readonly) UIResponder *nextResponder;
@end
@interface UITouch : NSObject
- (CGPoint)locationInView:(UIView *)v;
@property (readonly) UIView *view;
@end
@interface UIEvent : NSObject @end
@interface UIScreen : NSObject
+ (UIScreen *)mainScreen;
@property (readonly) CGRect bounds;
@property (readonly) CGFloat scale;
@end
@interface UIDevice : NSObject
+ (UIDevice *)currentDevice;
@property (readonly, copy) NSString *systemVersion;
@property (readonly, copy) NSString *model;
@end
@interface UIView : UIResponder
- (instancetype)initWithFrame:(CGRect)frame;
@property (nonatomic) CGRect frame;
@property (nonatomic) CGRect bounds;
@property (nonatomic) CGPoint center;
@property (nonatomic, copy) UIColor *backgroundColor;
@property (nonatomic) CGFloat alpha;
@property (nonatomic, getter=isHidden) BOOL hidden;
@property (nonatomic) NSInteger tag;
@property (nonatomic, getter=isUserInteractionEnabled) BOOL userInteractionEnabled;
@property (readonly, copy) NSArray<UIView *> *subviews;
@property (readonly) UIView *superview;
@property (readonly) UIWindow *window;
- (void)addSubview:(UIView *)v;
- (void)removeFromSuperview;
- (void)setNeedsDisplay;
- (void)setNeedsLayout;
- (void)layoutSubviews;
- (void)drawRect:(CGRect)r;
- (void)sizeToFit;
- (UIView *)viewWithTag:(NSInteger)t;
@end
@interface UIWindow : UIView
@property (nonatomic, strong) UIViewController *rootViewController;
- (void)makeKeyAndVisible;
@end
@interface UILabel : UIView
@property (nonatomic, copy) NSString *text;
@property (nonatomic, strong) UIColor *textColor;
@property (nonatomic, strong) UIFont *font;
@property (nonatomic) NSTextAlignment textAlignment;
@property (nonatomic) NSInteger numberOfLines;
@end
@interface UIControl : UIView
- (void)addTarget:(id)target action:(SEL)action forControlEvents:(UIControlEvents)events;
- (void)sendActionsForControlEvents:(UIControlEvents)events;
@property (nonatomic, getter=isEnabled) BOOL enabled;
@property (nonatomic, getter=isSelected) BOOL selected;
@end
@interface UIButton : UIControl
+ (instancetype)buttonWithType:(UIButtonType)t;
- (void)setTitle:(NSString *)t forState:(UIControlState)s;
- (void)setTitleColor:(UIColor *)c forState:(UIControlState)s;
- (NSString *)titleForState:(UIControlState)s;
@end
@interface UIViewController : UIResponder
@property (nonatomic, strong) UIView *view;
@property (nonatomic, copy) NSString *title;
- (instancetype)initWithNibName:(NSString *)n bundle:(NSBundle *)b;
- (void)loadView;
- (void)viewDidLoad;
- (void)viewWillAppear:(BOOL)a;
- (void)viewDidAppear:(BOOL)a;
@end
@protocol UIApplicationDelegate <NSObject>
@optional
@property (nonatomic, strong) UIWindow *window;
- (BOOL)application:(UIApplication *)app didFinishLaunchingWithOptions:(NSDictionary *)opts;
- (void)applicationDidBecomeActive:(UIApplication *)app;
- (void)applicationWillTerminate:(UIApplication *)app;
@end
@interface UIApplication : UIResponder
+ (UIApplication *)sharedApplication;
@property (nonatomic, weak) id<UIApplicationDelegate> delegate;
@property (readonly) UIWindow *keyWindow;
@property (nonatomic, getter=isIdleTimerDisabled) BOOL idleTimerDisabled;
@end
#ifdef __cplusplus
extern "C" {
#endif
int UIApplicationMain(int argc, char *argv[], NSString *principal, NSString *delegate);
CGContextRef UIGraphicsGetCurrentContext(void);
void UIRectFill(CGRect r);
NSString *NSStringFromCGRect(CGRect r);
NSString *NSStringFromCGPoint(CGPoint p);
#ifdef __cplusplus
}
#endif
