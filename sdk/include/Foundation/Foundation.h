/* iosrt mini-SDK: the Foundation subset implemented by the runtime. */
#pragma once
#include <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dispatch/dispatch.h>

typedef long NSInteger;
typedef unsigned long NSUInteger;
typedef double NSTimeInterval;
typedef unsigned short unichar;
typedef struct _NSRange { NSUInteger location, length; } NSRange;
static inline NSRange NSMakeRange(NSUInteger l, NSUInteger n) { NSRange r = { l, n }; return r; }
#define NSNotFound ((NSInteger)0x7fffffffffffffffL)
typedef enum { NSOrderedAscending = -1, NSOrderedSame, NSOrderedDescending } NSComparisonResult;
typedef NSComparisonResult (^NSComparator)(id, id);
enum { NSASCIIStringEncoding = 1, NSUTF8StringEncoding = 4, NSUTF16StringEncoding = 10 };
typedef NSUInteger NSStringEncoding;
#define NS_ENUM(T, N) enum N : T N; enum N : T
#define NS_OPTIONS(T, N) enum N : T N; enum N : T
#define NS_ASSUME_NONNULL_BEGIN
#define NS_ASSUME_NONNULL_END
@class NSString, NSArray, NSDictionary, NSData, NSDate, NSError, NSSet, NSEnumerator, NSNotification;

@protocol NSObject
- (BOOL)isEqual:(id)object;
- (NSUInteger)hash;
- (Class)class;
- (Class)superclass;
- (id)self;
- (BOOL)isKindOfClass:(Class)c;
- (BOOL)isMemberOfClass:(Class)c;
- (BOOL)respondsToSelector:(SEL)s;
- (BOOL)conformsToProtocol:(Protocol *)p;
- (id)performSelector:(SEL)s;
- (id)performSelector:(SEL)s withObject:(id)o;
- (id)performSelector:(SEL)s withObject:(id)a withObject:(id)b;
- (NSString *)description;
@end
@protocol NSCopying - (id)copyWithZone:(void *)zone; @end
@protocol NSMutableCopying - (id)mutableCopyWithZone:(void *)zone; @end
typedef struct { unsigned long state; id __unsafe_unretained *itemsPtr; unsigned long *mutationsPtr; unsigned long extra[5]; } NSFastEnumerationState;
@protocol NSFastEnumeration
- (NSUInteger)countByEnumeratingWithState:(NSFastEnumerationState *)state objects:(id __unsafe_unretained [])buffer count:(NSUInteger)len;
@end

__attribute__((objc_root_class))
@interface NSObject <NSObject> { Class isa; }
+ (void)load;
+ (void)initialize;
+ (instancetype)alloc;
+ (instancetype)new;
+ (Class)class;
+ (Class)superclass;
+ (BOOL)instancesRespondToSelector:(SEL)s;
+ (BOOL)isSubclassOfClass:(Class)c;
+ (NSString *)description;
- (instancetype)init;
- (void)dealloc;
- (id)copy;
- (id)mutableCopy;
- (IMP)methodForSelector:(SEL)s;
- (void)doesNotRecognizeSelector:(SEL)s;
- (id)forwardingTargetForSelector:(SEL)s;
- (void)performSelector:(SEL)s withObject:(id)o afterDelay:(NSTimeInterval)d;
- (NSString *)debugDescription;
@end

@interface NSString : NSObject <NSCopying, NSMutableCopying>
+ (instancetype)string;
+ (instancetype)stringWithUTF8String:(const char *)s;
+ (instancetype)stringWithString:(NSString *)s;
+ (instancetype)stringWithFormat:(NSString *)fmt, ... __attribute__((format(__NSString__, 1, 2)));
+ (instancetype)stringWithCharacters:(const unichar *)c length:(NSUInteger)n;
+ (instancetype)stringWithContentsOfFile:(NSString *)path encoding:(NSStringEncoding)e error:(NSError **)err;
- (instancetype)initWithUTF8String:(const char *)s;
- (instancetype)initWithFormat:(NSString *)fmt, ... __attribute__((format(__NSString__, 1, 2)));
- (instancetype)initWithFormat:(NSString *)fmt arguments:(va_list)args;
- (instancetype)initWithString:(NSString *)s;
- (instancetype)initWithBytes:(const void *)b length:(NSUInteger)n encoding:(NSStringEncoding)e;
@property (readonly) NSUInteger length;
- (unichar)characterAtIndex:(NSUInteger)i;
@property (readonly) const char *UTF8String;
- (const char *)cStringUsingEncoding:(NSStringEncoding)e;
- (BOOL)isEqualToString:(NSString *)s;
- (NSComparisonResult)compare:(NSString *)s;
- (NSComparisonResult)caseInsensitiveCompare:(NSString *)s;
- (BOOL)hasPrefix:(NSString *)s;
- (BOOL)hasSuffix:(NSString *)s;
- (BOOL)containsString:(NSString *)s;
- (NSRange)rangeOfString:(NSString *)s;
- (NSString *)stringByAppendingString:(NSString *)s;
- (NSString *)stringByAppendingFormat:(NSString *)fmt, ... __attribute__((format(__NSString__, 1, 2)));
- (NSString *)substringFromIndex:(NSUInteger)i;
- (NSString *)substringToIndex:(NSUInteger)i;
- (NSString *)substringWithRange:(NSRange)r;
@property (readonly, copy) NSString *uppercaseString;
@property (readonly, copy) NSString *lowercaseString;
@property (readonly, copy) NSString *capitalizedString;
@property (readonly) int intValue;
@property (readonly) NSInteger integerValue;
@property (readonly) double doubleValue;
@property (readonly) float floatValue;
@property (readonly) BOOL boolValue;
- (NSArray *)componentsSeparatedByString:(NSString *)sep;
- (NSString *)stringByReplacingOccurrencesOfString:(NSString *)a withString:(NSString *)b;
@property (readonly, copy) NSString *lastPathComponent;
@property (readonly, copy) NSString *pathExtension;
@property (readonly, copy) NSString *stringByDeletingLastPathComponent;
@property (readonly, copy) NSString *stringByDeletingPathExtension;
- (NSString *)stringByAppendingPathComponent:(NSString *)c;
- (NSData *)dataUsingEncoding:(NSStringEncoding)e;
- (BOOL)writeToFile:(NSString *)path atomically:(BOOL)a encoding:(NSStringEncoding)e error:(NSError **)err;
@end
@interface NSMutableString : NSString
+ (instancetype)stringWithCapacity:(NSUInteger)n;
- (void)appendString:(NSString *)s;
- (void)appendFormat:(NSString *)fmt, ... __attribute__((format(__NSString__, 1, 2)));
- (void)setString:(NSString *)s;
- (void)insertString:(NSString *)s atIndex:(NSUInteger)i;
- (void)deleteCharactersInRange:(NSRange)r;
- (void)replaceCharactersInRange:(NSRange)r withString:(NSString *)s;
@end

@interface NSNumber : NSObject <NSCopying>
+ (NSNumber *)numberWithInt:(int)v;
+ (NSNumber *)numberWithInteger:(NSInteger)v;
+ (NSNumber *)numberWithLong:(long)v;
+ (NSNumber *)numberWithLongLong:(long long)v;
+ (NSNumber *)numberWithUnsignedInt:(unsigned)v;
+ (NSNumber *)numberWithUnsignedInteger:(NSUInteger)v;
+ (NSNumber *)numberWithUnsignedLong:(unsigned long)v;
+ (NSNumber *)numberWithUnsignedLongLong:(unsigned long long)v;
+ (NSNumber *)numberWithChar:(char)v;
+ (NSNumber *)numberWithUnsignedChar:(unsigned char)v;
+ (NSNumber *)numberWithShort:(short)v;
+ (NSNumber *)numberWithUnsignedShort:(unsigned short)v;
+ (NSNumber *)numberWithFloat:(float)v;
+ (NSNumber *)numberWithDouble:(double)v;
+ (NSNumber *)numberWithBool:(BOOL)v;
@property (readonly) int intValue;
@property (readonly) NSInteger integerValue;
@property (readonly) long long longLongValue;
@property (readonly) double doubleValue;
@property (readonly) float floatValue;
@property (readonly) BOOL boolValue;
@property (readonly, copy) NSString *stringValue;
- (BOOL)isEqualToNumber:(NSNumber *)n;
- (NSComparisonResult)compare:(NSNumber *)n;
@end
@interface NSNull : NSObject
+ (NSNull *)null;
@end

@interface NSArray<ObjectType> : NSObject <NSCopying, NSMutableCopying, NSFastEnumeration>
+ (instancetype)array;
+ (instancetype)arrayWithObject:(ObjectType)o;
+ (instancetype)arrayWithObjects:(const ObjectType [])objs count:(NSUInteger)n;
+ (instancetype)arrayWithObjects:(ObjectType)first, ... __attribute__((sentinel));
+ (instancetype)arrayWithArray:(NSArray<ObjectType> *)a;
@property (readonly) NSUInteger count;
- (ObjectType)objectAtIndex:(NSUInteger)i;
- (ObjectType)objectAtIndexedSubscript:(NSUInteger)i;
@property (readonly) ObjectType firstObject;
@property (readonly) ObjectType lastObject;
- (BOOL)containsObject:(ObjectType)o;
- (NSUInteger)indexOfObject:(ObjectType)o;
- (NSArray<ObjectType> *)arrayByAddingObject:(ObjectType)o;
- (NSString *)componentsJoinedByString:(NSString *)sep;
- (void)enumerateObjectsUsingBlock:(void (^)(ObjectType obj, NSUInteger idx, BOOL *stop))block;
- (NSArray<ObjectType> *)sortedArrayUsingSelector:(SEL)cmp;
- (NSArray<ObjectType> *)sortedArrayUsingComparator:(NSComparator)cmp;
- (NSEnumerator *)objectEnumerator;
- (NSEnumerator *)reverseObjectEnumerator;
- (NSArray<ObjectType> *)subarrayWithRange:(NSRange)r;
@end
@interface NSMutableArray<ObjectType> : NSArray<ObjectType>
+ (instancetype)arrayWithCapacity:(NSUInteger)n;
- (void)addObject:(ObjectType)o;
- (void)insertObject:(ObjectType)o atIndex:(NSUInteger)i;
- (void)removeObjectAtIndex:(NSUInteger)i;
- (void)removeLastObject;
- (void)removeObject:(ObjectType)o;
- (void)removeAllObjects;
- (void)replaceObjectAtIndex:(NSUInteger)i withObject:(ObjectType)o;
- (void)setObject:(ObjectType)o atIndexedSubscript:(NSUInteger)i;
- (void)addObjectsFromArray:(NSArray<ObjectType> *)a;
- (void)sortUsingSelector:(SEL)cmp;
- (void)sortUsingComparator:(NSComparator)cmp;
@end
@interface NSEnumerator : NSObject <NSFastEnumeration>
- (id)nextObject;
@property (readonly, copy) NSArray *allObjects;
@end
@interface NSDictionary<KeyType, ObjectType> : NSObject <NSCopying, NSMutableCopying, NSFastEnumeration>
+ (instancetype)dictionary;
+ (instancetype)dictionaryWithObjects:(const ObjectType [])objs forKeys:(const KeyType [])keys count:(NSUInteger)n;
+ (instancetype)dictionaryWithObject:(ObjectType)o forKey:(KeyType)k;
+ (instancetype)dictionaryWithObjectsAndKeys:(id)first, ... __attribute__((sentinel));
@property (readonly) NSUInteger count;
- (ObjectType)objectForKey:(KeyType)k;
- (ObjectType)objectForKeyedSubscript:(KeyType)k;
@property (readonly, copy) NSArray<KeyType> *allKeys;
@property (readonly, copy) NSArray<ObjectType> *allValues;
- (void)enumerateKeysAndObjectsUsingBlock:(void (^)(KeyType key, ObjectType obj, BOOL *stop))block;
@end
@interface NSMutableDictionary<KeyType, ObjectType> : NSDictionary<KeyType, ObjectType>
+ (instancetype)dictionaryWithCapacity:(NSUInteger)n;
- (void)setObject:(ObjectType)o forKey:(KeyType)k;
- (void)setObject:(ObjectType)o forKeyedSubscript:(KeyType)k;
- (void)removeObjectForKey:(KeyType)k;
- (void)removeAllObjects;
@end
@interface NSSet<ObjectType> : NSObject <NSCopying, NSFastEnumeration>
+ (instancetype)set;
+ (instancetype)setWithObject:(ObjectType)o;
+ (instancetype)setWithArray:(NSArray<ObjectType> *)a;
@property (readonly) NSUInteger count;
- (ObjectType)anyObject;
- (BOOL)containsObject:(ObjectType)o;
@property (readonly, copy) NSArray<ObjectType> *allObjects;
@end
@interface NSMutableSet<ObjectType> : NSSet<ObjectType>
- (void)addObject:(ObjectType)o;
- (void)removeObject:(ObjectType)o;
@end

@interface NSData : NSObject <NSCopying>
+ (instancetype)data;
+ (instancetype)dataWithBytes:(const void *)b length:(NSUInteger)n;
+ (instancetype)dataWithContentsOfFile:(NSString *)path;
@property (readonly) const void *bytes;
@property (readonly) NSUInteger length;
- (BOOL)writeToFile:(NSString *)path atomically:(BOOL)a;
@end
@interface NSMutableData : NSData
+ (instancetype)dataWithLength:(NSUInteger)n;
@property (readonly) void *mutableBytes;
- (void)appendBytes:(const void *)b length:(NSUInteger)n;
- (void)appendData:(NSData *)d;
@end
@interface NSDate : NSObject
+ (instancetype)date;
+ (instancetype)dateWithTimeIntervalSinceNow:(NSTimeInterval)t;
+ (instancetype)dateWithTimeIntervalSince1970:(NSTimeInterval)t;
+ (NSTimeInterval)timeIntervalSinceReferenceDate;
@property (readonly) NSTimeInterval timeIntervalSince1970;
@property (readonly) NSTimeInterval timeIntervalSinceNow;
- (NSTimeInterval)timeIntervalSinceDate:(NSDate *)d;
- (NSDate *)dateByAddingTimeInterval:(NSTimeInterval)t;
@end
@interface NSProcessInfo : NSObject
+ (NSProcessInfo *)processInfo;
@property (readonly, copy) NSArray<NSString *> *arguments;
@property (readonly, copy) NSDictionary<NSString *, NSString *> *environment;
@property (readonly, copy) NSString *processName;
@property (readonly) int processIdentifier;
@property (readonly) NSTimeInterval systemUptime;
@property (readonly) NSUInteger processorCount;
@end
@interface NSBundle : NSObject
+ (NSBundle *)mainBundle;
@property (readonly, copy) NSString *bundlePath;
@property (readonly, copy) NSString *resourcePath;
@property (readonly, copy) NSString *bundleIdentifier;
@property (readonly, copy) NSDictionary *infoDictionary;
- (id)objectForInfoDictionaryKey:(NSString *)k;
- (NSString *)pathForResource:(NSString *)name ofType:(NSString *)ext;
- (NSString *)localizedStringForKey:(NSString *)k value:(NSString *)v table:(NSString *)t;
@end
#define NSLocalizedString(key, comment) [[NSBundle mainBundle] localizedStringForKey:(key) value:@"" table:nil]
@interface NSException : NSObject
+ (NSException *)exceptionWithName:(NSString *)n reason:(NSString *)r userInfo:(NSDictionary *)u;
+ (void)raise:(NSString *)name format:(NSString *)fmt, ...;
- (void)raise;
@property (readonly, copy) NSString *name;
@property (readonly, copy) NSString *reason;
@end
@interface NSThread : NSObject
+ (BOOL)isMainThread;
+ (void)sleepForTimeInterval:(NSTimeInterval)t;
+ (void)detachNewThreadSelector:(SEL)s toTarget:(id)t withObject:(id)o;
@end
@interface NSTimer : NSObject
+ (NSTimer *)scheduledTimerWithTimeInterval:(NSTimeInterval)t target:(id)target selector:(SEL)s userInfo:(id)u repeats:(BOOL)r;
+ (NSTimer *)scheduledTimerWithTimeInterval:(NSTimeInterval)t repeats:(BOOL)r block:(void (^)(NSTimer *timer))block;
- (void)invalidate;
@property (readonly, getter=isValid) BOOL valid;
@property (readonly) id userInfo;
@end
@interface NSRunLoop : NSObject
+ (NSRunLoop *)currentRunLoop;
+ (NSRunLoop *)mainRunLoop;
- (void)run;
- (void)runUntilDate:(NSDate *)d;
@end
@interface NSUserDefaults : NSObject
+ (NSUserDefaults *)standardUserDefaults;
- (id)objectForKey:(NSString *)k;
- (void)setObject:(id)o forKey:(NSString *)k;
- (NSInteger)integerForKey:(NSString *)k;
- (void)setInteger:(NSInteger)v forKey:(NSString *)k;
- (BOOL)boolForKey:(NSString *)k;
- (void)setBool:(BOOL)v forKey:(NSString *)k;
- (BOOL)synchronize;
@end
@interface NSFileManager : NSObject
+ (NSFileManager *)defaultManager;
- (BOOL)fileExistsAtPath:(NSString *)p;
- (NSArray<NSString *> *)contentsOfDirectoryAtPath:(NSString *)p error:(NSError **)e;
- (BOOL)createDirectoryAtPath:(NSString *)p withIntermediateDirectories:(BOOL)i attributes:(NSDictionary *)a error:(NSError **)e;
- (BOOL)removeItemAtPath:(NSString *)p error:(NSError **)e;
@end
@interface NSNotification : NSObject
@property (readonly, copy) NSString *name;
@property (readonly) id object;
@property (readonly, copy) NSDictionary *userInfo;
@end
@interface NSNotificationCenter : NSObject
+ (NSNotificationCenter *)defaultCenter;
- (void)addObserver:(id)o selector:(SEL)s name:(NSString *)n object:(id)obj;
- (id)addObserverForName:(NSString *)n object:(id)obj queue:(id)q usingBlock:(void (^)(NSNotification *note))block;
- (void)postNotificationName:(NSString *)n object:(id)obj;
- (void)postNotificationName:(NSString *)n object:(id)obj userInfo:(NSDictionary *)u;
- (void)removeObserver:(id)o;
@end
@interface NSIndexPath : NSObject <NSCopying>
+ (instancetype)indexPathWithIndex:(NSUInteger)index;
@property (readonly) NSUInteger length;
- (NSUInteger)indexAtPosition:(NSUInteger)position;
@end
@interface NSAutoreleasePool : NSObject
- (void)drain;
@end

#ifdef __cplusplus
extern "C" {
#endif
void NSLog(NSString *fmt, ...) __attribute__((format(__NSString__, 1, 2)));
NSString *NSStringFromClass(Class c);
Class NSClassFromString(NSString *s);
NSString *NSStringFromSelector(SEL s);
SEL NSSelectorFromString(NSString *s);
NSString *NSHomeDirectory(void);
NSString *NSTemporaryDirectory(void);
NSArray<NSString *> *NSSearchPathForDirectoriesInDomains(NSUInteger dir, NSUInteger mask, BOOL expand);
enum { NSDocumentDirectory = 9, NSCachesDirectory = 13, NSUserDomainMask = 1 };
extern NSString *const NSDefaultRunLoopMode;
#ifdef __cplusplus
}
#endif
