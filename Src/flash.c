#include "flash.h"
#include "spi.h"

void Flash_Poll_Until_Ready(void) {
	// Read Status Register 1 -> Poll until SPI Flash Chip says it is not BUSY
	// Call this before any operation!!

	transfer_arr[0] = FLASH_READ_STATUS_1_CMND;  // 0x05 - Read Status Register 1

	transfer_arr[1] = DUMMY;

	// Set length to two bytes
	user_def_transfer_len = 2;

	for (;;) {
		// Send out the read status register opcode, grab the register value that follows
		// Call SPI Interrupt driver function to send byte
		SPI_Interrupt_Send_Payload(transfer_arr, incoming_arr, user_def_transfer_len);

		// Wait for transmission to complete
		while (Get_Spi_State() != SPI_READY_STATE);

		// Check it's zero'th bit, a zero is not BUSY and a 1 is BUSY
		// if read status register says its NOT BUSY -> return
		if (!(incoming_arr[1] & FLASH_SR1_STATUS_MSK)) return;
	}
}

void Flash_Set_Write_Enable(void) {
	// Wait until the Chip's state machine is settled before sending anything, even this
	Flash_Poll_Until_Ready();

	// Load up our array with FLASH_WRITE_EN_CMND
	transfer_arr[0] = FLASH_WRITE_EN_CMND;
	// Set len to one byte
	user_def_transfer_len = 1;
	// Call SPI Interrupt driver function to send byte
	SPI_Interrupt_Send_Payload(transfer_arr, incoming_arr, user_def_transfer_len);

	// Poll until SMTHG ----> Maybe come back here!!
	while (Get_Spi_State() != SPI_READY_STATE);
}

void Flash_Page_Program(uint32_t adr, const uint8_t *buf, uint16_t len) {
	/*
	 * Page Program Procedure according to ds:
	 *
	 * Write Enable Command to set the Write Enable Latch on, allowing for writes(WEL = 1)
	 *  - Poll here, make sure next line does not execute until Chip's state machine reads
	 *    WEL = 1
	 *
	 * Poll until chip is not busy (BSY = 0)
	 *
	 * Send opcode(0x02) followed by send 24 bit adr, fifth byte and onward is payload
	 * 	- Must send at least one data byte (total min five byets)
	 *
	 * After the last byte gets sent / received and we pull CS high, begins write in chip
	 *
	 * The chip hardware will now do its internal write for tpp time
	 *  - While the page program operation is happening on chip, can poll
	 *    read_status_register and check on BUSY flag to see when goes to zero and done
	 *    with write, ready for next operation, after Page Program Cycle completes
	 *    Write Enable Latch (WEL) gets set back to 0, not allowing any more new writes
	 *
	 * Page Programs will not execute if page or sector or block protected by protect lock
	 */

	// Handle errors here: For now just return
	if (adr < 0 || adr > 0x00FFFFFF) return;
	// If pay-load larger than 256, too big for our transfer_arr
	if (len > MAX_TRANSFER_LEN - 4) return;
	if (buf == 0) return;

	// Send WE cmnd to flip on WEL flag
	Flash_Set_Write_Enable();

	// Make sure SPI Chip is done with Write Enable cmnd
	Flash_Poll_Until_Ready();

	// Now that the chip is allowed to do a write + not occupied
	transfer_arr[0] = FLASH_PAGE_PROGRAM_CMND;

	// Set the address bytes
	// 0x00123456
	transfer_arr[1] = (0xFF & (adr >> 16));
	transfer_arr[2] = (0xFF & (adr >> 8));
	transfer_arr[3] = (0xFF & adr);
	// Set len whatever the user wants to send plus the four byte header
	user_def_transfer_len = len + 4;

	for (int i = 0; i < len; i++) {
		transfer_arr[i + 4] = *(buf + i);
	}
	// Call SPI Interrupt driver function to send transmission
	SPI_Interrupt_Send_Payload(transfer_arr, incoming_arr, user_def_transfer_len);

	while (Get_Spi_State() != SPI_READY_STATE);
}

void Flash_Read_Data(uint32_t adr, uint8_t *buf, uint16_t len) {
	/*
	 * Poll BSY, wait until not busy
	 *
	 * Now, load up 0x03 followed by adr of first byte to read, followed by num bytes read
	 *
	 * Send it
	 */

	if (adr < 0 || adr > 0x00FFFFFF) return;
	// If read-load larger than 256, too big for our incoming_arr
	if (len > MAX_TRANSFER_LEN - 4) return;
	if (buf == 0) return;



	// Make sure SPI Chip is done with Write Enable cmnd
	Flash_Poll_Until_Ready();

	// Now that the chip is allowed to do a write + not occupied
	transfer_arr[0] = FLASH_READ_DATA_CMND;

	// Set the address bytes
	// 0x00123456
	transfer_arr[1] = (0xFF & (adr >> 16));
	transfer_arr[2] = (0xFF & (adr >> 8));
	transfer_arr[3] = (0xFF & adr);
	// Set len whatever the user wants to send plus the four byte header
	user_def_transfer_len = len + 4;

	for (int i = 0; i < len; i++) {
		transfer_arr[i + 4] = DUMMY;
	}
	// Call SPI Interrupt driver function to send transmission
	SPI_Interrupt_Send_Payload(transfer_arr, incoming_arr, user_def_transfer_len);

	// Wait for SPI transfer to finish
	while (Get_Spi_State() != SPI_READY_STATE);

	// Copy chip's memory from incoming_arr to user's buffer
	for (int i = 0; i < len; i++) {
		*(buf + i) = incoming_arr[i + 4];
	}
}

void Flash_Erase_Sector(uint32_t adr) {
	/*
	 * Call Write Enable Instruction
	 *
	 * Poll until not busy
	 *
	 * Send over 0x20 followed by addr
	 *
	 * Once CS pulled high, starts the erase, for a sector takes 45 - 400 ms
	 *
	 * Then, WEL and BSY go to zero
	 */


	if (adr < 0 || adr > 0x00FFFFFF) return;
	// Look into allowing unaligned erases later; rn keep aligned req
	if (adr % 4096 != 0) return;

	// Send WE cmnd to flip on WEL flag
	Flash_Set_Write_Enable();

	// Make sure SPI Chip is done with Write Enable cmnd
	Flash_Poll_Until_Ready();

	transfer_arr[0] = FLASH_SECTOR_ERASE_CMND;

	// Set the address bytes
	// 0x00123456
	transfer_arr[1] = (0xFF & (adr >> 16));
	transfer_arr[2] = (0xFF & (adr >> 8));
	transfer_arr[3] = (0xFF & adr);

	user_def_transfer_len = 4;

	SPI_Interrupt_Send_Payload(transfer_arr, incoming_arr, user_def_transfer_len);

	while (Get_Spi_State() != SPI_READY_STATE);
}

// Erase an entire 64 KB block (Used primarily during Garbage Collection)
// Time 150 - 2000 ms
void Flash_Erase_Block(uint32_t adr) {
	/*
	 * Call Write Enable Instruction
	 *
	 * Poll until not busy
	 *
	 * Send over 0x20 followed by addr
	 *
	 * Once CS pulled high, starts the erase, for a sector takes 45 - 400 ms
	 *
	 * Then, WEL and BSY go to zero
	 */


	if (adr < 0 || adr > 0x00FFFFFF) return;
	// Look into allowing unaligned erases later; rn keep aligned req
	if (adr % 4096 != 0) return;

	// Send WE cmnd to flip on WEL flag
	Flash_Set_Write_Enable();

	// Make sure SPI Chip is done with Write Enable cmnd
	Flash_Poll_Until_Ready();

	transfer_arr[0] = FLASH_BLOCK_ERASE_CMND;

	// Set the address bytes
	// 0x00123456
	transfer_arr[1] = (0xFF & (adr >> 16));
	transfer_arr[2] = (0xFF & (adr >> 8));
	transfer_arr[3] = (0xFF & adr);

	user_def_transfer_len = 4;

	SPI_Interrupt_Send_Payload(transfer_arr, incoming_arr, user_def_transfer_len);

	while (Get_Spi_State() != SPI_READY_STATE);
}


