// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

// Entry contract for a SaltyNX external plugin. The loader calls the ELF's
// load address with `blr`, so this must sit at offset zero and return through
// x30 rather than exiting the process it was injected into.

.section .crt0, "ax", %progbits
.global _start
.align 2

_start:
    b entrypoint
    .word __nx_mod0 - _start

entrypoint:
    b 1f
    .ascii "LSFGNX"

.org _start+0x80; 1:
    // x0 = env context pointer (should be zero)
    // x1 = main thread handle
    // x30 = loader return address
    mov x25, x0
    mov x26, x1
    mov x27, x30
    mov x28, sp

    adr  x0, _start
    adr  x1, __nx_mod0
    bl   __nx_dynamic

    adrp x9, __stack_top
    str  x28, [x9, #:lo12:__stack_top]

    adrp x9, __loader_return
    str  x27, [x9, #:lo12:__loader_return]

    mov  x0, x25
    mov  x1, x26
    mov  x2, x27
    bl   __libnx_init

    bl   main

    // Return to the loader instead of routing through libnx's exit path.
    adrp x9, __stack_top
    ldr  x9, [x9, #:lo12:__stack_top]
    mov  sp, x9
    mov  x30, x27
    ret

// libnx's exit path routes here. Defining it also keeps libnx's own crt0 out
// of the link, which would otherwise collide with the entry above.
.global __libnx_exit
.type   __libnx_exit, %function
__libnx_exit:

.global __nx_exit
.type   __nx_exit, %function
__nx_exit:
    adrp x8, __stack_top
    ldr  x8, [x8, #:lo12:__stack_top]
    mov  sp, x8
    adrp x8, __loader_return
    ldr  x8, [x8, #:lo12:__loader_return]
    br   x8

.global __nx_mod0
__nx_mod0:
    .ascii "MOD0"
    .word  _DYNAMIC             - __nx_mod0
    .word  __bss_start__        - __nx_mod0
    .word  __bss_end__          - __nx_mod0
    .word  __eh_frame_hdr_start - __nx_mod0
    .word  __eh_frame_hdr_end   - __nx_mod0
    .word  0

    .ascii "LNY0"
    .word  __got_start__        - __nx_mod0
    .word  __got_end__          - __nx_mod0

    .ascii "LNY1"
    .word  __relro_start        - __nx_mod0
    .word  __data_start         - __nx_mod0

    .ascii "LNY2"
    .word  0x1
    .word  0x0

.section .bss.__stack_top, "aw", %nobits
.global __stack_top
.align 3

__stack_top:
    .space 8

.section .bss.__loader_return, "aw", %nobits
.global __loader_return
.align 3

__loader_return:
    .space 8
