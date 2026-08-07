/*
 * Constraints:
 *
 * 1. Can not transfer more than 260 bytes at once via a single interrupt trigger
 *
 * 2. In order to minimize copying contents between arrays and to maintain the interrupt-driven design of
 *    this driver, when passing pay loads into driver functions always them only as pointers to global arrays.
 *    Never pass local arrays, and if you would like to utilize a general use case array for transferring and
 *    receiving bytes, use transfer_arr and incoming_arr
 */

#include "../Inc/stm32l476xx.h"

#include "spi.h"
#include "bsp.h"
#include "flash.h"


#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#if !defined(__SOFT_FP__) && defined(__ARM_FP)
#endif

// Define text
#define TEST_STR "This is the string"
#define TEST_LEN (sizeof(TEST_STR) - 1)

uint8_t transfer_payload[TEST_LEN] = TEST_STR;
// Null term
uint8_t receive_payload[TEST_LEN + 1] = {0};

int main(void)
{
	// Call mount, NEVER CALL INIT unless your sure all blocks are cleared
	// TODO: Only give Mount access
	PROFILE_PIN_INIT();
	BOUNCE_SINGLE_LONG_PROFILER();
	SPI_Setup();

	// Clear sector + program
	Flash_Erase_Sector(0x000000);
	Flash_Page_Program(0x000000, transfer_payload, TEST_LEN);

	// BREAK P 1

	// Clear RAM buf
	memset(receive_payload, 0, sizeof(receive_payload));

	// Read back from chip loc
	Flash_Read_Data(0x000000, receive_payload, TEST_LEN);

	// Break P 2: By here there should be TEST_STR in the receive_payload array
	for (;;) {

	}

}
