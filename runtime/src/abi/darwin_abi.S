/*
 * AArch64 glue between Darwin-PCS guest code and AAPCS64 runtime code.
 *
 * 1. Variadic trampolines: Darwin passes every variadic argument on the stack
 *    in 8-byte slots. On entry `sp` points at the first one, so the trampoline
 *    passes `sp` to the C implementation as `const uint64_t *args` in the first
 *    free argument register and tail-calls it.
 * 2. Trap entry for unimplemented imports (x16 = symbol name).
 * 3. ___chkstk_darwin stack probe (no-op).
 * 4. objc_msgSend / objc_msgSendSuper / objc_msgSendSuper2.
 */
    .text

.macro DVAR name, impl, reg
    .globl \name
    .type \name, %function
    .p2align 2
\name:
    mov \reg, sp
    b \impl
    .size \name, . - \name
.endm

/* libc */
DVAR iosrt_tramp_printf,    iosrt_darwin_printf,    x1
DVAR iosrt_tramp_fprintf,   iosrt_darwin_fprintf,   x2
DVAR iosrt_tramp_sprintf,   iosrt_darwin_sprintf,   x2
DVAR iosrt_tramp_snprintf,  iosrt_darwin_snprintf,  x3
DVAR iosrt_tramp_asprintf,  iosrt_darwin_asprintf,  x2
DVAR iosrt_tramp_dprintf,   iosrt_darwin_dprintf,   x2
DVAR iosrt_tramp_sprintf_chk,  iosrt_darwin_sprintf_chk,  x4
DVAR iosrt_tramp_snprintf_chk, iosrt_darwin_snprintf_chk, x5
DVAR iosrt_tramp_open,      iosrt_darwin_open,      x2
DVAR iosrt_tramp_openat,    iosrt_darwin_openat,    x3
DVAR iosrt_tramp_fcntl,     iosrt_darwin_fcntl,     x2
DVAR iosrt_tramp_ioctl,     iosrt_darwin_ioctl,     x2
DVAR iosrt_tramp_sscanf,    iosrt_darwin_sscanf,    x2
/* Foundation / CoreFoundation C functions */
DVAR iosrt_tramp_NSLog,     iosrt_darwin_NSLog,     x1
DVAR iosrt_tramp_CFStringCreateWithFormat, iosrt_darwin_CFStringCreateWithFormat, x3
/* Objective-C methods: x0=self x1=_cmd x2=first named arg */
DVAR iosrt_imp_stringWithFormat,           rt_NSString_stringWithFormat,           x3
DVAR iosrt_imp_initWithFormat,             rt_NSString_initWithFormat,             x3
DVAR iosrt_imp_stringByAppendingFormat,    rt_NSString_stringByAppendingFormat,    x3
DVAR iosrt_imp_appendFormat,               rt_NSMutableString_appendFormat,        x3
DVAR iosrt_imp_arrayWithObjects,           rt_NSArray_arrayWithObjects,            x3
DVAR iosrt_imp_initWithObjects,            rt_NSArray_initWithObjects,             x3
DVAR iosrt_imp_dictionaryWithObjectsAndKeys, rt_NSDictionary_dictionaryWithObjectsAndKeys, x3
DVAR iosrt_imp_initWithObjectsAndKeys,     rt_NSDictionary_initWithObjectsAndKeys, x3
DVAR iosrt_imp_exceptionRaiseFormat,       rt_NSException_raiseFormat,             x4
DVAR iosrt_imp_predicateWithFormat,        rt_NSPredicate_predicateWithFormat,     x3
DVAR iosrt_imp_setWithObjects,             rt_NSSet_setWithObjects,                x3
DVAR iosrt_imp_setInitWithObjects,         rt_NSSet_initWithObjects,               x3
DVAR iosrt_imp_handleFailureInMethod,      rt_NSAssertionHandler_handleFailure,    x7
DVAR iosrt_imp_handleFailureInFunction,    rt_NSAssertionHandler_handleFailureFn,  x6
DVAR iosrt_tramp_NSDictionaryOfVariableBindings, rt_NSDictionaryOfVariableBindings, x2
DVAR iosrt_imp_UIAlertView_init,           rt_UIAlertView_init,                    x7
DVAR iosrt_imp_UIActionSheet_init,         rt_UIActionSheet_init,                  x7

/* ---------------------------------------------------------------- traps */
    .globl iosrt_trap_entry
    .type iosrt_trap_entry, %function
    .p2align 2
iosrt_trap_entry:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    mov x0, x16
    bl rt_missing_called          /* strict mode: does not return */
    ldp x29, x30, [sp], #16
    /* lenient mode: behave like a function returning 0 / nil / 0.0 */
    mov x0, #0
    mov x1, #0
    movi d0, #0
    movi d1, #0
    movi d2, #0
    movi d3, #0
    ret
    .size iosrt_trap_entry, . - iosrt_trap_entry

/* IMP used for unrecognized selectors in lenient mode: returns nil / 0 */
    .globl iosrt_nil_imp
    .type iosrt_nil_imp, %function
    .p2align 2
iosrt_nil_imp:
    mov x0, #0
    mov x1, #0
    movi d0, #0
    movi d1, #0
    movi d2, #0
    movi d3, #0
    ret
    .size iosrt_nil_imp, . - iosrt_nil_imp

/* Darwin stack probe: x15 = size/16; must preserve all registers. */
    .globl iosrt_chkstk_darwin
    .type iosrt_chkstk_darwin, %function
    .p2align 2
iosrt_chkstk_darwin:
    ret
    .size iosrt_chkstk_darwin, . - iosrt_chkstk_darwin

/* ------------------------------------------------------------ messaging */
/*
 * Save all argument registers (x0-x8, q0-q7) + fp/lr, ask the C runtime for
 * the IMP, restore and tail-branch to it so the callee sees the original
 * arguments and stack (including Darwin variadic stack slots).
 */
.macro SAVE_ARGS
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    sub sp, sp, #(10*8 + 8*16)
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    str x8,     [sp, #64]
    stp q0, q1, [sp, #80]
    stp q2, q3, [sp, #112]
    stp q4, q5, [sp, #144]
    stp q6, q7, [sp, #176]
.endm
.macro RESTORE_ARGS
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    ldp x4, x5, [sp, #32]
    ldp x6, x7, [sp, #48]
    ldr x8,     [sp, #64]
    ldp q0, q1, [sp, #80]
    ldp q2, q3, [sp, #112]
    ldp q4, q5, [sp, #144]
    ldp q6, q7, [sp, #176]
    mov sp, x29
    ldp x29, x30, [sp], #16
.endm

    .globl objc_msgSend
    .type objc_msgSend, %function
    .p2align 2
objc_msgSend:
    cbz x0, .Lnil
    SAVE_ARGS
    mov x2, sp                     /* &saved x0: forwarding may replace the receiver */
    bl rt_lookup_for_send_slot     /* (x0=self, x1=sel, x2=slot) -> x0 = IMP */
    mov x17, x0
    RESTORE_ARGS
    br x17
.Lnil:
    mov x0, #0
    mov x1, #0
    movi d0, #0
    movi d1, #0
    movi d2, #0
    movi d3, #0
    ret
    .size objc_msgSend, . - objc_msgSend

/* struct objc_super { id receiver; Class cls; }  Super2: start at cls->superclass */
    .globl objc_msgSendSuper2
    .type objc_msgSendSuper2, %function
    .p2align 2
objc_msgSendSuper2:
    SAVE_ARGS
    bl rt_lookup_super2            /* (x0=super*, x1=sel) -> IMP */
    mov x17, x0
    RESTORE_ARGS
    ldr x0, [x0]                   /* receiver */
    br x17
    .size objc_msgSendSuper2, . - objc_msgSendSuper2

    .globl objc_msgSendSuper
    .type objc_msgSendSuper, %function
    .p2align 2
objc_msgSendSuper:
    SAVE_ARGS
    bl rt_lookup_super             /* start at cls itself */
    mov x17, x0
    RESTORE_ARGS
    ldr x0, [x0]
    br x17
    .size objc_msgSendSuper, . - objc_msgSendSuper

/* Darwin libm returns {sin, cos} in d0/d1 */
    .globl iosrt_sincos_stret
    .type iosrt_sincos_stret, %function
    .p2align 2
iosrt_sincos_stret:
    stp x29, x30, [sp, #-32]!
    mov x29, sp
    add x0, sp, #16
    add x1, sp, #24
    bl sincos
    ldr d0, [sp, #16]
    ldr d1, [sp, #24]
    ldp x29, x30, [sp], #32
    ret
    .size iosrt_sincos_stret, . - iosrt_sincos_stret

    .globl iosrt_sincosf_stret
    .type iosrt_sincosf_stret, %function
    .p2align 2
iosrt_sincosf_stret:
    stp x29, x30, [sp, #-32]!
    mov x29, sp
    add x0, sp, #16
    add x1, sp, #20
    bl sincosf
    ldr s0, [sp, #16]
    ldr s1, [sp, #20]
    ldp x29, x30, [sp], #32
    ret
    .size iosrt_sincosf_stret, . - iosrt_sincosf_stret


/* ------------------------------------------------- generic call / forward */
/* struct rt_callframe { u64 x[9]; double d[8]; u64 *stack; u64 nstack; u64 rx[2]; double rd[4]; }
 * offsets:          x:0       d:72         stack:136  nstack:144  rx:152    rd:168   (size 200) */
    .globl iosrt_call
    .type iosrt_call, %function
    .p2align 2
iosrt_call:                         /* x0 = fn, x1 = frame */
    stp x29, x30, [sp, #-32]!
    mov x29, sp
    stp x19, x20, [sp, #16]
    mov x19, x1
    mov x20, x0
    ldr x9, [x19, #144]
    lsl x10, x9, #3
    add x10, x10, #15
    and x10, x10, #~15
    sub sp, sp, x10
    ldr x11, [x19, #136]
    mov x12, #0
1:  cmp x12, x9
    b.ge 2f
    ldr x13, [x11, x12, lsl #3]
    str x13, [sp, x12, lsl #3]
    add x12, x12, #1
    b 1b
2:  ldp d0, d1, [x19, #72]
    ldp d2, d3, [x19, #88]
    ldp d4, d5, [x19, #104]
    ldp d6, d7, [x19, #120]
    ldp x0, x1, [x19, #0]
    ldp x2, x3, [x19, #16]
    ldp x4, x5, [x19, #32]
    ldp x6, x7, [x19, #48]
    ldr x8, [x19, #64]
    blr x20
    stp x0, x1, [x19, #152]
    stp d0, d1, [x19, #168]
    stp d2, d3, [x19, #184]
    mov sp, x29
    ldp x19, x20, [sp, #16]
    ldp x29, x30, [sp], #32
    ret
    .size iosrt_call, . - iosrt_call

    .globl iosrt_msgForward
    .type iosrt_msgForward, %function
    .p2align 2
iosrt_msgForward:
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    sub sp, sp, #208
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    str x8, [sp, #64]
    stp d0, d1, [sp, #72]
    stp d2, d3, [sp, #88]
    stp d4, d5, [sp, #104]
    stp d6, d7, [sp, #120]
    add x9, x29, #16
    str x9, [sp, #136]
    str xzr, [sp, #144]
    mov x0, sp
    bl rt_forward_invocation
    ldp x0, x1, [sp, #152]
    ldp d0, d1, [sp, #168]
    ldp d2, d3, [sp, #184]
    mov sp, x29
    ldp x29, x30, [sp], #16
    ret
    .size iosrt_msgForward, . - iosrt_msgForward

    .section .note.GNU-stack,"",%progbits

    .text
/*
 * setjmp/longjmp family for guest code. They must run without a C frame of
 * their own, so they live here. The guest's jmp_buf (Darwin: 48 ints, 192
 * bytes; sigjmp_buf one more) has room for x19-x30, sp and d8-d15 (168
 * bytes). The signal mask is not saved: the runtime does not change it.
 */
    .globl iosrt_setjmp
    .type iosrt_setjmp, %function
    .p2align 2
iosrt_setjmp:
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    stp x29, x30, [x0, #80]
    mov x9, sp
    str x9, [x0, #96]
    stp d8, d9, [x0, #104]
    stp d10, d11, [x0, #120]
    stp d12, d13, [x0, #136]
    stp d14, d15, [x0, #152]
    mov w0, #0
    ret
    .size iosrt_setjmp, . - iosrt_setjmp

    .globl iosrt_longjmp
    .type iosrt_longjmp, %function
    .p2align 2
iosrt_longjmp:
    ldp x19, x20, [x0, #0]
    ldp x21, x22, [x0, #16]
    ldp x23, x24, [x0, #32]
    ldp x25, x26, [x0, #48]
    ldp x27, x28, [x0, #64]
    ldp x29, x30, [x0, #80]
    ldr x9, [x0, #96]
    mov sp, x9
    ldp d8, d9, [x0, #104]
    ldp d10, d11, [x0, #120]
    ldp d12, d13, [x0, #136]
    ldp d14, d15, [x0, #152]
    cmp w1, #0
    csinc w0, w1, wzr, ne      /* longjmp(env, 0) returns 1 */
    ret
    .size iosrt_longjmp, . - iosrt_longjmp
