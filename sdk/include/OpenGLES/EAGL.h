/* EAGL (OpenGL ES contexts on iOS), for the iosrt mini-SDK. */
#ifndef IOSRT_EAGL_H
#define IOSRT_EAGL_H
#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>
typedef NS_ENUM(NSUInteger, EAGLRenderingAPI) { kEAGLRenderingAPIOpenGLES1 = 1, kEAGLRenderingAPIOpenGLES2 = 2, kEAGLRenderingAPIOpenGLES3 = 3 };
extern NSString *const kEAGLDrawablePropertyRetainedBacking;
extern NSString *const kEAGLDrawablePropertyColorFormat;
extern NSString *const kEAGLColorFormatRGBA8;
extern NSString *const kEAGLColorFormatRGB565;
extern NSString *const kEAGLColorFormatSRGBA8;
void EAGLGetVersion(unsigned int *major, unsigned int *minor);
@interface EAGLSharegroup : NSObject
@property (copy) NSString *debugLabel;
@end
@interface EAGLContext : NSObject
- (instancetype)initWithAPI:(EAGLRenderingAPI)api;
- (instancetype)initWithAPI:(EAGLRenderingAPI)api sharegroup:(EAGLSharegroup *)sharegroup;
+ (BOOL)setCurrentContext:(EAGLContext *)context;
+ (EAGLContext *)currentContext;
@property (readonly) EAGLRenderingAPI API;
@property (readonly) EAGLSharegroup *sharegroup;
@property (copy) NSString *debugLabel;
@property (getter=isMultiThreaded) BOOL multiThreaded;
- (BOOL)renderbufferStorage:(NSUInteger)target fromDrawable:(id<EAGLDrawable>)drawable;
- (BOOL)presentRenderbuffer:(NSUInteger)target;
@end
#endif
