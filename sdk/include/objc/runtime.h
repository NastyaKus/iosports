#pragma once
#include <objc/objc.h>
typedef struct objc_method *Method;
typedef struct objc_ivar *Ivar;
typedef struct objc_property *objc_property_t;
Class objc_getClass(const char *);
Class object_getClass(id);
const char *class_getName(Class);
Class class_getSuperclass(Class);
BOOL class_respondsToSelector(Class, SEL);
Method class_getInstanceMethod(Class, SEL);
Method class_getClassMethod(Class, SEL);
IMP method_getImplementation(Method);
IMP method_setImplementation(Method, IMP);
void method_exchangeImplementations(Method, Method);
BOOL class_addMethod(Class, SEL, IMP, const char *);
SEL sel_registerName(const char *);
const char *sel_getName(SEL);
Class objc_allocateClassPair(Class, const char *, size_t);
void objc_registerClassPair(Class);
void objc_setAssociatedObject(id, const void *, id, uintptr_t);
id objc_getAssociatedObject(id, const void *);
Method *class_copyMethodList(Class, unsigned *);
Ivar *class_copyIvarList(Class, unsigned *);
const char *ivar_getName(Ivar);
SEL method_getName(Method);
#define OBJC_ASSOCIATION_RETAIN_NONATOMIC 1
#define OBJC_ASSOCIATION_COPY_NONATOMIC 3
