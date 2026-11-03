/* iosrt sample: plain C on the Darwin ABI — variadics, globals with rebases,
 * function pointers, callbacks from runtime code into guest code, heap. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct shape { const char *name; double (*area)(double); };
static double square(double s) { return s * s; }
static double circle(double r) { return M_PI * r * r; }
static struct shape shapes[] = { { "square", square }, { "circle", circle } };   /* pointers need rebasing */
static const char *greeting = "hello from an iOS arm64 Mach-O";

static int cmp_int(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }
static long fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }

int main(int argc, char **argv) {
    printf("%s\n", greeting);
    printf("argc=%d argv0-is-set=%s\n", argc, argv[0] ? "yes" : "no");
    printf("ints: %d %u %ld %lld %x %05d|%-4d|\n", -42, 42u, -1234567890123L, 9000000000LL, 255, 7, 3);
    printf("floats: %.3f %g %e\n", 3.14159265, 0.5, 12345.678);
    printf("mixed: %s=%d, %c%c, %.2f%%\n", "answer", 42, 'o', 'k', 99.5);
    for (int i = 0; i < 2; i++) printf("area(%s, 2) = %.4f\n", shapes[i].name, shapes[i].area(2.0));
    int v[] = { 5, 3, 9, 1, 7, 2 };
    qsort(v, 6, sizeof v[0], cmp_int);              /* host qsort calls back into guest code */
    printf("sorted:"); for (int i = 0; i < 6; i++) printf(" %d", v[i]); printf("\n");
    char *buf = malloc(64);
    snprintf(buf, 64, "fib(20)=%ld sqrt(2)=%.6f", fib(20), sqrt(2.0));
    printf("%s (len %zu)\n", buf, strlen(buf));
    free(buf);
    fprintf(stderr, "stderr works\n");
    return 7;
}
