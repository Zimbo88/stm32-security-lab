#include "gpio.h"
#include "registers.h"

#define GPIOHEN    (1UL << 7)
#define PH12_PIN   12U
#define PH12_MASK  (1UL << PH12_PIN)

void heartbeat_init(void)
{
    RCC_AHB1ENR |= GPIOHEN;
    (void)RCC_AHB1ENR;

    GPIO_MODER(GPIOH_BASE) &=
        ~(3UL << (PH12_PIN * 2U));

    GPIO_MODER(GPIOH_BASE) |=
        (1UL << (PH12_PIN * 2U));

    GPIO_OTYPER(GPIOH_BASE) &= ~PH12_MASK;
    GPIO_OSPEEDR(GPIOH_BASE) &=
        ~(3UL << (PH12_PIN * 2U));

    GPIO_PUPDR(GPIOH_BASE) &=
        ~(3UL << (PH12_PIN * 2U));

    GPIO_BSRR(GPIOH_BASE) = PH12_MASK << 16U;
}

void heartbeat_toggle(void)
{
    if ((GPIO_ODR(GPIOH_BASE) & PH12_MASK) != 0U) {
        GPIO_BSRR(GPIOH_BASE) = PH12_MASK << 16U;
    } else {
        GPIO_BSRR(GPIOH_BASE) = PH12_MASK;
    }
}
