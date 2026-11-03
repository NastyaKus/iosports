/* iosrt mini-SDK: CoreText (the subset the runtime implements) */
#ifndef IOSRT_CORETEXT_H
#define IOSRT_CORETEXT_H
#include <CoreGraphics/CoreGraphics.h>
#include <Foundation/Foundation.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef const struct __CTFont *CTFontRef;
typedef const struct __CTLine *CTLineRef;
typedef const struct __CTRun *CTRunRef;
typedef const struct __CTFramesetter *CTFramesetterRef;
typedef const struct __CTFrame *CTFrameRef;
typedef const struct __CTParagraphStyle *CTParagraphStyleRef;
typedef const struct __CFAttributedString *CFAttributedStringRef;
typedef const struct __CFString *CFStringRef;
typedef const struct __CFDictionary *CFDictionaryRef;
typedef const struct __CFArray *CFArrayRef;
typedef const void *CFTypeRef;
typedef long CFIndex;
typedef struct { CFIndex location, length; } CFRange;
static inline CFRange CFRangeMake(CFIndex l, CFIndex n) { CFRange r = { l, n }; return r; }
typedef uint16_t CGGlyph;
typedef uint32_t CTFontSymbolicTraits;
enum { kCTFontTraitBold = 1 << 1, kCTFontTraitItalic = 1 << 0 };
typedef uint8_t CTTextAlignment;
enum { kCTTextAlignmentLeft = 0, kCTTextAlignmentRight = 1, kCTTextAlignmentCenter = 2, kCTTextAlignmentJustified = 3, kCTTextAlignmentNatural = 4 };
typedef uint32_t CTParagraphStyleSpecifier;
enum { kCTParagraphStyleSpecifierAlignment = 0, kCTParagraphStyleSpecifierLineSpacingAdjustment = 15 };
typedef struct { CTParagraphStyleSpecifier spec; size_t valueSize; const void *value; } CTParagraphStyleSetting;
typedef uint32_t CTLineTruncationType;
enum { kCTLineTruncationStart = 0, kCTLineTruncationEnd = 1, kCTLineTruncationMiddle = 2 };
extern const CFStringRef kCTFontAttributeName, kCTForegroundColorAttributeName, kCTParagraphStyleAttributeName, kCTKernAttributeName;
void CFRelease(CFTypeRef);
CFTypeRef CFRetain(CFTypeRef);
CFIndex CFArrayGetCount(CFArrayRef);
const void *CFArrayGetValueAtIndex(CFArrayRef, CFIndex);
CTFontRef CTFontCreateWithName(CFStringRef name, CGFloat size, const CGAffineTransform *matrix);
CTFontRef CTFontCreateCopyWithSymbolicTraits(CTFontRef, CGFloat size, const CGAffineTransform *m, CTFontSymbolicTraits value, CTFontSymbolicTraits mask);
CGFloat CTFontGetSize(CTFontRef);
CGFloat CTFontGetAscent(CTFontRef);
CGFloat CTFontGetDescent(CTFontRef);
CGFloat CTFontGetXHeight(CTFontRef);
CTFontSymbolicTraits CTFontGetSymbolicTraits(CTFontRef);
bool CTFontGetGlyphsForCharacters(CTFontRef, const unichar *chars, CGGlyph *glyphs, CFIndex n);
double CTFontGetAdvancesForGlyphs(CTFontRef, int orientation, const CGGlyph *glyphs, CGSize *advances, CFIndex n);
CTParagraphStyleRef CTParagraphStyleCreate(const CTParagraphStyleSetting *settings, size_t n);
CTLineRef CTLineCreateWithAttributedString(CFAttributedStringRef);
double CTLineGetTypographicBounds(CTLineRef, CGFloat *ascent, CGFloat *descent, CGFloat *leading);
CGRect CTLineGetImageBounds(CTLineRef, CGContextRef);
CFArrayRef CTLineGetGlyphRuns(CTLineRef);
CFIndex CTLineGetGlyphCount(CTLineRef);
CFRange CTLineGetStringRange(CTLineRef);
CGFloat CTLineGetOffsetForStringIndex(CTLineRef, CFIndex idx, CGFloat *secondary);
CFIndex CTLineGetStringIndexForPosition(CTLineRef, CGPoint p);
double CTLineGetPenOffsetForFlush(CTLineRef, CGFloat flush, double width);
CTLineRef CTLineCreateTruncatedLine(CTLineRef, double width, CTLineTruncationType type, CTLineRef token);
void CTLineDraw(CTLineRef, CGContextRef);
CFIndex CTRunGetGlyphCount(CTRunRef);
CFDictionaryRef CTRunGetAttributes(CTRunRef);
CFRange CTRunGetStringRange(CTRunRef);
void CTRunGetPositions(CTRunRef, CFRange, CGPoint *);
CTFramesetterRef CTFramesetterCreateWithAttributedString(CFAttributedStringRef);
CGSize CTFramesetterSuggestFrameSizeWithConstraints(CTFramesetterRef, CFRange, CFDictionaryRef, CGSize constraints, CFRange *fit);
CTFrameRef CTFramesetterCreateFrame(CTFramesetterRef, CFRange, CGPathRef path, CFDictionaryRef attrs);
CFArrayRef CTFrameGetLines(CTFrameRef);
void CTFrameGetLineOrigins(CTFrameRef, CFRange, CGPoint *origins);
CFRange CTFrameGetVisibleStringRange(CTFrameRef);
void CTFrameDraw(CTFrameRef, CGContextRef);
#ifdef __cplusplus
}
#endif
#endif
