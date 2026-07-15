#ifndef REGISTERS_H
#define REGISTERS_H

#include <stdint.h>

#define REG32(address) (*(volatile uint32_t *)(address))

/* Reset and Clock Control */
#define RCC_BASE        0x40023800UL
#define RCC_AHB1ENR     REG32(RCC_BASE + 0x30UL)
#define RCC_APB2ENR     REG32(RCC_BASE + 0x44UL)

/* GPIO */
#define GPIOA_BASE      0x40020000UL
#define GPIOH_BASE      0x40021C00UL

#define GPIO_MODER(base)   REG32((base) + 0x00UL)
#define GPIO_OTYPER(base)  REG32((base) + 0x04UL)
#define GPIO_OSPEEDR(base) REG32((base) + 0x08UL)
#define GPIO_PUPDR(base)   REG32((base) + 0x0CUL)
#define GPIO_IDR(base)     REG32((base) + 0x10UL)
#define GPIO_ODR(base)     REG32((base) + 0x14UL)
#define GPIO_BSRR(base)    REG32((base) + 0x18UL)
#define GPIO_AFRH(base)    REG32((base) + 0x24UL)

/* USART1 */
#define USART1_BASE     0x40011000UL
#define USART1_SR       REG32(USART1_BASE + 0x00UL)
#define USART1_DR       REG32(USART1_BASE + 0x04UL)
#define USART1_BRR      REG32(USART1_BASE + 0x08UL)
#define USART1_CR1      REG32(USART1_BASE + 0x0CUL)
#define USART1_CR2      REG32(USART1_BASE + 0x10UL)
#define USART1_CR3      REG32(USART1_BASE + 0x14UL)

#endif
