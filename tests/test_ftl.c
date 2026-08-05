/*
 * Worked on externally in VSCODE, can run this externally, will add Make to run locally as well
 */

#include "ftl.h"
#include "flash.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

void Mock_Flash_Init(void); // Not declared in prod ftl.h, only for testing

void Flash_Dump(uint32_t start_adr, uint32_t len);

void test_basic_write_and_read(void)
{
    printf("[TEST 1] Basic Write & Read....");

    uint8_t payload[] = "Hello Embedded World!";
    uint8_t read_buf[64] = {0};

    // Do the string write to sector 0
    bool write_ok = FTL_Write_Sector(0, payload, sizeof(payload));
    assert(write_ok == true);

    // Read back from sector zero again
    uint16_t bytes_read = FTL_Read_Sector(0, read_buf, 0, sizeof(payload));

    assert(bytes_read == sizeof(payload));
    assert(memcmp(payload, read_buf, sizeof(payload)) == 0);

    printf("[TEST 1] PASSED!\n");
}

// Test that FTL_Mount does not change values for Flash Memory on inital boot

// Test that RAM data structures are rebuilt properly after power-loss

// Test Read Short Page
// Bug Fixed: Was returning wrong number from short first page read (reading five bytes of page 0 containing 20 bytes)
void read_short_page(void)
{
    printf("[TEST 2] Testing a short read on an initial page....");

    uint8_t write_arr[] = "01234567890123456789";
    FTL_Write_Sector(0, write_arr, 20);

    uint8_t buf[10] = {0};
    uint16_t read_cnt = FTL_Read_Sector(0, buf, 0, 5); // Ask for only 5 bytes
    assert(read_cnt == 5);
    assert(memcmp(buf, "01234", 5) == 0);

    printf("[TEST 2] PASSED!\n");
}

// Test read accross page boundaries
void read_across_page_boundaries(void)
{
    printf("[TEST 3] Testing a read across page boundaries...\n");

    uint8_t write_arr[] = "01234567890123456789";
    FTL_Write_Sector(1, write_arr, 20);

    uint8_t append_arr[] = "ABCDEFGHIJKLMNOPQRST";
    FTL_Append_Sector(1, append_arr, 20);

    // Dump 512 bytes, all of P1 an P2
    Flash_Dump(0, 512);

    uint8_t buf[20] = {0};
    // read 20 bytes satrting at sector offset 10
    uint16_t read_cnt = FTL_Read_Sector(1, buf, 10, 20);

    printf("Bytes Read: %d\n", read_cnt);
    printf("Buffer Result: %.*s\n", read_cnt, buf);

    // Expected:
    // Page 0 ( idx : 10..19 ): "0123456789"
    // Page 1 ( idx : 0..9 ):   "ABCDEFGHIJ"
    assert(read_cnt == 20);
    assert(memcmp(buf, "0123456789ABCDEFGHIJ", 20) == 0);

    printf("[TEST 3] PASSED!\n");
}

// Test reading too much from a sector with too little
void test_extrenuous_reading(void)
{
    printf("[TEST 4] Testing read more bytes than exist in sector...\n");

    // Write a total of 16 bytes to this sector, across two pages (P1: 10, P2: 6)
    uint8_t write_arr[] = "Mock Flash";
    FTL_Write_Sector(2, write_arr, 10);
    uint8_t append_arr[] = " TEST!";
    FTL_Append_Sector(2, append_arr, 6);

    // Read 40 bytes from same logical sector
    uint8_t buf[40] = {0};
    uint16_t read_cnt = FTL_Read_Sector(2, buf, 0, 40);

    assert(read_cnt == 16); // Should only return actual user bytes written
    assert(memcmp(buf, "Mock Flash TEST!", 16) == 0);

    printf("[TEST 4] PASSED!\n");
}

// Test offset > total bytes written in sector
void test_huge_offset_read(void)
{
    printf("[TEST 5] Testing to start a read at offset greater than total bytes...\n");

    // Write a total of 16 bytes to this sector in one page
    FTL_Write_Sector(2, (uint8_t *)"Mock Flash TEST!", 16);

    // Read four bytes at offset 100
    uint8_t buf[4] = {0xAB, 0xAB, 0xAB, 0xAB};
    int sector_offset = 100;
    uint16_t read_cnt = FTL_Read_Sector(2, buf, sector_offset, 4);

    assert(read_cnt == 0);                                                        // Should return 0;
    assert(buf[0] == 0xAB && buf[1] == 0xAB && buf[2] == 0xAB && buf[3] == 0xAB); // buf should contain what it was initialized with (no bytes were moved in)

    printf("[TEST 5] PASSED!\n");
}

// Test offset > total bytes written in sector
// Bug Fix: Missing Increment of total_bytes_read by bytes_read for each page read in a multi-page read
void test_read_across_several_pages(void)
{
    printf("[TEST 6] Testing to read across several pages...\n");

    // Write a total of 16 bytes to this sector in one page
    // IMPORTANT: Use strlen to avoid adding null term char
    FTL_Append_Sector(4, (uint8_t *)"Mock Flash TEST 1!\n", strlen("Mock Flash TEST 1!\n")); // Append on a non-first-written page may cause bug
    FTL_Append_Sector(4, (uint8_t *)"Mock Flash TEST 2!\n", strlen("Mock Flash TEST 1!\n"));
    FTL_Append_Sector(4, (uint8_t *)"Mock Flash TEST 3!\n", strlen("Mock Flash TEST 1!\n"));
    FTL_Append_Sector(4, (uint8_t *)"Mock Flash TEST 4!\n", strlen("Mock Flash TEST 1!\n"));

    // int a = strlen("Mock Flash TEST 1!\n");
    // printf("a: %d", a);
    // Allocate one extra space with 0x00 for null term when printing
    uint8_t buf[(strlen("Mock Flash TEST 1!\n") * 4) + 1] = {0};
    int sector_offset = 0;
    uint16_t read_cnt = FTL_Read_Sector(4, buf, sector_offset, 76);

    int adr_page_zero = (0 * FTL_SECTOR_SIZE) + (0 * FTL_PAGE_SIZE);
    Flash_Dump(adr_page_zero, FTL_PAGE_SIZE);

    int adr_page_one = (0 * FTL_SECTOR_SIZE) + (1 * FTL_PAGE_SIZE);
    Flash_Dump(adr_page_one, FTL_PAGE_SIZE);

    int adr_page_two = (0 * FTL_SECTOR_SIZE) + (2 * FTL_PAGE_SIZE);
    Flash_Dump(adr_page_two, FTL_PAGE_SIZE);

    int adr_page_three = (0 * FTL_SECTOR_SIZE) + (3 * FTL_PAGE_SIZE);
    Flash_Dump(adr_page_three, FTL_PAGE_SIZE);

    printf("\n-- Start Buf --\n");
    printf("%s", buf);
    printf("\n---- End Buf ----\n");

    assert(read_cnt == strlen("Mock Flash TEST 1!\n") * 4);
    assert(memcmp(buf, "Mock Flash TEST 1!\nMock Flash TEST 2!\nMock Flash TEST 3!\nMock Flash TEST 4!\n", 76) == 0);

    printf("[TEST 6] PASSED!\n");
}

// Test invalid read params
// TODO: Consider Negative Param Inputs
void test_invalid_read_params(void)
{
    printf("[TEST 7] Testing to read invalid read params...\n");

    // Write a total of 16 bytes to this sector in one page
    FTL_Append_Sector(4, (uint8_t *)"Mock Flash TEST 1!\n", 18); // Append on a non-first-written page may cause bug

    uint8_t buf[4] = {0};
    // uint8_t buf2[4] = NULL;
    uint8_t buf3[4] = {0};
    int sector_offset = 0;
    int sector_offset_2 = 4100;

    // This logical sector is out of bounds, should just return zero
    uint16_t read_1_cnt = FTL_Read_Sector(16, buf, sector_offset, sizeof(buf));

    // This input buffer is a null pointer, should just return zero
    uint16_t read_2_cnt = FTL_Read_Sector(4, 0, sector_offset, sizeof(buf));

    // This sector offset is past this sector boundary
    uint16_t read_3_cnt = FTL_Read_Sector(4, buf, sector_offset_2, sizeof(buf));

    assert(read_1_cnt == 0); // Should be an invalid read
    assert(memcmp(buf, "", 0) == 0);

    assert(read_2_cnt == 0); // Should be an invalid read

    assert(read_3_cnt == 0); // Should be an invalid read
    assert(memcmp(buf3, "", 0) == 0);

    printf("[TEST 7] PASSED!\n");
}

// Test power-loss recovery -> Write, bootup, read, validate data correctness

// Bug Fixed: Incorrectly returned bytes on first if-else in Read -> Was returning full page for partial page read
// Bug Fixed: Endianness Issue in build_first_free_page_table_and_page_payload_len_table() -> Was setting page_payload_len_table[sector][page] value to most significabt byte of page metadata instead of least significant byte (Little Endian on ARM)
void test_power_loss_recovery(void)
{
    printf("[TEST 8] Testing to validate data after power-loss recovery...\n");

    // Write a total of 16 bytes to this sector in one page
    FTL_Append_Sector(4, (uint8_t *)"Mock Flash TEST 1!\n", strlen("Mock Flash TEST 1!\n")); // Append on a non-first-written page may cause bug
    FTL_Append_Sector(4, (uint8_t *)"Mock Flash TEST 2!\n", strlen("Mock Flash TEST 1!\n"));
    FTL_Append_Sector(4, (uint8_t *)"Mock Flash TEST 3!\n", strlen("Mock Flash TEST 1!\n"));
    FTL_Append_Sector(4, (uint8_t *)"Mock Flash TEST 4!\n", strlen("Mock Flash TEST 1!\n"));

    FTL_Mount(); // Wipe all In-RAM data structures, and then rebuild from SIM_FLASH

    // Buffer size 77 for ending null term for printing
    uint8_t buf[(strlen("Mock Flash TEST 1!\n") * 4) + 1] = {0};
    int sector_offset = 0;
    uint16_t read_cnt = FTL_Read_Sector(4, buf, sector_offset, 76);

    printf("\n-- Start Buf --\n");
    printf("%s", buf);
    printf("\n\n---- End Buf ----\n\n");

    assert(read_cnt == strlen("Mock Flash TEST 1!\n") * 4);
    assert(memcmp(buf, "Mock Flash TEST 1!\nMock Flash TEST 2!\nMock Flash TEST 3!\nMock Flash TEST 4!\n", 76) == 0);

    printf("[TEST 8] PASSED!\n");
}

// Test offset right on a page boundary
void test_offset_on_page_boundary(void)
{
    printf("[TEST 9] Testing to read with offset on page boundary...\n");

    // Write a total of 16 bytes to this sector in one page
    FTL_Append_Sector(4, (uint8_t *)"Mock Flash TEST 1!\n", strlen("Mock Flash TEST 1!\n")); // Append on a non-first-written page may cause bug
    FTL_Append_Sector(4, (uint8_t *)"Mock Flash TEST 2!\n", strlen("Mock Flash TEST 1!\n"));
    FTL_Append_Sector(4, (uint8_t *)"Mock Flash TEST 3!\n", strlen("Mock Flash TEST 1!\n"));
    FTL_Append_Sector(4, (uint8_t *)"Mock Flash TEST 4!\n", strlen("Mock Flash TEST 1!\n"));

    // FTL_Mount(); // Wipe all In-RAM data structures, and then rebuild from SIM_FLASH

    // Buffer size 77 for ending null term for printing
    uint8_t buf[(strlen("Mock Flash TEST 1!\n") * 4) + 1] = {0};
    int sector_offset = 19;
    uint16_t read_cnt = FTL_Read_Sector(4, buf, sector_offset, 76);

    printf("\n-- Start Buf --\n");
    printf("%s", buf);
    printf("\n\n---- End Buf ----\n\n");

    assert(read_cnt == (strlen("Mock Flash TEST 1!\n") * 3));
    assert(memcmp(buf, "Mock Flash TEST 2!\nMock Flash TEST 3!\nMock Flash TEST 4!\n", 57) == 0);

    printf("[TEST 9] PASSED!\n");
}

// Redo Test 6 but with strange sector offsets

// TODO: Check RAM is built as default even with Mount on a clean SIM_FLASH

int main(void)
{
    Mock_Flash_Init(); // Clean erase on all flash memory
    FTL_Init();        // Initialialize FTL software layer based on SIM_FLASH

    // All tests
    test_basic_write_and_read(); // Write, check succesful, then read back

    Mock_Flash_Init();
    FTL_Init();

    read_short_page();

    Mock_Flash_Init();
    FTL_Init();

    read_across_page_boundaries();

    Mock_Flash_Init();
    FTL_Init();

    test_extrenuous_reading();

    Mock_Flash_Init();
    FTL_Init();

    test_huge_offset_read();

    Mock_Flash_Init();
    FTL_Init();

    test_read_across_several_pages();

    Mock_Flash_Init();
    FTL_Init();

    test_invalid_read_params();

    Mock_Flash_Init();
    FTL_Init();

    test_power_loss_recovery();

    Mock_Flash_Init();
    FTL_Init();

    test_offset_on_page_boundary();

    printf("All Tests Passed Succesfully!\n\n");
    return 0;
}
