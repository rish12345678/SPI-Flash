#ifndef SPI_H_
#define SPI_H_

#include <stdint.h>
#include "stm32l476xx.h"

#define MAX_TRANSFER_LEN 100 // How many bytes we are sending

// Array to hold outgoing bytes
extern volatile uint8_t transfer_arr[MAX_TRANSFER_LEN];

// Array to capture incoming bytes
extern volatile uint8_t incoming_arr[MAX_TRANSFER_LEN];


extern volatile uint8_t* ptr_TXData_Base;
extern volatile uint8_t* ptr_RXData_Base;


extern volatile uint8_t* ptr_TXData;
extern volatile uint8_t* ptr_RXData;

extern volatile uint32_t user_def_transfer_len;




/*
 * SPI State Machine Type
 */

typedef enum {
    SPI_READY_STATE = 0, // SPI peripheral is free for use
	SPI_BUSY_STATE // SPI peripheral is busy in transmission
} SPI_State_t;



/*
 * FUNCTION DECLARATIONS
 */




// SPI State Check Function
SPI_State_t Get_Spi_State(void);


// Sets up configurations for the SPI Peripheral before triggering a transmission
void SPI_Setup(void);

// This is the code that executes in the ISR every time the hardware triggers an interrupt in the SPI's IT line
void SPI1_IRQHandler(void);

// This trigger function sends over a pay load starting at transfer_PTR of length trans_len with received
// bytes of equal length starting at receive_PTR

// HOW TO CALL: Make sure to just through this call in an if, if it evals to true the transmission went through
bool SPI_Interrupt_Send_Payload(volatile uint8_t* transfer_PTR, volatile uint8_t* receive_PTR, uint32_t trans_len);

/*
 * FUNCTION DEFINITION ---- Inline Only
 */

__attribute__((always_inline)) inline void SPI_SEND_BYTE_POLLING(void) {
	// Pull chip select low, to indicate communication
	GPIOA->ODR &= ~(1U << 4);

	// while TX buffer not empty, keep polling
	// once empty give it byte
	while(ptr_TXData - ptr_TXData_Base < MAX_TRANSFER_LEN) {
		// Check if bit 1 is 0
		while (!(SPI1->SR & SPI_SR_TXE));

		*(__IO uint8_t *)&SPI1->DR = *ptr_TXData; // Sample Data to send 0x84 | 0b 1000 0100
		ptr_TXData++;

		while (!(SPI1->SR & SPI_SR_RXNE)); // Poll until RX buffer has a byte in it
		*ptr_RXData = *(__IO uint8_t *)&SPI1->DR;
		ptr_RXData++;
	}

	while (SPI1->SR & SPI_SR_BSY); // Poll until shift register is not busy in transmission

	GPIOA->ODR |= (1U << 4);
}


#endif // SPI_H_
