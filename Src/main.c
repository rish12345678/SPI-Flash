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

#include "../image_data.h"

#include "spi.h"
#include "bsp.h"
#include "flash.h"
#include "ftl.h"


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

uint8_t rec_pay[270] = {0};

void flash_image_to_ftl(void) {
	FTL_Mount();

    uint32_t bytes_remaining = IMAGE_SIZE_BYTES;
    uint32_t array_offset = 0;
    uint16_t logical_sector = 0;

    while (bytes_remaining > 0) {
        static uint8_t sector_buffer[FTL_SECTOR_SIZE]; // 4096-byte buffer in bss

        // Determine how many bytes to pack into this sector (up to 4096)
        uint16_t bytes_to_write = (bytes_remaining > FTL_SECTOR_SIZE) ? FTL_SECTOR_SIZE : bytes_remaining;

        // Copy chunk from C array to buffer
        memcpy(sector_buffer, &IMAGE_BYTE_ARRAY[array_offset], bytes_to_write);

        // Write to current logical sector using FTL API
        bool success = FTL_Write_Sector(logical_sector, sector_buffer, bytes_to_write);
        if (!success) {
            // printf("Error writing to Logical Sector %d!\n", logical_sector);
            return;
        }

        // Advance pointers
        bytes_remaining -= bytes_to_write;
        array_offset += bytes_to_write;
        logical_sector++;
    }

//    printf("Done");
}


void read_image_from_ftl_and_dump_uart(void) {
    // 1. Mount FTL and reconstruct RAM L2P tables from Flash headers
    FTL_Mount();

    uint32_t bytes_remaining = IMAGE_SIZE_BYTES; // Or stored size metadata
    uint16_t logical_sector = 0;



    while (bytes_remaining > 0) {
        static uint8_t read_buffer[FTL_SECTOR_SIZE];
        uint16_t bytes_to_read = (bytes_remaining > FTL_SECTOR_SIZE) ? FTL_SECTOR_SIZE : bytes_remaining;

        // Read sector through L2P translation
        FTL_Read_Sector(logical_sector, read_buffer, 0, bytes_to_read);

        // Print raw hex over UART/Serial Terminal (or write to file)
        for (int i = 0; i < bytes_to_read; i++) {
             // Example UART output or stdout
             // printf("%02X", read_buffer[i]);
        }

        bytes_remaining -= bytes_to_read;
        logical_sector++;
    }

//    printf("\n=== READ COMPLETE ===\n");
}


int main(void)
{
	// Call mount, NEVER CALL INIT unless your sure all blocks are cleared
	// TODO: Only give Mount access
	PROFILE_PIN_INIT();
	BOUNCE_SINGLE_LONG_PROFILER();
	SPI_Setup();

	FTL_Mount();

	FTL_Read_Sector(2, rec_pay, 0, 100);


//
//	// Clear sector + program
//	Flash_Erase_Sector(0x000000);
//	Flash_Page_Program(0x000000, transfer_payload, TEST_LEN);
//
//	// BREAK P 1
//
//	// Clear RAM buf
//	memset(receive_payload, 0, sizeof(receive_payload));
//
//	// Read back from chip loc
//	Flash_Read_Data(0x000000, receive_payload, TEST_LEN);

	// Break P 2: By here there should be TEST_STR in the receive_payload array
	for (;;) {

	}

}
