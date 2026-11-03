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

/* ---------------------------------------------------------------- traps */
    .globl iosrt_trap_entry
    .type iosrt_trap_entry, %function
    .p2align 2
iosrt_trap_entry:
    mov x0, x16
    b rt_missing_called
    .size iosrt_trap_entry, . - iosrt_trap_entry

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

    .section .note.GNU-stack,"",%progbits
