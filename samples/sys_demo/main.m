// iosrt sample: assorted system APIs games and older apps use:
// setjmp/longjmp, Mach clock services, BSD hash tables, URL file APIs.
#import <Foundation/Foundation.h>
#include <setjmp.h>

typedef struct { unsigned int tv_sec; int tv_nsec; } mach_ts;
extern int host_get_clock_service(unsigned host, int clock_id, unsigned *serv);
extern int clock_get_time(unsigned serv, mach_ts *t);
extern unsigned mach_host_self(void);
extern void *hash_create(int size);
extern char *hash_search(void *table, const char *key, char *datum, void (*replace)(char *));

static jmp_buf env;
static int depth;
static void dive(int n) {
    depth = n;
    if (n == 5) longjmp(env, 42);
    dive(n + 1);
}

int main(void) {
    @autoreleasepool {
        volatile int rounds = 0;
        int r = setjmp(env);
        rounds++;
        if (r == 0) dive(0);
        NSLog(@"setjmp returned %d after longjmp from depth %d, rounds %d", r, depth, rounds);
        if (setjmp(env) == 0) longjmp(env, 0);
        else NSLog(@"longjmp(env, 0) makes setjmp return 1");

        unsigned sys = 0, cal = 0;
        int k1 = host_get_clock_service(mach_host_self(), 0, &sys), k2 = host_get_clock_service(mach_host_self(), 1, &cal);
        mach_ts a, b, c;
        clock_get_time(sys, &a); clock_get_time(sys, &b); clock_get_time(cal, &c);
        double wall = [[NSDate date] timeIntervalSince1970];
        NSLog(@"clock services ok: %d, uptime monotonic: %d, calendar near NSDate: %d", k1 == 0 && k2 == 0,
              (b.tv_sec > a.tv_sec) || (b.tv_sec == a.tv_sec && b.tv_nsec >= a.tv_nsec), fabs(c.tv_sec - wall) < 5);

        void *h = hash_create(16);
        hash_search(h, "worm", "long", NULL);
        hash_search(h, "apple", "red", NULL);
        hash_search(h, "worm", "longer", NULL);
        NSLog(@"hash: worm=%s apple=%s missing=%s", hash_search(h, "worm", NULL, NULL), hash_search(h, "apple", NULL, NULL),
              hash_search(h, "nope", NULL, NULL) ? "found" : "(null)");

        NSFileManager *fm = [NSFileManager defaultManager];
        NSURL *docs = [fm URLForDirectory:NSDocumentDirectory inDomain:NSUserDomainMask appropriateForURL:nil create:YES error:NULL];
        NSURL *dir = [docs URLByAppendingPathComponent:@"urlio"];
        [fm removeItemAtURL:dir error:NULL];
        BOOL made = [fm createDirectoryAtURL:dir withIntermediateDirectories:YES attributes:nil error:NULL];
        NSURL *f = [dir URLByAppendingPathComponent:@"a.txt"];
        BOOL wrote = [@"hello" writeToURL:f atomically:YES encoding:NSUTF8StringEncoding error:NULL];
        NSURL *g = [dir URLByAppendingPathComponent:@"b.txt"];
        BOOL copied = [fm copyItemAtURL:f toURL:g error:NULL];
        NSArray *items = [fm contentsOfDirectoryAtURL:dir includingPropertiesForKeys:nil options:0 error:NULL];
        NSMutableArray *names = [NSMutableArray array];
        for (NSURL *u in items) [names addObject:u.lastPathComponent];
        [names sortUsingSelector:@selector(compare:)];
        NSString *back = [NSString stringWithContentsOfURL:g encoding:NSUTF8StringEncoding error:NULL];
        [@{@"k": @"v"} writeToURL:[dir URLByAppendingPathComponent:@"d.plist"] atomically:YES];
        NSDictionary *d = [NSDictionary dictionaryWithContentsOfURL:[dir URLByAppendingPathComponent:@"d.plist"]];
        NSError *err = nil;
        BOOL removedMissing = [fm removeItemAtURL:[dir URLByAppendingPathComponent:@"nope"] error:&err];
        BOOL removed = [fm removeItemAtURL:dir error:NULL];
        NSLog(@"url io: made %d wrote %d copied %d items %@ read '%@' plist %@ missing-remove %d (error %d) removed %d",
              made, wrote, copied, [names componentsJoinedByString:@","], back, d[@"k"], removedMissing, err != nil, removed);
    }
    return 0;
}
