#ifndef FLASH_H_
#define FLASH_H_

#include <stdint.h>
#include <stdbool.h>

// Main Opcodes
#define FLASH_WRITE_EN_CMND 0x06
#define FLASH_READ_STATUS_1_CMND 0x05
#define FLASH_PAGE_PROGRAM_CMND 0x02
#define FLASH_SECTOR_ERASE_CMND 0x20
#define FLASH_READ_DATA_CMND 0x03

#define FLASH_SR1_STATUS_MSK 0x01 // Check bit zero; 0 = free, 1 = busy

#define DUMMY 0xFF;




// Size of different addressable memory chunks in bytes
/*
 * Total : 16 MB
 * 16 MB = 256 blocks
 * 1 Block = 16 sectors
 * 1 Sector = 16 pages
 * 1 Page = 256 bytes
 */
#define FLASH_PAGE_SIZE 256
#define FLASH_SECTOR_SIZE 4096
#define FLASH_BLOCK_SIZE 65536


// API
void Flash_Poll_Until_Ready(void);
void Flash_Set_Write_Enable(void);
void Flash_Erase_Sector(uint32_t adr);
void Flash_Page_Program(uint32_t adr, const uint8_t *buf, uint16_t len);
void Flash_Read_Data(uint32_t adr, uint8_t *buf, uint16_t len);

#endif
ftl;
