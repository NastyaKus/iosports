/* calls an API the runtime does not implement -> must trap with the symbol name */
#include <stdio.h>
void CFNetworkExecuteProxyAutoConfigurationURL(void);
int main(void) {
    printf("before missing call\n");
    CFNetworkExecuteProxyAutoConfigurationURL();
    printf("unreachable\n");
    return 0;
}
