.syntax unified
.cpu cortex-m4
.thumb

.global Reset_Handler
.global Default_Handler

.extern main
.extern _estack
.extern _etext
.extern _siramfunc
.extern _sramfunc
.extern _eramfunc
.extern _sdata
.extern _edata
.extern _sbss
.extern _ebss

.section .isr_vector, "a", %progbits
vector_table:
    .word _estack
    .word Reset_Handler
    .rept 14
    .word Default_Handler
    .endr

.section .text.Reset_Handler, "ax", %progbits
.thumb_func
Reset_Handler:
    ldr r0, =_siramfunc
    ldr r1, =_sramfunc
    ldr r2, =_eramfunc
1:
    cmp r1, r2
    bcs 2f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 1b

2:
    ldr r0, =_etext
    ldr r1, =_sdata
    ldr r2, =_edata
3:
    cmp r1, r2
    bcs 4f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 3b

4:
    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0
5:
    cmp r0, r1
    bcs 6f
    str r2, [r0], #4
    b 5b

6:
    bl main
7:
    b 7b

.section .text.Default_Handler, "ax", %progbits
.thumb_func
Default_Handler:
    b Default_Handler
