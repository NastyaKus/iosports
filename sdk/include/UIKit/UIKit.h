/* iosrt mini-SDK: the UIKit subset implemented by the runtime. */
#pragma once
#import <Foundation/Foundation.h>
#include <CoreGraphics/CoreGraphics.h>
#import <QuartzCore/QuartzCore.h>
@class UIWindow, UIView, UIViewController, UIEvent, UITouch, UIApplication, UIImage, UIGestureRecognizer;
typedef NS_ENUM(NSInteger, NSTextAlignment) { NSTextAlignmentLeft = 0, NSTextAlignmentCenter = 1, NSTextAlignmentRight = 2 };
typedef NS_OPTIONS(NSUInteger, UIControlEvents) { UIControlEventTouchDown = 1 << 0, UIControlEventTouchUpInside = 1 << 6, UIControlEventValueChanged = 1 << 12 };
typedef NS_OPTIONS(NSUInteger, UIControlState) { UIControlStateNormal = 0, UIControlStateHighlighted = 1 };
typedef NS_ENUM(NSInteger, UIButtonType) { UIButtonTypeCustom = 0, UIButtonTypeSystem = 1 };
typedef struct UIEdgeInsets { CGFloat top, left, bottom, right; } UIEdgeInsets;
static inline UIEdgeInsets UIEdgeInsetsMake(CGFloat t, CGFloat l, CGFloat b, CGFloat r) { UIEdgeInsets e = { t, l, b, r }; return e; }
typedef NS_ENUM(NSInteger, UIViewContentMode) { UIViewContentModeScaleToFill = 0, UIViewContentModeScaleAspectFit = 1, UIViewContentModeScaleAspectFill = 2, UIViewContentModeCenter = 4 };
typedef NS_OPTIONS(NSUInteger, UIViewAutoresizing) { UIViewAutoresizingNone = 0, UIViewAutoresizingFlexibleLeftMargin = 1, UIViewAutoresizingFlexibleWidth = 2,
    UIViewAutoresizingFlexibleRightMargin = 4, UIViewAutoresizingFlexibleTopMargin = 8, UIViewAutoresizingFlexibleHeight = 16, UIViewAutoresizingFlexibleBottomMargin = 32 };
typedef NS_ENUM(NSInteger, UIGestureRecognizerState) { UIGestureRecognizerStatePossible, UIGestureRecognizerStateBegan, UIGestureRecognizerStateChanged,
    UIGestureRecognizerStateEnded, UIGestureRecognizerStateCancelled, UIGestureRecognizerStateFailed, UIGestureRecognizerStateRecognized = UIGestureRecognizerStateEnded };
typedef NS_ENUM(NSInteger, UIAlertControllerStyle) { UIAlertControllerStyleActionSheet = 0, UIAlertControllerStyleAlert = 1 };
typedef NS_ENUM(NSInteger, UIAlertActionStyle) { UIAlertActionStyleDefault = 0, UIAlertActionStyleCancel = 1, UIAlertActionStyleDestructive = 2 };
typedef NS_ENUM(NSInteger, UIActivityIndicatorViewStyle) { UIActivityIndicatorViewStyleWhiteLarge = 0, UIActivityIndicatorViewStyleWhite = 1, UIActivityIndicatorViewStyleGray = 2 };

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
@interface NSObject (UINibLoadingAdditions)
- (void)awakeFromNib;
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
@property (readonly, strong) CALayer *layer;
@property (nonatomic) CGAffineTransform transform;
@property (nonatomic) BOOL clipsToBounds;
@property (nonatomic) UIViewContentMode contentMode;
@property (nonatomic) UIViewAutoresizing autoresizingMask;
@property (nonatomic, strong) UIColor *tintColor;
- (void)addGestureRecognizer:(UIGestureRecognizer *)g;
- (CGPoint)convertPoint:(CGPoint)p toView:(UIView *)v;
+ (void)animateWithDuration:(NSTimeInterval)d animations:(void (^)(void))a completion:(void (^)(BOOL finished))c;
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
@interface UIImage : NSObject
+ (UIImage *)imageNamed:(NSString *)name;
+ (UIImage *)imageWithCGImage:(CGImageRef)img;
+ (UIImage *)imageWithCGImage:(CGImageRef)img scale:(CGFloat)scale orientation:(NSInteger)o;
@property (readonly) CGSize size;
@property (readonly) CGFloat scale;
@property (readonly) CGImageRef CGImage;
- (void)drawInRect:(CGRect)r;
- (UIImage *)resizableImageWithCapInsets:(UIEdgeInsets)insets;
@end
@interface UIImageView : UIView
- (instancetype)initWithImage:(UIImage *)img;
@property (nonatomic, strong) UIImage *image;
@end
@interface UIBezierPath : NSObject
+ (instancetype)bezierPath;
+ (instancetype)bezierPathWithRect:(CGRect)r;
+ (instancetype)bezierPathWithOvalInRect:(CGRect)r;
+ (instancetype)bezierPathWithRoundedRect:(CGRect)r cornerRadius:(CGFloat)c;
+ (instancetype)bezierPathWithArcCenter:(CGPoint)c radius:(CGFloat)r startAngle:(CGFloat)a endAngle:(CGFloat)b clockwise:(BOOL)cw;
- (void)moveToPoint:(CGPoint)p;
- (void)addLineToPoint:(CGPoint)p;
- (void)addQuadCurveToPoint:(CGPoint)p controlPoint:(CGPoint)c;
- (void)closePath;
- (void)fill;
- (void)stroke;
- (void)addClip;
@property (nonatomic) CGFloat lineWidth;
@property (readonly) CGPathRef CGPath;
@end
@interface UISlider : UIControl
@property (nonatomic) float value;
@property (nonatomic) float minimumValue;
@property (nonatomic) float maximumValue;
@property (nonatomic, strong) UIColor *minimumTrackTintColor;
@end
@interface UISwitch : UIControl
@property (nonatomic, getter=isOn) BOOL on;
@property (nonatomic, strong) UIColor *onTintColor;
@end
@interface UIActivityIndicatorView : UIView
- (instancetype)initWithActivityIndicatorStyle:(UIActivityIndicatorViewStyle)s;
- (void)startAnimating;
- (void)stopAnimating;
@end
@interface UIPageControl : UIControl
@property (nonatomic) NSInteger numberOfPages;
@property (nonatomic) NSInteger currentPage;
@end
@protocol UIScrollViewDelegate <NSObject>
@optional
- (void)scrollViewDidScroll:(id)scrollView;
- (void)scrollViewDidEndDecelerating:(id)scrollView;
@end
@interface UIScrollView : UIView
@property (nonatomic) CGPoint contentOffset;
@property (nonatomic) CGSize contentSize;
@property (nonatomic, getter=isPagingEnabled) BOOL pagingEnabled;
@property (nonatomic, weak) id<UIScrollViewDelegate> delegate;
@end
@interface UIGestureRecognizer : NSObject
- (instancetype)initWithTarget:(id)target action:(SEL)action;
@property (readonly) UIGestureRecognizerState state;
@property (readonly) UIView *view;
- (CGPoint)locationInView:(UIView *)v;
@end
@interface UITapGestureRecognizer : UIGestureRecognizer
@property (nonatomic) NSUInteger numberOfTapsRequired;
@end
@interface UIPanGestureRecognizer : UIGestureRecognizer
- (CGPoint)translationInView:(UIView *)v;
@end
@interface UIViewController : UIResponder
@property (nonatomic, strong) UIView *view;
@property (nonatomic, copy) NSString *title;
- (instancetype)initWithNibName:(NSString *)n bundle:(NSBundle *)b;
- (void)loadView;
- (void)viewDidLoad;
- (void)viewWillAppear:(BOOL)a;
- (void)viewDidAppear:(BOOL)a;
- (void)presentViewController:(UIViewController *)vc animated:(BOOL)a completion:(void (^)(void))c;
- (void)dismissViewControllerAnimated:(BOOL)a completion:(void (^)(void))c;
@end
@interface UIAlertAction : NSObject
+ (instancetype)actionWithTitle:(NSString *)t style:(UIAlertActionStyle)s handler:(void (^)(UIAlertAction *a))h;
@property (readonly) NSString *title;
@end
@interface UIAlertController : UIViewController
+ (instancetype)alertControllerWithTitle:(NSString *)t message:(NSString *)m preferredStyle:(UIAlertControllerStyle)s;
- (void)addAction:(UIAlertAction *)a;
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
void UIGraphicsBeginImageContextWithOptions(CGSize size, BOOL opaque, CGFloat scale);
UIImage *UIGraphicsGetImageFromCurrentImageContext(void);
void UIGraphicsEndImageContext(void);
NSString *NSStringFromCGRect(CGRect r);
NSString *NSStringFromCGPoint(CGPoint p);
#ifdef __cplusplus
}
#endif
