#include "uart.h"
#define REG32(a) (*(volatile uint32_t *)(a))
#define RCC_AHB1ENR REG32(0x40023830UL)
#define RCC_APB2ENR REG32(0x40023844UL)
#define GPIOA_MODER REG32(0x40020000UL)
#define GPIOA_OTYPER REG32(0x40020004UL)
#define GPIOA_OSPEEDR REG32(0x40020008UL)
#define GPIOA_PUPDR REG32(0x4002000CUL)
#define GPIOA_AFRH REG32(0x40020024UL)
#define USART1_SR REG32(0x40011000UL)
#define USART1_DR REG32(0x40011004UL)
#define USART1_BRR REG32(0x40011008UL)
#define USART1_CR1 REG32(0x4001100CUL)
#define USART1_CR2 REG32(0x40011010UL)
#define USART1_CR3 REG32(0x40011014UL)
#define USART_RXNE (1UL << 5)
#define USART_TXE (1UL << 7)
void uart_init(void) {
 RCC_AHB1ENR |= 1UL; RCC_APB2ENR |= 1UL << 4; (void)RCC_AHB1ENR;
 GPIOA_MODER = (GPIOA_MODER & ~((3UL << 18) | (3UL << 20))) | (2UL << 18) | (2UL << 20);
 GPIOA_OTYPER &= ~((1UL << 9) | (1UL << 10));
 GPIOA_OSPEEDR |= (2UL << 18) | (2UL << 20);
 GPIOA_PUPDR = (GPIOA_PUPDR & ~((3UL << 18) | (3UL << 20))) | (1UL << 20);
 GPIOA_AFRH = (GPIOA_AFRH & ~((0xFUL << 4) | (0xFUL << 8))) | (7UL << 4) | (7UL << 8);
 USART1_CR1 = 0U; USART1_CR2 = 0U; USART1_CR3 = 0U; USART1_BRR = 0x008BUL;
 USART1_CR1 = (1UL << 13) | (1UL << 3) | (1UL << 2);
}
void uart_putc(char value) { while ((USART1_SR & USART_TXE) == 0U) {} USART1_DR = (uint8_t)value; }
void uart_puts(const char *text) { while (*text != '\0') { if (*text == '\n') uart_putc('\r'); uart_putc(*text++); } }
void uart_put_hex32(uint32_t value) { static const char h[]="0123456789ABCDEF"; uart_puts("0x"); for (uint32_t s=28U;;s-=4U) { uart_putc(h[(value>>s)&15U]); if (s==0U) break; } }
void uart_put_u32(uint32_t value) { char b[10]; uint32_t n=0U; if(value==0U){uart_putc('0');return;} while(value!=0U){b[n++]=(char)('0'+value%10U);value/=10U;} while(n!=0U)uart_putc(b[--n]); }
int uart_getc_nonblocking(char *value) { if ((USART1_SR & USART_RXNE)==0U) return 0; *value=(char)(uint8_t)USART1_DR; return 1; }
