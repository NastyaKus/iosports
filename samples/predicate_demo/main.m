// predicate_demo: NSPredicate format strings parsed and evaluated (filters,
// string operators with [cd], key paths, @count, IN / BETWEEN, AND / OR / NOT,
// ANY / ALL, %@ / %K / %d / %f substitutions, argument arrays).
#import <Foundation/Foundation.h>
#include <stdio.h>

@interface NSPredicate : NSObject
+ (NSPredicate *)predicateWithFormat:(NSString *)fmt, ...;
+ (NSPredicate *)predicateWithFormat:(NSString *)fmt argumentArray:(NSArray *)args;
- (BOOL)evaluateWithObject:(id)o;
@end
@interface NSArray (Filter)
- (NSArray *)filteredArrayUsingPredicate:(NSPredicate *)p;
@end

static void show(const char *what, NSArray *a) {
    printf("%-34s", what);
    for (id o in a) printf(" %s", [[o description] UTF8String]);
    printf("\n");
}

int main(void) {
    @autoreleasepool {
        NSArray *keys = @[ @"normalImage", @"selectedImage", @"normalBackgroundImage", @"frame", @"titleColor", @"PressedIMAGE" ];
        show("endswith[c] Image:", [keys filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"SELF endswith[c] %@", @"image"]]);
        show("beginswith normal:", [keys filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"SELF BEGINSWITH 'normal'"]]);
        show("contains[c] background:", [keys filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"SELF CONTAINS[c] 'background'"]]);
        show("like *Color:", [keys filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"SELF LIKE '*Color'"]]);
        show("matches regex:", [keys filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"SELF MATCHES '[a-z]+Image'"]]);
        show("not endswith Image:", [keys filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"NOT (SELF ENDSWITH 'Image')"]]);

        NSArray *people = @[
            @{ @"name": @"Ann", @"age": @31, @"tags": @[ @"a", @"b" ] },
            @{ @"name": @"Bob", @"age": @17, @"tags": @[] },
            @{ @"name": @"Céline", @"age": @45, @"tags": @[ @"c" ] },
        ];
        NSPredicate *p = [NSPredicate predicateWithFormat:@"age >= %d AND name != %@", 18, @"Ann"];
        show("age >= 18 AND name != Ann:", [[people filteredArrayUsingPredicate:p] valueForKey:@"name"]);
        show("%K < 40 OR name ==[c] 'bob':", [[people filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"%K < 40 OR name ==[c] 'bob'", @"age"]] valueForKey:@"name"]);
        show("tags.@count > 0:", [[people filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"tags.@count > 0"]] valueForKey:@"name"]);
        show("ANY tags == 'c':", [[people filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"ANY tags == 'c'"]] valueForKey:@"name"]);
        show("name IN {'Bob','Ann'}:", [[people filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"name IN {'Bob', 'Ann'}"]] valueForKey:@"name"]);
        show("age BETWEEN {20, 50}:", [[people filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"age BETWEEN {20, 50}"]] valueForKey:@"name"]);
        show("name ==[cd] 'celine':", [[people filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"name ==[cd] 'celine'"]] valueForKey:@"name"]);
        show("argumentArray age > 30:", [[people filteredArrayUsingPredicate:[NSPredicate predicateWithFormat:@"age > %@" argumentArray:@[ @30 ]]] valueForKey:@"name"]);
        printf("evaluate 2.5 > %%f(2.0): %d\n", [[NSPredicate predicateWithFormat:@"SELF > %f", 2.0] evaluateWithObject:@2.5]);
        printf("TRUEPREDICATE: %d, FALSEPREDICATE: %d\n", [[NSPredicate predicateWithFormat:@"TRUEPREDICATE"] evaluateWithObject:nil], [[NSPredicate predicateWithFormat:@"FALSEPREDICATE"] evaluateWithObject:nil]);
    }
    return 0;
}
