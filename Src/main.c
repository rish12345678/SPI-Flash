/*
 * Constraints:
 *
 * 1. Can not transfer more than 100 bytes at once via a single interrupt trigger
 */

#include "../Inc/stm32l476xx.h"

#include "spi.h"
#include "bsp.h"


#include <stdint.h>
#include <stdbool.h>

#if !defined(__SOFT_FP__) && defined(__ARM_FP)
#endif

// Main runs a simple loop back test by connecting the MOSI line to the MISO pin

int main(void)
{
// // THIS IS ONLY FOR SINGLE TIME LOOP BACK TESTS
//	bool done_check = false;
//	((void) done_check);

	PROFILE_PIN_INIT();
	// Now sets up SPI Peripheral + Interrupt Specifics
	BOUNCE_SINGLE_LONG_PROFILER();
	SPI_Setup();


//	Toggle_Profile_Pin_Low();
//	PROFILE_PIN_INIT();


//	for (int j = 0; j < 2; j++) {
//		Toggle_Profile_Pin_Low();
//		for (volatile int i = 0; i < 2; i++);
//		Toggle_Profile_Pin_High();
//		for (volatile int i = 0; i < 2; i++);
//	}

	SPI_Interrupt_Send_Payload(transfer_arr, incoming_arr, 4);


//
	for (;;) {
		uint8_t man_code = incoming_arr[1];
		if (Get_Spi_State() == 0) {
			STUTTER_PROFILER();
		}
		for (volatile int i = 0; i < 100; i++);
		((void) man_code);
		/*
		 * Loop back test Here
		 */


		if (incoming_arr[1] != 0xEF) BOUNCE_SINGLE_LONG_PROFILER();

//
////		if (!done_check) {
//		for (int i = 0; i < user_def_transfer_len; i++) {
//			if (incoming_arr[i] != transfer_arr[i]) {
//				Toggle_Profile_Pin_High();
//				for (volatile int i = 0; i < 10; i++);
//				Toggle_Profile_Pin_Low();
//				for (volatile int i = 0; i < 50; i++);
//			}
////			done_check = true;
//		}
////		}

		for (volatile int i = 0; i < 10000; i++);

	}

}
