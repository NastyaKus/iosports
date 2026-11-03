// iosrt sample: C++ on the Apple libc++ ABI (std::__1, alternate string layout)
// mixed with Objective-C (Objective-C++).
#import <Foundation/Foundation.h>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <thread>
#include <functional>
#include <algorithm>
#include <cstdio>

struct Shape { virtual ~Shape() {} virtual double area() const = 0; virtual const char *name() const = 0; };
struct Square : Shape { double s; explicit Square(double v) : s(v) {} double area() const override { return s * s; } const char *name() const override { return "square"; } };
struct Rect : Shape { double w, h; Rect(double a, double b) : w(a), h(b) {} double area() const override { return w * h; } const char *name() const override { return "rect"; } };

static int g_ctor_count;
struct Registry {
    std::map<std::string, int> items;
    Registry() { g_ctor_count++; items["boot"] = 1; }
};
static Registry &registry() { static Registry r; return r; }   // __cxa_guard_*

int main() {
    @autoreleasepool {
        std::string s = "Hello";
        s += ", C++ on iOS ABI";
        s.append("!");
        std::string longer = s + " — this string is long enough to leave the small-string buffer";
        printf("string: %s (size %zu, long size %zu)\n", s.c_str(), s.size(), longer.size());
        printf("find: %zu substr: %s\n", s.find("C++"), s.substr(7, 3).c_str());
        printf("to_string: %s %s\n", std::to_string(42u).c_str(), std::to_string(1234567890123UL).c_str());

        std::vector<std::unique_ptr<Shape>> shapes;
        shapes.push_back(std::make_unique<Square>(3));
        shapes.push_back(std::make_unique<Rect>(2, 5));
        std::sort(shapes.begin(), shapes.end(), [](auto &a, auto &b) { return a->area() < b->area(); });
        for (auto &sh : shapes) printf("shape %s area %.1f\n", sh->name(), sh->area());
        Shape *base = shapes[0].get();
        printf("dynamic_cast<Rect*>: %s\n", dynamic_cast<Rect *>(base) ? "yes" : "no");

        auto sp = std::make_shared<std::string>("shared");
        std::weak_ptr<std::string> wp = sp;
        {
            auto sp2 = sp;
            printf("shared_ptr use_count=%ld lock=%s\n", sp.use_count(), wp.lock()->c_str());
        }
        sp.reset();
        printf("after reset expired=%d\n", wp.expired());

        std::unordered_map<std::string, int> counts;
        for (const char *w : { "a", "b", "a", "c", "a" }) counts[w]++;
        printf("unordered_map a=%d b=%d size=%zu\n", counts["a"], counts["b"], counts.size());

        registry().items["x"] = 2;
        registry();
        printf("function-local static constructed %d time(s), items=%zu\n", g_ctor_count, registry().items.size());

        std::mutex m;
        int total = 0;
        std::vector<std::thread> threads;
        for (int t = 0; t < 4; t++)
            threads.emplace_back([&, t] { for (int i = 0; i < 1000; i++) { std::lock_guard<std::mutex> g(m); total += t + 1; } });
        for (auto &t : threads) t.join();
        printf("threads total=%d (expected 10000)\n", total);

        std::function<int(int)> twice = [](int x) { return 2 * x; };
        NSString *ns = [NSString stringWithUTF8String:longer.c_str()];
        NSLog(@"bridged to NSString: %lu chars, twice(21)=%d", (unsigned long)ns.length, twice(21));
    }
    return 0;
}
