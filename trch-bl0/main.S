
.cpu cortex-m4
.thumb

.thumb_func
.global main
main:
    adr  r1, here
    ldr  r0, =$(CONFIG_RELOC_ADDR)
    add  r1, r1, r0
    
    movs r2, #0x1
    orr  r1, r1, r2
    mov  lr, r1
    b relocate_code

here:
    // Zero-initialize .bss
    ldr  r0, =__bss_start__
    ldr  r1, =__bss_end__
    mov  r2, #0
zero_relocated_bss_loop:
    str  r2, [r0]
    add  r0, #4
    cmp  r0, r1
    bne  zero_relocated_bss_loop

    bl   main_relocated

