#include "ftl.h"

#include <string.h>

// Global file scope usage variables and data structures
// Holds 0 or 1 based on which block is currently holding the data, use this to see which parts of array to look at 0-15 or 16-31 idxs
static int block_in_use = 0;
// Hold 32 physical sectors at one time TOTAL, even though 16 are not in use at all at a time
static PhysicalSectorMetadata_t FTL_Phys_Page_Meta_Arr[FTL_RESERVED_PHYSICAL_SECTORS]; // 32
// Allow use access to 8 logical sectors at one time (over provisioning)
// Each index is each of the eight logical sectors, and the corresponding value is the physical sector
static uint16_t L2P[FTL_LOGICAL_SECTORS]; // 8


static uint16_t next_clean_sector_idx = 0;
// Hold the next of the 16 pages in a sector in the corresponding sector you that is not yet fully filled
// Lets say we have written up to half of page five in sector two; append_page_idx_arr[2] = 5;
// Scan through page five of sector two and whichever byte was the last none 0xFFFF, can write after that.
// This is used for appends to a sector only, not for overwrites or clean new writes
static uint8_t append_page_idx_arr[FTL_RESERVED_PHYSICAL_SECTORS]; // 32



void FTL_Init(void) {
	// Mark all logical sectors as unmapped
	for (int i = 0; i < FTL_LOGICAL_SECTORS; i++) {
		L2P[i] = FTL_UNMAPPED;
	}

	// Initialize physical sector metadata
	for (int i = 0; i < FTL_RESERVED_PHYSICAL_SECTORS; i++) {
		FTL_Phys_Page_Meta_Arr[i].logical_sector_owner = FTL_UNMAPPED;
		FTL_Phys_Page_Meta_Arr[i].state = FREE_SECTOR;
	}

	for (int i = 0; i < FTL_RESERVED_PHYSICAL_SECTORS; i++) {
		append_page_idx_arr[i] = 0;
	}
}

void FTL_Mount(void) {
	// IMPORTANT: On Power Downs, we have to boot up differently; read in FLASH metadata

	/*
	 * Set current usage block to zero, and within that block set
	 * sector idx zero as nxt_cln_sctr_idx of metadata array
	 *
	 * Speaking of, create metadata array: 16 elements representing
	 * each of the physical sectors in the currently used block
	 *  - Each element is PhysicalSectorMetadata_t type
	 *
	 * Create the L2P mapping array, start all of them out at current
	 * nxt_cln_sctr_idx value (zero), from now on when any L Page is
	 * requested for a new or overwrite, just give it
	 * nxt_cln_sctr_idx + 1, and then increment nxt_cln_sctr_idx by one
	 * Then update that logic index in L2P with correct physical value
	 *
	 * Also, build append_page_idx_arr that stores 16 uint8_t's that
	 * say which page of the corresponding sector can be appended to.
	 */

	next_clean_sector_idx = 0; // TODO: Set this to either 0 or 16 based on which block is currently in use
}
bool FTL_Write_Sector(uint16_t logical_sector, const uint8_t *payload_buf);
bool FTL_Read_Sector(uint16_t logical_sector, uint8_t *incoming_payload_buff);
void FTL_GarbageCollect(void);
