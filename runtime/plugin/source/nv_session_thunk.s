// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

.section .text.lsfg_nv_session_trampoline, "ax", %progbits
.balign 16

.global lsfg_plugin_nv_connect_trampoline
.type lsfg_plugin_nv_connect_trampoline, %function
lsfg_plugin_nv_connect_trampoline:
.global lsfg_plugin_nv_connect_trampoline_instruction
lsfg_plugin_nv_connect_trampoline_instruction:
    nop
    nop
    nop
    nop
    ldr x16, lsfg_plugin_nv_connect_trampoline_return
    br x16
    nop
    nop
.global lsfg_plugin_nv_connect_trampoline_return
.type lsfg_plugin_nv_connect_trampoline_return, %object
lsfg_plugin_nv_connect_trampoline_return:
    .xword 0
.size lsfg_plugin_nv_connect_trampoline, . - lsfg_plugin_nv_connect_trampoline
