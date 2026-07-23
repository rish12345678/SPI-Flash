#include "../Inc/stm32l476xx.h"

#define ENABLE_PROFILE // Turn on Profiling Pin Settings
#include "spi.h"
#include "bsp.h"


#include <stdint.h>
#include <stdbool.h>

#if !defined(__SOFT_FP__) && defined(__ARM_FP)
#endif

// Main runs a simple loop back test by connecting the MOSI line to the MISO pin

int main(void)
{
	PROFILE_PIN_INIT();
	// Now sets up SPI Peripheral + Interrupt Specifics
	Toggle_Profile_Pin_High();
	Toggle_Profile_Pin_Low();
	SPI_Setup();

	SPI_IT_Trigger();


//	Toggle_Profile_Pin_Low();
//	PROFILE_PIN_INIT();
//
	for (;;) {
//		Toggle_Profile_Pin_High();
//		SPI_SEND_BYTE_POLLING(); // Should load up incoming_arr[] with {0x24, 0x48, 0x11, 0x54}
//		Toggle_Profile_Pin_Low();
//
////		for (int i = 0; i < TRANSFER_LEN; i++) {
////			if (!(transfer_arr[i] == incoming_arr[i])) {
////				while (1);
////			}
////		}
//		for(volatile int i = 0; i < 4000; i++); // ~10 ms between transmissions
	}

}
