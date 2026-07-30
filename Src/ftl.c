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

// Helper Functions
static int block_sector_page_offset_to_adr(int block, int sector, int page, int offset) {
	return (block * FTL_BLOCK_SIZE) + (sector * FTL_SECTOR_SIZE) + (page * FTL_PAGE_SIZE) + offset;
}

static uint8_t identify_block_in_use(void) {
	// Read in GC State Machine byte for both starting sectors of each section(currently in use and not)
	int region_0_gc_state_machine_adr = block_sector_page_offset_to_adr(FTL_REGION_0, 0, 0, FTL_Flash_Logical_Page_Meta);
	int region_1_gc_state_machine_adr = block_sector_page_offset_to_adr(FTL_REGION_1, 0, 0, FTL_Flash_Logical_Page_Meta);
	uint8_t region_0_gc_meta_buffer;
	uint8_t region_1_gc_meta_buffer;
	Flash_Read_Data(region_0_gc_state_machine_adr, &region_0_gc_meta_buffer, FTL_Flash_GC_State_Meta);
	Flash_Read_Data(region_1_gc_state_machine_adr, &region_1_gc_meta_buffer, FTL_Flash_GC_State_Meta);

	if (region_0_gc_meta_buffer == GC_META_VALID_BLOCK) {
		// Power went during general (non-gc) use of block 0
		return FTL_REGION_0;
	} else if (region_1_gc_meta_buffer == GC_META_VALID_BLOCK) {
		// Power went during general (non-gc) use of block 1
		return FTL_REGION_1;
	} else if (region_0_gc_meta_buffer == GC_META_TRANSFERING_OUT_BLOCK) {
		// Power went out mid transfer from block 0 to block 1, block 0 is still the source of truth
		return FTL_REGION_0;
	} else if (region_1_gc_meta_buffer == GC_META_TRANSFERING_OUT_BLOCK) {
		// Power went out mid transfer from block 1 to block 0, block 1 is still the source of truth
		return FTL_REGION_1;
	} else if (region_0_gc_meta_buffer == GC_META_OBSOLETE_BLOCK) {
		// Power went out mid hardware erase of block 0, block 1 is now the source of truth
		// TODO: In this case, power went our mid block 0 hardware erase, re-issue
		return FTL_REGION_1;
	} else if (region_1_gc_meta_buffer == GC_META_OBSOLETE_BLOCK) {
		// Power went out mid hardware erase of block 1, block 0 is now the source of truth
		// TODO: In this case, power went our mid block 1 hardware erase, re-issue
		return FTL_REGION_0;
	} else {
		// If both GC state machines show that they are "not in use", that means this is a fresh run
		// power did not go out, do not change anything
		return FTL_REGION_0;
	}

}



void FTL_Init(void) {

	/*
	 * ESSENTIALLY: Set everything to default here, will handle power-loss recov in mount if need be
	 *
	 * Set current usage block to zero, and within that block set
	 * sector idx zero as nxt_cln_sctr_idx of metadata array
	 *
	 * Speaking of, create metadata array: 16 elements representing
	 * each of the physical sectors in the currently used block
	 *  - Each element is PhysicalSectorMetadata_t type
	 *
	 * Create the L2P mapping array, start all of them out at current
	 * unmapped (0xFFFF), from now on when any Logical Sector is
	 * requested for a new or overwrite, just give it
	 * nxt_cln_sctr_idx + 1, and then increment nxt_cln_sctr_idx by one
	 * Then update that logic index in L2P with correct physical value
	 *
	 * Also, build append_page_idx_arr that stores 16 uint8_t's that
	 * say which page of the corresponding sector can be appended to.
	 */

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

	block_in_use = 0;
	next_clean_sector_idx = 0; // TODO: Set this to either 0 or 16 based on which block is currently in use
}

void FTL_Mount(void) {
	// IMPORTANT: On Power Downs, we have to boot up differently; read in FLASH metadata

	/*
	 * ESSENTIALLY: Set up all global and file-local data structures and variables based on Flash
	 * If this is a new clean boot, it shouldn't change anything from FTL_Init(void)
	 *
	 * The only metadata we are storing is as follows:
	 *
	 * Sector 0 & 16: [ | uint16_t: Logical Page # || uint8_t: GC State Machine || CLEAN USABLE MEMORY]
	 *
	 * For any sector other than the first of the two blocks we are using as double buffers, ex. Sector 10
	 *
	 * Sector 1-15 & 17-31: [ | uint16_t: Logical Page # || CLEAN USABLE MEMORY ]
	 *
	 * PLAN TO REBUILD ALL RAM TRACKING:
	 *
	 * PLAN to set block_in_use:
	 *
	 * Check sector 0 and 16 for GC State Machine, heres how it works:
	 * Block 0 to Block 1 Software GC Transfer
	 * STATES:
	 * 1. DURING GENERAL USE; NO GC --> B0 in use: 0xFC | B1 not in use: 0xFF ----> Source of truth: B0
	 * 2. RIGHT BEFORE BEGINNING GC TRANSFER --> B0 starting GC software transfer: 0xFB | B1 copy target: 0xFE ----> Source of truth: B0
	 * 3. RIGHT AFTER GC TRANSFER COMPLETES, BEFORE HARDWARE ERASE --> B0 completed transfer, now OBSOLETE: 0xFA | B1 Valid Memory: 0xFD ----> Source of truth: B1
	 * NOW ISSUE HARDWARE BLOCK ERASE ON BLOCK 0
	 * 4. Final state has already been reached, right before the hardware erase B0: 0xFA and B1: 0xFD
	 * 	  which means we know power went mid hardware erase, we know B1 is the source of truth, just
	 * 	  need to issue another hardware erase on the non-valid block B0.
	 *
	 * PLAN to build L2P Table:
	 *
	 * Go through the 16 physical sectors of the block that we now know is correct/valid
	 * Read each sector's logical page number owner, and in L2P array, if just 0xFF
	 * then don't do anything, if it has a a number 0 - 8 for the logical sectors
	 * then take [that number], so L2P[that number] = index of physical sector your on.
	 * Later on in traversal if you have to overwrite an L2P value, do it, because when
	 * handing out physical sectors we only ever do it going forward 0->15 of 16->31.
	 * This means that the largest physical sector index is the one that is
	 * valid / telling the truth about who its logical owner is.
	 */

	// TODO: Actually handle the power-off recovery during GC, not just identifying
	// the correct block

	FTL_Init();

	// Got the block number (0 or 1 for now)
	block_in_use = identify_block_in_use();


	// Build L2P


}
bool FTL_Write_Sector(uint16_t logical_sector, const uint8_t *payload_buf);
bool FTL_Read_Sector(uint16_t logical_sector, uint8_t *incoming_payload_buff);
void FTL_GarbageCollect(void);
