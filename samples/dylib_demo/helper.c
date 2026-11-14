/* libhelper.dylib: a plain C dylib that DemoKit links through @rpath */
#include <stdio.h>
int helper_value = 40;
int helper_twice(int x) { return 2 * x; }
__attribute__((constructor)) static void helper_init(void) { printf("helper: initializer\n"); }
