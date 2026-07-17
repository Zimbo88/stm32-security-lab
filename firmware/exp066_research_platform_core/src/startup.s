.syntax unified
.cpu cortex-m4
.thumb
.section .isr_vector,"a",%progbits
.word _estack
.word Reset_Handler
.word HardFault_Handler
.word MemManage_Handler
.word BusFault_Handler
.word UsageFault_Handler
.rept 10
.word Default_Handler
.endr
.text
.thumb_func
.global Reset_Handler
.type Reset_Handler,%function
Reset_Handler:
    ldr r0, =_sdata
    ldr r1, =_edata
    ldr r2, =_etext
1:  cmp r0, r1
    bcs 2f
    ldr r3, [r2], #4
    str r3, [r0], #4
    b 1b
2:  ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0
3:  cmp r0, r1
    bcs 4f
    str r2, [r0], #4
    b 3b
4:  bl main
5:  b 5b
.thumb_func
.global Default_Handler
Default_Handler: b Default_Handler
