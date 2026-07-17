.syntax unified
.cpu cortex-m4
.thumb

.global Reset_Handler
.extern main
.extern _estack
.extern _etext
.extern _sdata
.extern _edata
.extern _sbss
.extern _ebss

.section .isr_vector, "a", %progbits
.type vector_table, %object

vector_table:
    .word _estack
    .word Reset_Handler
    .word Default_Handler
    .word Default_Handler
    .word Default_Handler
    .word Default_Handler
    .word Default_Handler
    .word 0
    .word 0
    .word 0
    .word 0
    .word Default_Handler
    .word Default_Handler
    .word 0
    .word Default_Handler
    .word Default_Handler

.size vector_table, . - vector_table

.section .text.Reset_Handler, "ax", %progbits
.thumb_func
.type Reset_Handler, %function

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
    ldr r1, =_sbss
    ldr r2, =_ebss
    movs r3, #0

3:
    cmp r1, r2
    bcs 4f
    str r3, [r1], #4
    b 3b

4:
    bl main

5:
    b 5b

.size Reset_Handler, . - Reset_Handler

.section .text.Default_Handler, "ax", %progbits
.thumb_func
.type Default_Handler, %function

Default_Handler:
    b Default_Handler

.size Default_Handler, . - Default_Handler
