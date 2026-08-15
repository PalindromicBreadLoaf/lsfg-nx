// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

// Trampolines for the deferred installer.

.section .text.lsfg_deferred_thunk, "ax", %progbits
.align 2

.macro lsfg_deferred_thunk index
.global lsfg_plugin_deferred_thunk_\index
.type lsfg_plugin_deferred_thunk_\index, %function
lsfg_plugin_deferred_thunk_\index:
    mov w9, #\index
    b lsfg_plugin_deferred_common
.size lsfg_plugin_deferred_thunk_\index, . - lsfg_plugin_deferred_thunk_\index
.endm

lsfg_deferred_thunk 0

.type lsfg_plugin_deferred_common, %function
lsfg_plugin_deferred_common:
    stp x29, x30, [sp, #-96]!
    mov x29, sp
    stp x0, x1, [sp, #16]
    stp x2, x3, [sp, #32]
    stp x4, x5, [sp, #48]
    stp x6, x7, [sp, #64]
    str x8, [sp, #80]

    // Runs the installation once and returns the displaced function.
    mov w0, w9
    bl lsfg_plugin_deferred_enter
    mov x16, x0

    ldp x0, x1, [sp, #16]
    ldp x2, x3, [sp, #32]
    ldp x4, x5, [sp, #48]
    ldp x6, x7, [sp, #64]
    ldr x8, [sp, #80]
    ldp x29, x30, [sp], #96

    br x16
.size lsfg_plugin_deferred_common, . - lsfg_plugin_deferred_common
