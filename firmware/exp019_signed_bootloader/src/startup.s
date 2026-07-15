.syntax unified
.cpu cortex-m4
.thumb

.global Reset_Handler
.global Default_Handler

.extern main
.extern _estack
.extern _etext
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
    ldr r0, =_etext
    ldr r1, =_sdata
    ldr r2, =_edata
1:
    cmp r1, r2
    bcs 2f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 1b

2:
    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0
3:
    cmp r0, r1
    bcs 4f
    str r2, [r0], #4
    b 3b

4:
    bl main
5:
    b 5b

.section .text.Default_Handler, "ax", %progbits
.thumb_func
Default_Handler:
    b Default_Handler
