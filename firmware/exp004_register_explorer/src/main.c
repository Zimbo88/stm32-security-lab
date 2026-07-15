#include <stdint.h>

#include "delay.h"
#include "gpio.h"
#include "registers.h"
#include "uart.h"

static void print_register(const char *name, uint32_t value)
{
    uart_puts(name);
    uart_puts(" = ");
    uart_put_hex32(value);
    uart_puts("\n");
}

int main(void)
{
    heartbeat_init();
    uart_init();

    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("STM32F429 SECURITY LAB\n");
    uart_puts("EXP004 REGISTER EXPLORER\n");
    uart_puts("========================================\n");

    for (;;) {
        uart_puts("\n--- REGISTER SNAPSHOT ---\n");

        print_register("RCC_AHB1ENR ", RCC_AHB1ENR);
        print_register("RCC_APB2ENR ", RCC_APB2ENR);

        print_register("GPIOA_MODER ", GPIO_MODER(GPIOA_BASE));
        print_register("GPIOA_AFRH  ", GPIO_AFRH(GPIOA_BASE));

        print_register("GPIOH_MODER ", GPIO_MODER(GPIOH_BASE));
        print_register("GPIOH_IDR   ", GPIO_IDR(GPIOH_BASE));
        print_register("GPIOH_ODR   ", GPIO_ODR(GPIOH_BASE));

        print_register("USART1_SR   ", USART1_SR);
        print_register("USART1_BRR  ", USART1_BRR);
        print_register("USART1_CR1  ", USART1_CR1);

        heartbeat_toggle();

        uart_puts("PH12 toggled\n");

        delay_cycles(8000000U);
    }
}
