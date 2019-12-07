
.cpu cortex-m4
.thumb

.thumb_func
.global _main
_main:
    adr  r1, here
    ldr  r0, =$(CONFIG_RELOC_ADDR)
    add  r1, r1, r0
    
    movs r2, #0x1
    orr  r1, r1, r2
    mov  lr, r1
    b relocate_code

here:
    // Zero-initialize .bss
    ldr  r0, =__bss_start
    ldr  r1, =__bss_end
    mov  r2, #0
bss_zero_loop:
    str  r2, [r0]
    add  r0, #4
    cmp  r0, r1
    bne  bss_zero_loop

    bl   main

