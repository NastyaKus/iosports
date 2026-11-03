// iosrt sample: Objective-C runtime + Foundation feature tour (ARC).
#import <Foundation/Foundation.h>
#import <objc/runtime.h>

@protocol Greeter <NSObject>
- (NSString *)greet:(NSString *)who;
@optional
- (void)optionalThing;
@end

@interface Animal : NSObject <Greeter>
@property (nonatomic, copy) NSString *name;
@property (nonatomic) NSInteger legs;
@property (nonatomic, weak) Animal *friend;
+ (instancetype)animalNamed:(NSString *)name legs:(NSInteger)legs;
- (NSString *)sound;
@end

static int g_deallocs;
static int g_initialized;

@implementation Animal
+ (void)initialize { if (self == [Animal class]) g_initialized++; }
+ (instancetype)animalNamed:(NSString *)name legs:(NSInteger)legs {
    Animal *a = [[self alloc] init];
    a.name = name;
    a.legs = legs;
    return a;
}
- (NSString *)sound { return @"..."; }
- (NSString *)greet:(NSString *)who { return [NSString stringWithFormat:@"%@ says %@ to %@", self.name, [self sound], who]; }
- (NSString *)description { return [NSString stringWithFormat:@"<%@ %@ legs=%ld>", NSStringFromClass([self class]), self.name, (long)self.legs]; }
- (void)dealloc { g_deallocs++; }
@end

@interface Dog : Animal
@end
@implementation Dog
- (NSString *)sound { return @"woof"; }
- (NSString *)greet:(NSString *)who { return [[super greet:who] stringByAppendingString:@"!"]; }
@end

@interface Animal (Extras)
- (NSString *)shout;
@end
@implementation Animal (Extras)
- (NSString *)shout { return [[self sound] uppercaseString]; }
@end

@interface NSString (Reverse)
- (NSString *)iosrt_reversed;
@end
@implementation NSString (Reverse)
- (NSString *)iosrt_reversed {
    NSMutableString *r = [NSMutableString string];
    for (NSInteger i = (NSInteger)self.length - 1; i >= 0; i--) [r appendFormat:@"%C", [self characterAtIndex:(NSUInteger)i]];
    return r;
}
@end

@interface Forwarder : NSObject
@property (nonatomic, strong) id target;
@end
@implementation Forwarder
- (id)forwardingTargetForSelector:(SEL)s { return self.target; }
@end

static int g_loads;
@interface Loader : NSObject @end
@implementation Loader
+ (void)load { g_loads++; }
@end

int main(int argc, char **argv) {
    @autoreleasepool {
        NSLog(@"== classes & messaging");
        Dog *d = [Dog animalNamed:@"Rex" legs:4];
        Animal *a = [Animal animalNamed:@"Generic" legs:2];
        NSLog(@"%@", d);
        NSLog(@"%@", [d greet:@"Kirill"]);
        NSLog(@"%@ / %@", [a greet:@"you"], [d shout]);
        NSLog(@"isKindOfClass Animal: %d, isMemberOfClass Animal: %d", [d isKindOfClass:[Animal class]], [d isMemberOfClass:[Animal class]]);
        NSLog(@"respondsTo shout: %d, optionalThing: %d", [d respondsToSelector:@selector(shout)], [d respondsToSelector:@selector(optionalThing)]);
        NSLog(@"conformsTo Greeter: %d", [d conformsToProtocol:@protocol(Greeter)]);
        NSLog(@"superclass chain: %@ -> %@ -> %@", NSStringFromClass([d class]), NSStringFromClass([Dog superclass]), NSStringFromClass([Animal superclass]));
        NSLog(@"+load ran %d time(s), +initialize %d time(s)", g_loads, g_initialized);

        NSLog(@"== weak refs & dealloc");
        @autoreleasepool {
            Dog *tmp = [Dog animalNamed:@"Tmp" legs:3];
            a.friend = tmp;
            NSLog(@"friend before: %@", a.friend.name);
            tmp = nil;
        }
        NSLog(@"friend after release: %@, deallocs=%d", a.friend, g_deallocs);

        NSLog(@"== strings");
        NSString *s = @"Hello, iOS runtime";
        NSLog(@"len=%lu upper=%@ reversed=%@", (unsigned long)s.length, s.uppercaseString, [s iosrt_reversed]);
        NSLog(@"hasPrefix=%d contains=%d range=%lu", [s hasPrefix:@"Hello"], [s containsString:@"iOS"], (unsigned long)[s rangeOfString:@"runtime"].location);
        NSArray *parts = [@"a,b,c" componentsSeparatedByString:@","];
        NSLog(@"split=%@ joined=%@", parts, [parts componentsJoinedByString:@"-"]);
        NSLog(@"unicode: %@ (%lu units)", @"Привет, мир", (unsigned long)@"Привет, мир".length);
        NSLog(@"format: %d %.2f %@ %s %05ld", 42, 2.5, @"obj", "cstr", (long)7);
        NSLog(@"intValue=%d doubleValue=%.1f", [@"123" intValue], [@"4.5" doubleValue]);

        NSLog(@"== literals & collections");
        NSArray *nums = @[ @3, @1, @2, @(10 * 4), @YES ];
        NSDictionary *dict = @{ @"one" : @1, @"two" : @"zwei", @"list" : @[ @"x", @"y" ] };
        NSLog(@"array=%@", nums);
        NSLog(@"dict count=%lu two=%@ list[1]=%@", (unsigned long)dict.count, dict[@"two"], dict[@"list"][1]);
        NSMutableArray *m = [NSMutableArray array];
        for (NSNumber *n in nums) if ([n intValue] < 5) [m addObject:n];
        [m sortUsingComparator:^NSComparisonResult(NSNumber *x, NSNumber *y) { return [x compare:y]; }];
        NSLog(@"filtered+sorted=%@", [m componentsJoinedByString:@","]);
        __block NSInteger sum = 0;
        [nums enumerateObjectsUsingBlock:^(NSNumber *n, NSUInteger i, BOOL *stop) { sum += n.integerValue; if (i == 2) *stop = YES; }];
        NSLog(@"block enumeration sum of first 3 = %ld", (long)sum);
        NSMutableDictionary *md = [dict mutableCopy];
        md[@"three"] = @3;
        [md removeObjectForKey:@"one"];
        NSArray *keys = [md.allKeys sortedArrayUsingSelector:@selector(compare:)];
        NSLog(@"mutable dict keys=%@", [keys componentsJoinedByString:@" "]);

        NSLog(@"== blocks");
        int base = 100;
        NSInteger (^adder)(NSInteger) = ^NSInteger(NSInteger x) { return x + base; };
        __block int counter = 0;
        void (^inc)(void) = ^{ counter++; };
        inc(); inc(); inc();
        NSLog(@"adder(5)=%ld counter=%d", (long)adder(5), counter);
        NSMutableArray *blocks = [NSMutableArray array];
        for (int i = 0; i < 3; i++) [blocks addObject:[^{ return i * i; } copy]];
        int (^b2)(void) = blocks[2];
        NSLog(@"captured block from array: %d", b2());

        NSLog(@"== runtime API");
        Method m1 = class_getInstanceMethod([Dog class], @selector(sound));
        Method m2 = class_getInstanceMethod([Dog class], @selector(shout));
        NSLog(@"method names: %s %s", sel_getName(method_getName(m1)), sel_getName(method_getName(m2)));
        Forwarder *f = [Forwarder new];
        f.target = d;
        NSLog(@"forwarded: %@", [(Dog *)f sound]);
        static char key;
        objc_setAssociatedObject(d, &key, @"associated!", OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        NSLog(@"associated object: %@", objc_getAssociatedObject(d, &key));

        NSLog(@"== misc Foundation");
        NSNumber *pi = @3.14159;
        NSLog(@"NSNumber %@ int=%d bool=%d", pi, pi.intValue, @NO.boolValue);
        NSData *data = [@"hi!" dataUsingEncoding:NSUTF8StringEncoding];
        NSLog(@"data=%@ length=%lu", data, (unsigned long)data.length);
        NSLog(@"process arguments=%lu main thread=%d", (unsigned long)[NSProcessInfo processInfo].arguments.count, [NSThread isMainThread]);
        NSUserDefaults *ud = [NSUserDefaults standardUserDefaults];
        [ud setInteger:5 forKey:@"launches"];
        NSLog(@"defaults launches=%ld", (long)[ud integerForKey:@"launches"]);
        __block int fired = 0;
        [NSTimer scheduledTimerWithTimeInterval:0.01 repeats:YES block:^(NSTimer *t) { if (++fired == 3) [t invalidate]; }];
        dispatch_async(dispatch_get_main_queue(), ^{ NSLog(@"main-queue block ran"); });
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.2]];
        NSLog(@"timer fired %d times", fired);
        [[NSNotificationCenter defaultCenter] addObserverForName:@"Ping" object:nil queue:nil usingBlock:^(NSNotification *n) {
            NSLog(@"notification %@ userInfo=%@", n.name, n.userInfo[@"k"]);
        }];
        [[NSNotificationCenter defaultCenter] postNotificationName:@"Ping" object:nil userInfo:@{ @"k" : @"v" }];
    }
    NSLog(@"done, deallocs=%d", g_deallocs);
    return 0;
}
