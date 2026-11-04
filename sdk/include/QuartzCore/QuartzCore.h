/* iosrt mini-SDK: the QuartzCore subset implemented by the runtime. */
#pragma once
#import <Foundation/Foundation.h>
#include <CoreGraphics/CoreGraphics.h>
typedef double CFTimeInterval;
typedef struct CATransform3D { CGFloat m11, m12, m13, m14, m21, m22, m23, m24, m31, m32, m33, m34, m41, m42, m43, m44; } CATransform3D;
#ifdef __cplusplus
extern "C" {
#endif
extern const CATransform3D CATransform3DIdentity;
CATransform3D CATransform3DMakeRotation(CGFloat angle, CGFloat x, CGFloat y, CGFloat z);
CATransform3D CATransform3DMakeScale(CGFloat sx, CGFloat sy, CGFloat sz);
CFTimeInterval CACurrentMediaTime(void);
#ifdef __cplusplus
}
#endif
extern NSString *const kCAMediaTimingFunctionEaseInEaseOut;
extern NSString *const kCALineCapRound;
@interface CAMediaTimingFunction : NSObject
+ (instancetype)functionWithName:(NSString *)name;
@end
@interface CALayer : NSObject
+ (instancetype)layer;
@property CGRect frame;
@property CGRect bounds;
@property CGPoint position;
@property CGPoint anchorPoint;
@property CATransform3D transform;
@property float opacity;
@property (getter=isHidden) BOOL hidden;
@property BOOL masksToBounds;
@property CGColorRef backgroundColor;
@property CGColorRef borderColor;
@property CGFloat borderWidth;
@property CGFloat cornerRadius;
@property (strong) id contents;
@property CGFloat contentsScale;
@property (getter=isOpaque) BOOL opaque;
@property (readonly) NSArray *sublayers;
- (void)addSublayer:(CALayer *)l;
- (void)removeFromSuperlayer;
- (void)addAnimation:(id)anim forKey:(NSString *)key;
- (void)removeAllAnimations;
- (void)setNeedsDisplay;
@end
@interface CAShapeLayer : CALayer
@property CGPathRef path;
@property CGColorRef fillColor;
@property CGColorRef strokeColor;
@property CGFloat lineWidth;
@property CGFloat strokeStart;
@property CGFloat strokeEnd;
@property (copy) NSString *lineCap;
@end
@interface CAGradientLayer : CALayer
@property (copy) NSArray *colors;
@property (copy) NSArray<NSNumber *> *locations;
@property CGPoint startPoint;
@property CGPoint endPoint;
@end
@interface CAAnimation : NSObject
+ (instancetype)animation;
@property CFTimeInterval duration;
@property float repeatCount;
@property BOOL autoreverses;
@property (strong) CAMediaTimingFunction *timingFunction;
@property (strong) id delegate;
@end
@interface CAPropertyAnimation : CAAnimation
+ (instancetype)animationWithKeyPath:(NSString *)path;
@end
@interface CABasicAnimation : CAPropertyAnimation
@property (strong) id fromValue;
@property (strong) id toValue;
@end
@interface CATransaction : NSObject
+ (void)begin; + (void)commit;
+ (void)setCompletionBlock:(void (^)(void))block;
+ (void)setDisableActions:(BOOL)flag;
@end
@interface CADisplayLink : NSObject
+ (CADisplayLink *)displayLinkWithTarget:(id)target selector:(SEL)sel;
- (void)addToRunLoop:(NSRunLoop *)runloop forMode:(NSString *)mode;
- (void)removeFromRunLoop:(NSRunLoop *)runloop forMode:(NSString *)mode;
- (void)invalidate;
@property (readonly) CFTimeInterval timestamp;
@property (readonly) CFTimeInterval duration;
@property (getter=isPaused) BOOL paused;
@property NSInteger frameInterval;
@property NSInteger preferredFramesPerSecond;
@end
@protocol EAGLDrawable
@property (copy) NSDictionary *drawableProperties;
@end
@interface CAEAGLLayer : CALayer <EAGLDrawable>
@property BOOL presentsWithTransaction;
@end
