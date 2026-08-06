#ifndef FTL_H_
#define FTL_H_

#include <stdint.h>
#include <stdbool.h>
/*
 * Initial Design:
 *
 * Double buffering between Block 0 and Block 1, with clean block-leveling erase in between transfers
 * Writes and overwrites occur at the sector level
 */

// Page + Sector Dimensions
#define FTL_PAGE_SIZE 256 // Bytes per page
#define FTL_PAGES_PER_SECTOR 16 // 16 pages * 256 bytes = 4096 bytes per Sector
#define FTL_SECTOR_SIZE 4096 // Size of one physical sector
#define FTL_BLOCK_SIZE 65536

// Break down of zone in Flash managed and used by the FTL
#define FTL_RESERVED_PHYSICAL_SECTORS 32 // Physical Sectors 0 to 31 reserved for FTL -> 0-15 Block 1, 16-31 Block 2
#define FTL_SECTORS_PER_BLOCK 16

// User application gets eight logical sectors, that map to various physical sectors
#define FTL_LOGICAL_SECTORS 8

// Urgency of garbage collection need
#define FTL_URGENT_GC_NEEDED 14
#define FTL_VITAL_GC_NEEDED 15

#define NUM_BLOCKS_PER_REGION 1

// Currently using block 0 as buffer 0, and block 1 as buffer 1, could change later, so defining here
#define FTL_REGION_0 0
#define FTL_REGION_1 1

// Flash MetaData sizes
#define FTL_Flash_Logical_Page_Meta 2 // two bytes
#define FTL_Flash_GC_State_Meta 1 // one byte

// Due to Flash Per Region & Per Sector & Per Page Metadate, this is the useable memory
#define FTL_METADATA_PER_PAGE 4
#define FTL_USABLE_BYTES_PER_PAGE 252

// GC State-Machine Representations
#define GC_META_ERASED_BLOCK 0xFF
#define GC_META_VALID_BLOCK 0xFC // 0 b 1111 1100
#define GC_META_TRANSFERING_OUT_BLOCK 0xF8 // 0 b 1111 1000
#define GC_META_OBSOLETE_BLOCK 0xFA


#define FTL_UNMAPPED 0xFFFF

#define CLEAN_META {0xFF, 0xFF, 0xFF, 0xFF}

// This is the state of each of the currently 16 PHYSICAL sectors in the currently used block
typedef enum {
	FREE_SECTOR = 0, // This sector is erased, all 0xFF
	VALID_SECTOR,    // Active valid data
	DIRTY_SECTOR     // Dirty data -> Has been overwritten, ready to be GCed
} SectorState_t;

// Metadata struct for tracking physical sector states
typedef struct {
    uint16_t logical_sector_owner; // sector owner number mapped here
    SectorState_t state; // FREE, VALID, or DIRTY
} PhysicalSectorMetadata_t;

// FTL API Functions
void FTL_Init(void);
void FTL_Mount(void);
bool FTL_Write_Sector(uint16_t logical_sector, const uint8_t *payload_buf, int payload_len);
bool FTL_Append_Sector(uint16_t logical_sector, const uint8_t *payload_buf, int payload_len);
uint16_t FTL_Read_Sector(uint16_t logical_sector, uint8_t *incoming_payload_buff, int sector_offset, int payload_len);
void FTL_GarbageCollect(void);

#endif
