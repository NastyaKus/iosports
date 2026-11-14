#include "shared.h"
extern "C" int demokit_bump(void) { return ++shared_counter(); }
