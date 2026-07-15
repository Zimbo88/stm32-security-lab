#include <stdint.h>

#include "delay.h"
#include "uart.h"

#define REG32(a) (*(volatile uint32_t *)(a))
#define RCC_AHB1ENR REG32(0x40023830UL)
#define GPIOH_MODER REG32(0x40021C00UL)
#define GPIOH_BSRR  REG32(0x40021C18UL)
#define SCB_VTOR    REG32(0xE000ED08UL)
#define PH12        (1UL << 12)

static void heartbeat_init(void)
{
    RCC_AHB1ENR |= (1UL << 7);
    (void)RCC_AHB1ENR;

    GPIOH_MODER &= ~(3UL << 24U);
    GPIOH_MODER |=  (1UL << 24U);
    GPIOH_BSRR = PH12 << 16U;
}

int main(void)
{
    heartbeat_init();
    uart_init_115200_hsi16();

    uart_puts("\n========================================\n");
    uart_puts("EXP014 CRC-VERIFIED APPLICATION\n");
    uart_puts("Linked vector at 0x08008200\n");
    uart_puts("Runtime VTOR     = ");
    uart_put_hex32(SCB_VTOR);
    uart_puts("\n========================================\n");

    for (;;) {
        GPIOH_BSRR = PH12;
        uart_puts("verified application heartbeat 1\n");
        delay_cycles(4000000U);

        GPIOH_BSRR = PH12 << 16U;
        uart_puts("verified application heartbeat 0\n");
        delay_cycles(4000000U);
    }
}
