// iosrt sample: NSKeyedArchiver / NSKeyedUnarchiver.
//  1. round trip of an object graph (custom NSCoding class, collections,
//     scalars, a reference cycle, dates, URLs, data) in binary and XML form;
//  2. decoding an archive written by another implementation
//     (Archive.app/fixture.archive, made by tests/make_archive_fixture.py
//     in Apple's format), including a $classes fallback chain and
//     +setClass:forClassName:.
#import <Foundation/Foundation.h>

@interface Player : NSObject <NSCoding>
@property (nonatomic, copy) NSString *name;
@property (nonatomic) NSInteger score;
@property (nonatomic) double ratio;
@property (nonatomic) BOOL active;
@property (nonatomic, strong) NSArray *items;
@property (nonatomic, strong) NSDictionary *stats;
@property (nonatomic, weak) Player *buddy;
@property (nonatomic, strong) Player *strongBuddy;
@end
@implementation Player
- (void)encodeWithCoder:(NSCoder *)c {
    [c encodeObject:self.name forKey:@"name"];
    [c encodeInteger:self.score forKey:@"score"];
    [c encodeDouble:self.ratio forKey:@"ratio"];
    [c encodeBool:self.active forKey:@"active"];
    [c encodeObject:self.items forKey:@"items"];
    [c encodeObject:self.stats forKey:@"stats"];
    [c encodeObject:self.strongBuddy forKey:@"buddy"];
}
- (instancetype)initWithCoder:(NSCoder *)c {
    if ((self = [super init])) {
        _name = [c decodeObjectForKey:@"name"];
        _score = [c decodeIntegerForKey:@"score"];
        _ratio = [c decodeDoubleForKey:@"ratio"];
        _active = [c decodeBoolForKey:@"active"];
        _items = [c decodeObjectForKey:@"items"];
        _stats = [c decodeObjectForKey:@"stats"];
        _strongBuddy = [c decodeObjectForKey:@"buddy"];
    }
    return self;
}
- (NSString *)summary {
    return [NSString stringWithFormat:@"%@ score=%ld ratio=%.2f active=%d items=%@ stats=%@", self.name, (long)self.score, self.ratio, self.active,
            [self.items componentsJoinedByString:@"|"], self.stats[@"level"]];
}
@end

/* stands in for a class the archive names but this app does not have */
@interface GameSettings : NSObject <NSCoding>
@property (nonatomic) NSInteger volume;
@property (nonatomic, copy) NSString *difficulty;
@end
@implementation GameSettings
- (void)encodeWithCoder:(NSCoder *)c { [c encodeInteger:self.volume forKey:@"volume"]; [c encodeObject:self.difficulty forKey:@"difficulty"]; }
- (instancetype)initWithCoder:(NSCoder *)c {
    if ((self = [super init])) { _volume = [c decodeIntegerForKey:@"volume"]; _difficulty = [c decodeObjectForKey:@"difficulty"]; }
    return self;
}
@end
@interface RenamedThing : NSObject <NSCoding>
@property (nonatomic, copy) NSString *tag;
@end
@implementation RenamedThing
- (void)encodeWithCoder:(NSCoder *)c { [c encodeObject:self.tag forKey:@"tag"]; }
- (instancetype)initWithCoder:(NSCoder *)c { if ((self = [super init])) _tag = [c decodeObjectForKey:@"tag"]; return self; }
@end

static void roundTrip(BOOL xml) {
    Player *a = [Player new], *b = [Player new];
    a.name = @"Worm"; a.score = 1234567890123; a.ratio = 0.75; a.active = YES;
    a.items = @[@"apple", @42, @3.5, [NSNull null]];
    a.stats = @{@"level": @7, @"tags": [NSSet setWithObjects:@"x", @"y", nil]};
    b.name = @"Bird"; b.items = @[]; b.stats = @{};
    a.strongBuddy = b; b.strongBuddy = a;   /* cycle */
    NSDate *when = [NSDate dateWithTimeIntervalSinceReferenceDate:123456.5];
    NSDictionary *root = @{@"player": a, @"when": when, @"url": [NSURL URLWithString:@"https://example.com/a?b=c"],
                           @"bytes": [NSData dataWithBytes:"\x01\x02\x03" length:3], @"list": [NSMutableArray arrayWithObjects:@"m", nil]};
    NSData *data;
    if (xml) {
        NSKeyedArchiver *ar = [[NSKeyedArchiver alloc] initRequiringSecureCoding:NO];
        ar.outputFormat = NSPropertyListXMLFormat_v1_0;
        [ar encodeObject:root forKey:NSKeyedArchiveRootObjectKey];
        [ar finishEncoding];
        data = ar.encodedData;
    } else {
        data = [NSKeyedArchiver archivedDataWithRootObject:root];
        [data writeToFile:[NSTemporaryDirectory() stringByAppendingPathComponent:@"roundtrip.archive"] atomically:YES];   /* checked by the test with plistlib */
    }
    NSString *head = [[NSString alloc] initWithData:[data subdataWithRange:NSMakeRange(0, xml ? 5 : 8)] encoding:NSUTF8StringEncoding];
    NSDictionary *back = [NSKeyedUnarchiver unarchiveObjectWithData:data];
    Player *p = back[@"player"];
    NSLog(@"%@ archive starts '%@'; decoded %@", xml ? @"xml" : @"binary", head, [p summary]);
    NSLog(@"  cycle intact: %d, buddy %@, tags %ld, when %.1f, url %@, bytes %lu, list mutable %d",
          p.strongBuddy.strongBuddy == p, p.strongBuddy.name, (long)[p.stats[@"tags"] count], [back[@"when"] timeIntervalSinceReferenceDate],
          [back[@"url"] absoluteString], (unsigned long)[back[@"bytes"] length], [back[@"list"] isKindOfClass:[NSMutableArray class]]);
    p.strongBuddy.strongBuddy = nil;   /* break the cycle */
    a.strongBuddy = nil;
}

int main(void) {
    @autoreleasepool {
        roundTrip(NO);
        roundTrip(YES);

        NSString *path = [[NSBundle mainBundle] pathForResource:@"fixture" ofType:@"archive"];
        [NSKeyedUnarchiver setClass:[RenamedThing class] forClassName:@"LegacyThing"];
        NSDictionary *fx = [NSKeyedUnarchiver unarchiveObjectWithFile:path];
        GameSettings *gs = fx[@"settings"];
        RenamedThing *rt = fx[@"legacy"];
        NSLog(@"fixture: settings is %@ volume=%ld difficulty=%@; legacy is %@ tag=%@; title '%@'; numbers %@; flag %@",
              NSStringFromClass([gs class]), (long)gs.volume, gs.difficulty, NSStringFromClass([rt class]), rt.tag,
              fx[@"title"], [fx[@"numbers"] componentsJoinedByString:@","], fx[@"flag"]);

        NSKeyedUnarchiver *un = [[NSKeyedUnarchiver alloc] initForReadingWithData:[NSData dataWithContentsOfFile:path]];
        NSLog(@"keyed: %d, contains root %d, missing key decodes to %@", un.allowsKeyedCoding,
              [un containsValueForKey:@"root"], [un decodeObjectForKey:@"nope"]);
        [un finishDecoding];

        NSData *junk = [@"not an archive" dataUsingEncoding:NSUTF8StringEncoding];
        NSLog(@"junk decodes to %@", [NSKeyedUnarchiver unarchiveObjectWithData:junk]);
    }
    return 0;
}
