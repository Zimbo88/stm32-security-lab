.syntax unified
.cpu cortex-m4
.thumb

.global Reset_Handler
.global Default_Handler
.global vector_table

.extern _siramfunc
.extern _sramfunc
.extern _eramfunc

.weak NMI_Handler
.thumb_set NMI_Handler, Default_Handler
.weak HardFault_Handler
.thumb_set HardFault_Handler, Default_Handler
.weak MemManage_Handler
.thumb_set MemManage_Handler, Default_Handler
.weak BusFault_Handler
.thumb_set BusFault_Handler, Default_Handler
.weak UsageFault_Handler
.thumb_set UsageFault_Handler, Default_Handler
.weak SVCall_Handler
.thumb_set SVCall_Handler, Default_Handler
.weak DebugMon_Handler
.thumb_set DebugMon_Handler, Default_Handler
.weak PendSV_Handler
.thumb_set PendSV_Handler, Default_Handler
.weak SysTick_Handler
.thumb_set SysTick_Handler, Default_Handler

.section .isr_vector,"a",%progbits
.type vector_table,%object
vector_table:
.word _estack
.word Reset_Handler
.word NMI_Handler
.word HardFault_Handler
.word MemManage_Handler
.word BusFault_Handler
.word UsageFault_Handler
.word 0
.word 0
.word 0
.word 0
.word SVCall_Handler
.word DebugMon_Handler
.word 0
.word PendSV_Handler
.word SysTick_Handler
.size vector_table, . - vector_table

.text
.thumb_func
.type Reset_Handler,%function
.global Reset_Handler
Reset_Handler:
    ldr r0, =_siramfunc
    ldr r1, =_sramfunc
    ldr r2, =_eramfunc
1:  cmp r1, r2
    bcs 2f
    ldr r3, [r0], #4
    str r3, [r1], #4
    b 1b
2:
    ldr r0, =_sdata
    ldr r1, =_edata
    ldr r2, =_etext
3:  cmp r0, r1
    bcs 4f
    ldr r3, [r2], #4
    str r3, [r0], #4
    b 3b
4:  ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0
5:  cmp r0, r1
    bcs 6f
    str r2, [r0], #4
    b 5b
6:  bl main
7:  b 7b

.thumb_func
.type Default_Handler,%function
Default_Handler:
    b Default_Handler
