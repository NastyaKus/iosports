#include "shared.h"
extern "C" int main_bump(void) { return ++shared_counter(); }
