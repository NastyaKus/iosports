#pragma once
#include <stdbool.h>
typedef double CGFloat;
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
void CGContextSetStrokeColorWithColor(CGContextRef, CGColorRef);
void CGContextSetLineWidth(CGContextRef, CGFloat);
void CGContextFillRect(CGContextRef, CGRect);
void CGContextStrokeRect(CGContextRef, CGRect);
void CGContextFillEllipseInRect(CGContextRef, CGRect);
void CGContextStrokeEllipseInRect(CGContextRef, CGRect);
void CGContextMoveToPoint(CGContextRef, CGFloat, CGFloat);
void CGContextAddLineToPoint(CGContextRef, CGFloat, CGFloat);
void CGContextStrokePath(CGContextRef);
