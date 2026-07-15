#include <stdint.h>

#include "fault.h"
#include "uart.h"

#define REG32(address) (*(volatile uint32_t *)(address))

#define SCB_CFSR   REG32(0xE000ED28UL)
#define SCB_HFSR   REG32(0xE000ED2CUL)
#define SCB_MMFAR  REG32(0xE000ED34UL)
#define SCB_BFAR   REG32(0xE000ED38UL)
#define SCB_SHCSR  REG32(0xE000ED24UL)

static void print_register(const char *name, uint32_t value)
{
    uart_puts(name);
    uart_puts(" = ");
    uart_put_hex32(value);
    uart_puts("\n");
}

__attribute__((naked))
void HardFault_Handler(void)
{
    __asm volatile (
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "movs r1, #1\n"
        "b fault_handler_c\n"
    );
}

__attribute__((naked))
void MemManage_Handler(void)
{
    __asm volatile (
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "movs r1, #2\n"
        "b fault_handler_c\n"
    );
}

void fault_handler_c(uint32_t *stack_frame, uint32_t exception_kind)
{
    uart_puts("\n");
    uart_puts("========================================\n");

    if (exception_kind == 2U) {
        uart_puts("MEMMANAGE FAULT CAPTURED\n");
    } else {
        uart_puts("HARDFAULT CAPTURED\n");
    }

    uart_puts("========================================\n");

    print_register("STACKED_R0   ", stack_frame[0]);
    print_register("STACKED_R1   ", stack_frame[1]);
    print_register("STACKED_R2   ", stack_frame[2]);
    print_register("STACKED_R3   ", stack_frame[3]);
    print_register("STACKED_R12  ", stack_frame[4]);
    print_register("STACKED_LR   ", stack_frame[5]);
    print_register("STACKED_PC   ", stack_frame[6]);
    print_register("STACKED_XPSR ", stack_frame[7]);

    uart_puts("\nFault status:\n");
    print_register("SCB_CFSR     ", SCB_CFSR);
    print_register("SCB_HFSR     ", SCB_HFSR);
    print_register("SCB_SHCSR    ", SCB_SHCSR);
    print_register("SCB_MMFAR    ", SCB_MMFAR);
    print_register("SCB_BFAR     ", SCB_BFAR);

    uart_puts("\nCPU halted intentionally.\n");

    for (;;) {
        __asm volatile ("nop");
    }
}
