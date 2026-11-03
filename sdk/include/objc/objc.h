/* iosrt mini-SDK: Objective-C basics (arm64: BOOL is bool). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct objc_class *Class;
typedef struct objc_object { Class isa; } *id;
typedef struct objc_selector *SEL;
typedef id (*IMP)(id, SEL, ...);
typedef bool BOOL;
#define YES ((BOOL)1)
#define NO ((BOOL)0)
#ifndef nil
#define nil ((void *)0)
#endif
#ifndef Nil
#define Nil ((void *)0)
#endif
