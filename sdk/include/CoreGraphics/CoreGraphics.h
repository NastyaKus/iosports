#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef double CGFloat;
#define CGFLOAT_MAX 1.7976931348623157e+308
#define CGFLOAT_MIN 2.2250738585072014e-308
typedef struct CGPoint { CGFloat x, y; } CGPoint;
typedef struct CGSize { CGFloat width, height; } CGSize;
typedef struct CGRect { CGPoint origin; CGSize size; } CGRect;
typedef struct CGContext *CGContextRef;
typedef struct CGColor *CGColorRef;
static inline CGRect CGRectMake(CGFloat x, CGFloat y, CGFloat w, CGFloat h) { CGRect r = { { x, y }, { w, h } }; return r; }
static inline CGPoint CGPointMake(CGFloat x, CGFloat y) { CGPoint p = { x, y }; return p; }
static inline CGSize CGSizeMake(CGFloat w, CGFloat h) { CGSize s = { w, h }; return s; }
extern const CGRect CGRectZero;
extern const CGPoint CGPointZero;
CGFloat CGRectGetMidX(CGRect); CGFloat CGRectGetMidY(CGRect); CGFloat CGRectGetMaxX(CGRect); CGFloat CGRectGetMaxY(CGRect);
CGFloat CGRectGetWidth(CGRect); CGFloat CGRectGetHeight(CGRect);
bool CGRectContainsPoint(CGRect, CGPoint);
CGRect CGRectInset(CGRect, CGFloat, CGFloat);
void CGContextSetRGBFillColor(CGContextRef, CGFloat, CGFloat, CGFloat, CGFloat);
void CGContextSetRGBStrokeColor(CGContextRef, CGFloat, CGFloat, CGFloat, CGFloat);
void CGContextSetFillColorWithColor(CGContextRef, CGColorRef);
CGFloat CGColorGetAlpha(CGColorRef);
const CGFloat *CGColorGetComponents(CGColorRef);
size_t CGColorGetNumberOfComponents(CGColorRef);
void CGContextSetStrokeColorWithColor(CGContextRef, CGColorRef);
void CGContextSetLineWidth(CGContextRef, CGFloat);
void CGContextFillRect(CGContextRef, CGRect);
void CGContextStrokeRect(CGContextRef, CGRect);
void CGContextFillEllipseInRect(CGContextRef, CGRect);
void CGContextStrokeEllipseInRect(CGContextRef, CGRect);
void CGContextMoveToPoint(CGContextRef, CGFloat, CGFloat);
void CGContextAddLineToPoint(CGContextRef, CGFloat, CGFloat);
void CGContextStrokePath(CGContextRef);

typedef struct CGAffineTransform { CGFloat a, b, c, d, tx, ty; } CGAffineTransform;
extern const CGAffineTransform CGAffineTransformIdentity;
CGAffineTransform CGAffineTransformMakeRotation(CGFloat angle);
CGAffineTransform CGAffineTransformMakeScale(CGFloat sx, CGFloat sy);
CGAffineTransform CGAffineTransformMakeTranslation(CGFloat tx, CGFloat ty);
CGAffineTransform CGAffineTransformRotate(CGAffineTransform t, CGFloat angle);
CGAffineTransform CGAffineTransformScale(CGAffineTransform t, CGFloat sx, CGFloat sy);
CGAffineTransform CGAffineTransformConcat(CGAffineTransform a, CGAffineTransform b);
CGRect CGRectOffset(CGRect, CGFloat, CGFloat);
bool CGRectIntersectsRect(CGRect, CGRect);
CGFloat CGRectGetMinX(CGRect); CGFloat CGRectGetMinY(CGRect);

typedef const struct CGPath *CGPathRef;
typedef struct CGPath *CGMutablePathRef;
CGMutablePathRef CGPathCreateMutable(void);
void CGPathMoveToPoint(CGMutablePathRef, const CGAffineTransform *, CGFloat, CGFloat);
CGPathRef CGPathCreateWithRect(CGRect rect, const CGAffineTransform *transform);
void CGPathRelease(CGPathRef);
void CGContextSetTextMatrix(CGContextRef, CGAffineTransform);
void CGContextSetTextPosition(CGContextRef, CGFloat x, CGFloat y);
CGPoint CGContextGetTextPosition(CGContextRef);
void CGPathAddLineToPoint(CGMutablePathRef, const CGAffineTransform *, CGFloat, CGFloat);
void CGPathAddCurveToPoint(CGMutablePathRef, const CGAffineTransform *, CGFloat, CGFloat, CGFloat, CGFloat, CGFloat, CGFloat);
void CGPathAddArc(CGMutablePathRef, const CGAffineTransform *, CGFloat x, CGFloat y, CGFloat r, CGFloat a0, CGFloat a1, bool clockwise);
void CGPathAddRect(CGMutablePathRef, const CGAffineTransform *, CGRect);
void CGPathAddEllipseInRect(CGMutablePathRef, const CGAffineTransform *, CGRect);
void CGPathCloseSubpath(CGMutablePathRef);
void CGPathRelease(CGPathRef);

typedef struct CGColorSpace *CGColorSpaceRef;
typedef struct CGGradient *CGGradientRef;
typedef struct CGImage *CGImageRef;
CGColorSpaceRef CGColorSpaceCreateDeviceRGB(void);
void CGColorSpaceRelease(CGColorSpaceRef);
CGGradientRef CGGradientCreateWithColorComponents(CGColorSpaceRef, const CGFloat *components, const CGFloat *locations, size_t count);
void CGGradientRelease(CGGradientRef);
enum { kCGGradientDrawsBeforeStartLocation = 1, kCGGradientDrawsAfterEndLocation = 2 };
enum { kCGImageAlphaPremultipliedLast = 1, kCGImageAlphaPremultipliedFirst = 2 };
enum { kCGBitmapByteOrder32Little = 2 << 12 };
CGContextRef CGBitmapContextCreate(void *data, size_t w, size_t h, size_t bpc, size_t bpr, CGColorSpaceRef, uint32_t info);
CGImageRef CGBitmapContextCreateImage(CGContextRef);
void CGContextRelease(CGContextRef);
void CGImageRelease(CGImageRef);
size_t CGImageGetWidth(CGImageRef); size_t CGImageGetHeight(CGImageRef);

void CGContextSaveGState(CGContextRef);
void CGContextRestoreGState(CGContextRef);
void CGContextTranslateCTM(CGContextRef, CGFloat, CGFloat);
void CGContextScaleCTM(CGContextRef, CGFloat, CGFloat);
void CGContextRotateCTM(CGContextRef, CGFloat);
void CGContextBeginPath(CGContextRef);
void CGContextAddPath(CGContextRef, CGPathRef);
void CGContextAddArc(CGContextRef, CGFloat x, CGFloat y, CGFloat r, CGFloat a0, CGFloat a1, int clockwise);
void CGContextAddRect(CGContextRef, CGRect);
void CGContextClosePath(CGContextRef);
void CGContextFillPath(CGContextRef);
void CGContextEOFillPath(CGContextRef);
void CGContextClip(CGContextRef);
void CGContextClipToRect(CGContextRef, CGRect);
void CGContextSetAlpha(CGContextRef, CGFloat);
void CGContextSetLineCap(CGContextRef, int);
void CGContextSetLineJoin(CGContextRef, int);
void CGContextSetLineDash(CGContextRef, CGFloat phase, const CGFloat *lengths, size_t count);
enum { kCGLineCapButt = 0, kCGLineCapRound = 1, kCGLineCapSquare = 2 };
void CGContextDrawLinearGradient(CGContextRef, CGGradientRef, CGPoint start, CGPoint end, uint32_t options);
void CGContextDrawRadialGradient(CGContextRef, CGGradientRef, CGPoint c0, CGFloat r0, CGPoint c1, CGFloat r1, uint32_t options);
void CGContextDrawImage(CGContextRef, CGRect, CGImageRef);
#ifdef __cplusplus
}
#endif
