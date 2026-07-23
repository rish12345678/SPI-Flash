#ifndef BSP_H_
#define BSP_H_

#include "stm32l476xx.h"

#define PROFILE_GPIO_PORT GPIOA
#define PROFILE_PIN 1
#define CS_LOW (GPIOA->ODR &= ~(1U << 4))
#define CS_HIGH (GPIOA->ODR |= (1U << 4))

#ifdef ENABLE_PROFILE
	#define PROFILE_PIN_INIT() { \
        RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN; \  // provide clock as well, SPI_SETUP() is late
        GPIOA->MODER &= ~(GPIO_MODER_MODE1_Msk); \
        GPIOA->MODER |= GPIO_MODER_MODE1_0; \
        GPIOA->OSPEEDR |= GPIO_OSPEEDR_OSPEED1_1; \
    } // Minimal slew rate

	#define Toggle_Profile_Pin_High() (GPIOA->BSRR = (GPIO_BSRR_BS1))
	#define Toggle_Profile_Pin_Low() (GPIOA->BRR = (GPIO_BRR_BR1))

#else
	// Disable to eliminate profiling pin in prod
	#define PROFILE_PIN_INIT() ((void) 0) // no-op

	#define Toggle_Profile_Pin_High() ((void) 0)
	#define Toggle_Profile_Pin_Low() ((void) 0)

#endif

#endif // BSP_H_
