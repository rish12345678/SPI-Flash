#include "flash.h"
#include "ftl.h"

#include <string.h>

// Global file scope usage variables and data structures
// Holds 0 or 1 based on which block is currently holding the data, use this to see which parts of array to look at 0-15 or 16-31 idxs
static int block_in_use = 0;
// Hold 32 physical sectors at one time TOTAL, even though 16 are not in use at all at a time
static PhysicalSectorMetadata_t FTL_Phys_Page_Meta_Arr[FTL_SECTORS_PER_BLOCK]; // 16 physical sectors at once
// Allow use access to 8 logical sectors at one time (over provisioning)
// Each index is each of the eight logical sectors, and the corresponding value is the physical sector
static uint16_t L2P[FTL_LOGICAL_SECTORS]; // 8


static uint16_t next_clean_sector_idx = 0;


// Holds the next fully blank page per sector.  If we have written up to half of
// page four in physical sector eight, then first_free_page_table[8] = 5
static uint16_t first_free_page_table[FTL_SECTORS_PER_BLOCK]; // 16 physical sector, each has a page



// Helper Functions
static int block_sector_page_offset_to_adr(int block, int sector, int page, int offset) {
	return (block * FTL_BLOCK_SIZE) + (sector * FTL_SECTOR_SIZE) + (page * FTL_PAGE_SIZE) + offset;
}

static uint8_t identify_block_in_use(void) {
	// Read in GC State Machine byte for both starting sectors of each section(currently in use and not)
	uint32_t region_0_gc_state_machine_adr = block_sector_page_offset_to_adr(FTL_REGION_0, 0, 0, FTL_Flash_Logical_Page_Meta);
	uint32_t region_1_gc_state_machine_adr = block_sector_page_offset_to_adr(FTL_REGION_1, 0, 0, FTL_Flash_Logical_Page_Meta);
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

static void build_L2P_and_Phys_Meta(void) {
	/*
	 * Grab each sector's metadata in the correct block, and do the following:
	 *
	 * if that number is 0xFF, ignore it
	 *
	 * Before you just throw the value in L2P, check if that L2P index has a value:
	 *  - if the value is 0xFF, that means its clean, do this:
	 *  		- for this exact index in physMeta table, set Logical Sector # to 0xFF and state to CLEAN /ERASED
	 *  		- dont touch L2P Table, isn't mapped yet
	 * 	-if the [value] is a number 0 - 8 (only eight logical sectors):
	 * 			- then check what the L2P[value] is:
	 * 				- if L2P[value] = 0xFF, do L2P[value] = current idx in phys sector traversal | set PhysMeta[currIDX] = logpag# = currsectorMetaValue & page state = VALID
	 * 				- if L2P[value] != 0xFF: // this logical sector already has a prev, now stale phys mapping that is no longer valid
	 * 					- current L2P[value] is stale -> L2P[value] = stalePhysSector, go to PhysMeta[stalePhysSector] and set that state to STALE, logical page # doesn't matter anymore
	 *
	 * 	At the end of all of these checks, do THE CLASSIC ONLY IF the sector metadata value is in the range of logical sectors(0 - 8).
	 * 	Obviously, you can't do L2P[1000], when L2P has eight elements.
	 * 	THE CLASSIC -> L2P[that number] = index of physical sector your on.
	 */

	// TODO: Where do we assign FLASH metadata at setup?
	// TODO: How do we ensure that we loop through the correct block


	bool nxt_cln_sector_set = false;
	// for (uint16_t sec_metadata : In_use_block)
	for (int curr_Flash_Sector = 0; curr_Flash_Sector < FTL_SECTORS_PER_BLOCK; curr_Flash_Sector++) {
		uint32_t adr = block_sector_page_offset_to_adr(block_in_use, curr_Flash_Sector, 0, 0);

		uint16_t sector_metadata = 0xFFFF;
		Flash_Read_Data(adr, (uint8_t*)&sector_metadata, sizeof(uint16_t));

		// sector_metadata now holds the sector metadata


		// This means that this sector isn't even getting its metadata tracked
		// This just means the metadata is in an ERASED part of memory, haven't gotten there yet.
		if (sector_metadata == FTL_UNMAPPED) {
			// This sector is completely clean / erased, there is no L2P mapping to this
			FTL_Phys_Page_Meta_Arr[curr_Flash_Sector].logical_sector_owner = 0xFF; // Default: No L2P mapping
			FTL_Phys_Page_Meta_Arr[curr_Flash_Sector].state = FREE_SECTOR; // Default: This is all clean
			if (!nxt_cln_sector_set) {
				next_clean_sector_idx = curr_Flash_Sector;
				nxt_cln_sector_set = true;
			}
			continue; // don't even try to index 0xFF in L2P, max is idx = 7
		}

		// Do the checks before throwing into L2P
		uint16_t L2P_Val = L2P[sector_metadata];
		if (L2P_Val == FTL_UNMAPPED) {
			// This means this is the first L2P is seeing of this physical sector
			// This is a valid mapping, just throw it in there
			FTL_Phys_Page_Meta_Arr[curr_Flash_Sector].state = VALID_SECTOR;
			FTL_Phys_Page_Meta_Arr[curr_Flash_Sector].logical_sector_owner = sector_metadata;
		} else {
			FTL_Phys_Page_Meta_Arr[L2P_Val].state = DIRTY_SECTOR;
			// Logical owner doesn't matter now, its dirty


			FTL_Phys_Page_Meta_Arr[curr_Flash_Sector].state = VALID_SECTOR;
			FTL_Phys_Page_Meta_Arr[curr_Flash_Sector].logical_sector_owner = sector_metadata;
			// Logical owner doesn't matter anymore
		}
		// Both if and else should execute this after updating data structures
		L2P[sector_metadata] = curr_Flash_Sector;
	}

	if (!nxt_cln_sector_set) next_clean_sector_idx = FTL_SECTORS_PER_BLOCK;
}

void build_first_free_page_table(void) {
	// TODO: Look into using a magic number
	/*
	 * Scan through the sectors of current in use block - i
	 *
	 * For each sector, scan through every page, and read the first two bytes only - j
	 *
	 * if the two bytes we get are 0xFFFF, then that means this is a clean page
	 *  - Set arr[i] = j and bounce out of the inner loop
	 */

	for (int sector = 0; sector < FTL_SECTORS_PER_BLOCK; sector++) {
		for (int page = 0; page < FTL_PAGES_PER_SECTOR; page++) {
			uint32_t adr = block_sector_page_offset_to_adr(block_in_use, sector, page, 0);
			uint16_t sector_metadata = 0xFFFF;
			// grab two bytes
			Flash_Read_Data(adr, (uint8_t*)&sector_metadata, sizeof(uint16_t));

			if (sector_metadata == 0xFFFF) {
				// This is the first clean page, set this value
				first_free_page_table[sector] = page;
				break;
			}

		}
		// Essentially if using one block per region, is it
		if (first_free_page_table[sector] == 0xFFFF) {
			// This sector never found a clean page, set to 16, indicating we went passed page
			// limit and never found a clean page, this sector is completely used up
			first_free_page_table[sector] = FTL_PAGES_PER_SECTOR;
		} // ELSE: We found a free page in this sector, we are good to go, we hit prev break
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
		// Set default 0xFFFF
		L2P[i] = FTL_UNMAPPED; // Default in RAM mapping value, means not mapped to phys
	}

	// Initialize physical sector metadata
	for (int i = 0; i < FTL_SECTORS_PER_BLOCK; i++) {
		FTL_Phys_Page_Meta_Arr[i].logical_sector_owner = FTL_UNMAPPED;
		FTL_Phys_Page_Meta_Arr[i].state = FREE_SECTOR;
	}

	for (int i = 0; i < FTL_SECTORS_PER_BLOCK; i++) {
		first_free_page_table[i] = 0xFFFF;
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


	// Build L2P and FTL_Phys_Page_Meta_Arr
	build_L2P_and_Phys_Meta();

	/*
	 * TODO:
	 * By this point at least make sure block 0 / 1, sector 0, page 0, offset zero in
	 * Flash meta data is written otherwise it will mess up the following function
	 */

	// Build first_free_page_table
	build_first_free_page_table();

}
/*
 * TODO: PAYLOAD MAX: 253 bytes, Make it more bytes
 */
bool FTL_Write_Sector(uint16_t logical_sector, const uint8_t *payload_buf, int payload_len) {
	// If L2P[logical_sector] != 0xFF, do the setting stale thing in physmeta for L2P[logical_sector] (the old value), (either way
	// update L2P with new nextcleansectoridx, set PhysMeta[nextcleansectoridx] to Valid and logic sector = logical_sector, increment next_clean_sector_idx)
	//

	// Also, keep checking next_clean_sector_idx for FTL_URGENT_GC_NEEDED

	// Always write to two bytes in to the start of a page of teh scetor, unless phsy sector
	// 0 or 16, in those cases write three bytes in

	// For now, only do 253 byte writes at a time, handle sector, then block size later

	/*
	 * ORDER:
	 *
	 * Check if we even can do the requested write, return if not
	 *
	 * Write to Flash Metadata, followed by Flash Payload
	 *
	 * Then write the RAM metadata
	 *
	 * This way RAM isn't an ill-reflection of Flash -> if power goes we set up RAM based on accurate Flash
	 * And then also if power goes mid Flash payload write, Flash metadata tells us not to tamper
	 */

	// For now only take 253 bytes
	if (payload_len > FTL_PAGE_SIZE - 3) return false;
	if (logical_sector > FTL_LOGICAL_SECTORS - 1) return false;
	if (payload_buf == 0) return false;



	// First do the write

	// If we are at first physical sector in our region, skip three bytes
	uint8_t meta_skip = 0;

	if (next_clean_sector_idx == 0) {
		meta_skip = 3;
		int write_adr = block_sector_page_offset_to_adr(block_in_use, next_clean_sector_idx, 0, 0);
		uint8_t meta_payload_buff[meta_skip];

		*(uint16_t*) meta_payload_buff = logical_sector;
		*(meta_payload_buff + 2) = GC_META_VALID_BLOCK;

		Flash_Page_Program(write_adr, (uint8_t*)meta_payload_buff, meta_skip);
	} else {
		meta_skip = 2;
		int write_adr = block_sector_page_offset_to_adr(block_in_use, next_clean_sector_idx, 0, 0);
		uint8_t meta_payload_buff[meta_skip];

		*(uint16_t*) meta_payload_buff = logical_sector;
		Flash_Page_Program(write_adr, (uint8_t*)meta_payload_buff, meta_skip);
	}

	int write_adr = block_sector_page_offset_to_adr(block_in_use, next_clean_sector_idx, 0, meta_skip);
	Flash_Page_Program(write_adr, (uint8_t*)payload_buf, payload_len);

	// Update first_free_page_table, we just wrote to page zero of next_clean_sector_idx, so
	// the next clean page in that sector is incremented to 1.
	first_free_page_table[next_clean_sector_idx] = 1;


	// Do RAM data structure updates after confirmed Flash write
	if (L2P[logical_sector] != FTL_UNMAPPED) {
		// This logical sector is not unused, there was already a prev phys mapping to this
		// Set the physMeta Table for this old sector as stale, keep logical owner the same for magic num, doesn't matter tho
		FTL_Phys_Page_Meta_Arr[L2P[logical_sector]].state = DIRTY_SECTOR;
	}
	L2P[logical_sector] = next_clean_sector_idx;
	FTL_Phys_Page_Meta_Arr[next_clean_sector_idx].logical_sector_owner = logical_sector;
	FTL_Phys_Page_Meta_Arr[next_clean_sector_idx].state = VALID_SECTOR;




	next_clean_sector_idx++;
	// If our next clean sector is close to the end of physical sectors in one block, jump to
	// the other
	if (next_clean_sector_idx >= FTL_URGENT_GC_NEEDED) {
		FTL_GarbageCollect(); // TODO: In the meantime save writes to RAM BUFFER
	}
	return true;
}

bool FTL_Append_Sector(uint16_t logical_sector, const uint8_t *payload_buf, int payload_len) {
	/*
	 * Do same initial check as FTL_Write_Sector
	 *
	 * Now, if L2P[logical_sector] is 0xFFFF, unmapped, then just call Write Sector
	 *  - This handles accidental new / clean sector attempts to append, so this will take care of setting up the new sector and writing block and/or sector level metadata in RAM + Flash
	 *  - Increment of the first_free_page[physSectorIDX] value, taken care of in called function
	 *  - Leave the function immediately, the rest is only for mid sector, new page writes
	 *
	 *
	 * Otherwise, L2P[logical_sector] = physSectorIDX
	 *
	 * look at first_free_page[physSectorIDX] = first_free_page
	 *
	 * if (first_free_page > 15) this whole sector is full of written pages, call write sector to get a new physical sector for this logical sector
	 *
	 * else ie. first_free_page <= 15 --> if starting with 0xFFFF write page meta(logical sector owner), followed by 254 bytes of payload and then first_free_page[physSectorIDX]++
	 */
}

bool FTL_Read_Sector(uint16_t logical_sector, uint8_t *incoming_payload_buff, int payload_len) {

	return true;
}

void FTL_GarbageCollect(void) {

}
