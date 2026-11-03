/* calls an API the runtime does not implement -> must trap with the symbol name */
#include <stdio.h>
void CFNetworkCopySystemProxySettings(void);
int main(void) {
    printf("before missing call\n");
    CFNetworkCopySystemProxySettings();
    printf("unreachable\n");
    return 0;
}
