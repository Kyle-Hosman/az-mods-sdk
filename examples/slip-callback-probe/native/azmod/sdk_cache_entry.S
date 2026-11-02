/* SPDX-License-Identifier: GPL-3.0-only */
/* Register-only native entries; see azcacheentry.h's explicit limits. */
#include "../common/azcacheentry.h"
.text
.p2align 2
.macro CACHE_ENTRY number
.global azsdk_cache_entry_\number
.hidden azsdk_cache_entry_\number
.type azsdk_cache_entry_\number, %function
azsdk_cache_entry_\number:
.cfi_startproc
    sub sp, sp, #AZCE_STACK
.cfi_def_cfa_offset AZCE_STACK
    stp x29, x30, [sp, #AZCE_SAVED_FP]
.cfi_offset 29, -16
.cfi_offset 30, -8
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    stp x4, x5, [sp, #32]
    stp x6, x7, [sp, #48]
    str x8, [sp, #AZCE_X8]
    str x30, [sp, #AZCE_CALLER_LINK]
    str x21, [sp, #AZCE_CALLER_X21]
    str xzr, [sp, #(AZCE_CALLER_X21 + 8)]
    stp q0, q1, [sp, #AZCE_VARGS]
    stp q2, q3, [sp, #(AZCE_VARGS + 32)]
    stp q4, q5, [sp, #(AZCE_VARGS + 64)]
    stp q6, q7, [sp, #(AZCE_VARGS + 96)]
    adrp x10, azsdk_cache_entry_originals
    add x10, x10, :lo12:azsdk_cache_entry_originals
    ldr x11, [x10, #(\number * 8)]
    str x11, [sp, #AZCE_ORIGINAL]
    mov w9, #\number
    str w9, [sp, #AZCE_KIND]
    str wzr, [sp, #(AZCE_KIND + 4)]
    mov x29, sp
.cfi_def_cfa_register 29
    mov x0, sp
    bl azsdk_cache_dispatch_call
    ldp x0, x1, [sp, #AZCE_RESULTS]
    ldp q0, q1, [sp, #AZCE_VRESULTS]
    ldp q2, q3, [sp, #(AZCE_VRESULTS + 32)]
    ldp x29, x30, [sp, #AZCE_SAVED_FP]
    add sp, sp, #AZCE_STACK
.cfi_def_cfa sp, 0
.cfi_restore 29
.cfi_restore 30
    ret
.cfi_endproc
.size azsdk_cache_entry_\number, .-azsdk_cache_entry_\number
.endm
CACHE_ENTRY 0
CACHE_ENTRY 1
CACHE_ENTRY 2
CACHE_ENTRY 3
CACHE_ENTRY 4
CACHE_ENTRY 5
CACHE_ENTRY 6
CACHE_ENTRY 7
CACHE_ENTRY 8
CACHE_ENTRY 9
CACHE_ENTRY 10
CACHE_ENTRY 11
CACHE_ENTRY 12
CACHE_ENTRY 13
CACHE_ENTRY 14
CACHE_ENTRY 15
CACHE_ENTRY 16
CACHE_ENTRY 17
CACHE_ENTRY 18
CACHE_ENTRY 19
CACHE_ENTRY 20
CACHE_ENTRY 21
CACHE_ENTRY 22

.p2align 2
.global azsdk_cache_invoke_original
.hidden azsdk_cache_invoke_original
.type azsdk_cache_invoke_original, %function
azsdk_cache_invoke_original:
.cfi_startproc
    stp x29, x30, [sp, #-32]!
.cfi_def_cfa_offset 32
.cfi_offset 29, -32
.cfi_offset 30, -24
    str x19, [sp, #16]
.cfi_offset 19, -16
    mov x29, sp
.cfi_def_cfa_register 29
    mov x19, x0
    ldr x16, [x19, #AZCE_ORIGINAL]
    ldp x0, x1, [x19, #0]
    ldp x2, x3, [x19, #16]
    ldp x4, x5, [x19, #32]
    ldp x6, x7, [x19, #48]
    ldr x8, [x19, #AZCE_X8]
    ldp q0, q1, [x19, #AZCE_VARGS]
    ldp q2, q3, [x19, #(AZCE_VARGS + 32)]
    ldp q4, q5, [x19, #(AZCE_VARGS + 64)]
    ldp q6, q7, [x19, #(AZCE_VARGS + 96)]
    blr x16
    stp x0, x1, [x19, #AZCE_RESULTS]
    stp q0, q1, [x19, #AZCE_VRESULTS]
    stp q2, q3, [x19, #(AZCE_VRESULTS + 32)]
    ldr x19, [sp, #16]
    ldp x29, x30, [sp], #32
.cfi_def_cfa sp, 0
.cfi_restore 19
.cfi_restore 29
.cfi_restore 30
    ret
.cfi_endproc
.size azsdk_cache_invoke_original, .-azsdk_cache_invoke_original
.section .note.GNU-stack,"",%progbits
