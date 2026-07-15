#include <stdint.h>

#include "delay.h"
#include "gpio.h"
#include "registers.h"
#include "uart.h"

#define FLASH_BASE_ADDR       0x08000000UL
#define SRAM1_BASE_ADDR       0x20000000UL
#define CCM_RAM_BASE_ADDR     0x10000000UL

static volatile uint32_t normal_sram_test;
static volatile uint32_t ccm_test
    __attribute__((section(".ccmram")));

static uint32_t read_msp(void)
{
    uint32_t value;

    __asm volatile (
        "mrs %0, msp"
        : "=r" (value)
    );

    return value;
}

static void print_hex(const char *name, uint32_t value)
{
    uart_puts(name);
    uart_puts(" = ");
    uart_put_hex32(value);
    uart_puts("\n");
}

static void memory_test(void)
{
    normal_sram_test = 0x12345678UL;
    ccm_test = 0xA5A55A5AUL;

    uart_puts("\nRAM write/read test:\n");

    print_hex("SRAM value      ", normal_sram_test);
    print_hex("SRAM address    ",
              (uint32_t)(uintptr_t)&normal_sram_test);

    print_hex("CCM value       ", ccm_test);
    print_hex("CCM address     ",
              (uint32_t)(uintptr_t)&ccm_test);

    if (normal_sram_test == 0x12345678UL) {
        uart_puts("SRAM test       = PASS\n");
    } else {
        uart_puts("SRAM test       = FAIL\n");
    }

    if (ccm_test == 0xA5A55A5AUL) {
        uart_puts("CCM test        = PASS\n");
    } else {
        uart_puts("CCM test        = FAIL\n");
    }
}

int main(void)
{
    heartbeat_init();
    uart_init();

    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("STM32F429 SECURITY LAB\n");
    uart_puts("EXP007 MEMORY MAP BASELINE\n");
    uart_puts("========================================\n");

    print_hex("FLASH base      ", FLASH_BASE_ADDR);
    print_hex("SRAM base       ", SRAM1_BASE_ADDR);
    print_hex("CCM RAM base    ", CCM_RAM_BASE_ADDR);

    print_hex("Vector word 0   ",
              *(volatile const uint32_t *)0x00000000UL);
    print_hex("Vector word 1   ",
              *(volatile const uint32_t *)0x00000004UL);

    print_hex("Flash word 0    ",
              *(volatile const uint32_t *)0x08000000UL);
    print_hex("Flash word 1    ",
              *(volatile const uint32_t *)0x08000004UL);

    print_hex("SCB_VTOR        ", SCB_VTOR);
    print_hex("Current MSP     ", read_msp());

    memory_test();

    uart_puts("\nEXP007 READY\n");

    for (;;) {
        heartbeat_toggle();
        uart_puts("heartbeat\n");
        delay_cycles(8000000U);
    }
}
