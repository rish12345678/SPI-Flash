#ifndef SPI_H_
#define SPI_H_

#include <stdint.h>
#include "stm32l476xx.h"

#define TRANSFER_LEN 4 // How many bytes we are sending

// Array to hold outgoing bytes
extern volatile uint8_t transfer_arr[TRANSFER_LEN];

// Array to capture incoming bytes
extern volatile uint8_t incoming_arr[TRANSFER_LEN];
//
//extern uint8_t incoming_idx;
//extern uint8_t outgoing_idx;

/*
 * FUNCTION DECLARATIONS
 */

void SPI_Setup(void);

__attribute__((always_inline)) inline void SPI_SEND_BYTE_POLLING(void) {
	uint8_t incoming_idx = 0;
	uint8_t outgoing_idx = 0;
	// Pull chip select low, to indicate communication
	GPIOA->ODR &= ~(1U << 4);

	// while TX buffer not empty, keep polling
	// once empty give it byte
	while(outgoing_idx < TRANSFER_LEN) {
		// Check if bit 1 is 0
		while (!(SPI1->SR & SPI_SR_TXE));

		*(__IO uint8_t *)&SPI1->DR = transfer_arr[outgoing_idx]; // Sample Data to send 0x84 | 0b 1000 0100
		outgoing_idx++;

		while (!(SPI1->SR & SPI_SR_RXNE)); // Poll until RX buffer has a byte in it
		incoming_arr[incoming_idx] = *(__IO uint8_t *)&SPI1->DR;
		incoming_idx++;
	}

	while (SPI1->SR & SPI_SR_BSY); // Poll until shift register is not busy in transmission

	GPIOA->ODR |= (1U << 4);
}


#endif // SPI_H_
