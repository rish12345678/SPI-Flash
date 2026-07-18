#ifndef SPI_H_
#define SPI_H_

#include <stdint.h>
#include "stm32l476xx.h"

#define TRANSFER_LEN 4 // How many bytes we are sending

// Array to hold outgoing bytes
extern volatile uint8_t transfer_arr[TRANSFER_LEN];

// Array to capture incoming bytes
extern volatile uint8_t incoming_arr[TRANSFER_LEN];

extern volatile uint8_t incoming_idx;
extern volatile uint8_t outgoing_idx;

/*
 * FUNCTION DECLARATIONS
 */

void SPI_Setup(void);

void SPI_SEND_BYTE_POLLING(void);


#endif // SPI_H_
