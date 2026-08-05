#include "flash.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// FLASH SIMULATION - Just do two blocks for now, only need two
uint8_t SIM_FLASH[MOCK_FLASH_SIZE];

void Mock_Flash_Init(void)
{
    // Erase on entire Flash simu
    memset(SIM_FLASH, 0xFF, sizeof(SIM_FLASH));
}

void Flash_Dump(uint32_t start_adr, uint32_t len)
{
    printf("\n----------------------------------------------------\n");
    printf("\nFLASH HEX DUMP (Address: 0x%05X, Len: %d)\n", start_adr, len);
    for (uint32_t i = 0; i < len; i++)
    {
        if (i % 16 == 0)
        {
            uint32_t print_adr = start_adr + i;
            printf("\n0x%05X: ", print_adr);
        }

        printf("%02X ", SIM_FLASH[start_adr + i]);
    }
    printf("\n----------------------------------------------------\n");
}

void Flash_Read_Data(uint32_t address, uint8_t *buffer, uint16_t length)
{
    if (buffer == NULL)
        return;
    if (address + length > MOCK_FLASH_SIZE)
        return; // Quick bouncs check

    memcpy(buffer, &SIM_FLASH[address], length);
}

// Write to adr
void Flash_Page_Program(uint32_t address, const uint8_t *data, uint16_t length)
{
    if (data == NULL)
        return;
    if (address + length > MOCK_FLASH_SIZE)
        return;

    for (uint32_t i = 0; i < length; i++)
    {
        // Simulate ony 1 -> 0 with bitwise &
        SIM_FLASH[address + i] &= data[i];
    }
}

// Erase whole block, 64 KB Block
void Flash_Erase_Block(uint32_t block_address)
{
    uint32_t block_size = 16 * 16 * 256;
    if (block_address + block_size > MOCK_FLASH_SIZE)
        return;

    memset(&SIM_FLASH[block_address], 0xFF, block_size);
    return;
}
