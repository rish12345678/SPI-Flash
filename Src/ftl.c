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

// Row is sector num, column is page num, each index pair holds value of number of bytes written in that page
static uint8_t page_payload_len_table[FTL_SECTORS_PER_BLOCK * NUM_BLOCKS_PER_REGION][FTL_PAGES_PER_SECTOR];

// Helper Functions
static int block_sector_page_offset_to_adr(int block, int sector, int page, int offset)
{
    return (block * FTL_BLOCK_SIZE) + (sector * FTL_SECTOR_SIZE) + (page * FTL_PAGE_SIZE) + offset;
}

//static uint8_t identify_block_in_use(void)
//{
//    // Read in GC State Machine byte for both starting sectors of each section(currently in use and not)
//    uint32_t region_0_gc_state_machine_adr = block_sector_page_offset_to_adr(FTL_REGION_0, 0, 0, FTL_Flash_Logical_Page_Meta);
//    uint32_t region_1_gc_state_machine_adr = block_sector_page_offset_to_adr(FTL_REGION_1, 0, 0, FTL_Flash_Logical_Page_Meta);
//    uint8_t region_0_gc_meta_buffer;
//    uint8_t region_1_gc_meta_buffer;
//
//    Flash_Read_Data(region_0_gc_state_machine_adr, &region_0_gc_meta_buffer, FTL_Flash_GC_State_Meta);
//    Flash_Read_Data(region_1_gc_state_machine_adr, &region_1_gc_meta_buffer, FTL_Flash_GC_State_Meta);
//
//    if (region_0_gc_meta_buffer == GC_META_VALID_BLOCK)
//    {
//        // Power went during general (non-gc) use of block 0
//        return FTL_REGION_0;
//    }
//    else if (region_1_gc_meta_buffer == GC_META_VALID_BLOCK)
//    {
//        // Power went during general (non-gc) use of block 1
//        return FTL_REGION_1;
//    }
//    else if (region_0_gc_meta_buffer == GC_META_TRANSFERING_OUT_BLOCK)
//    {
//        // Power went out mid transfer from block 0 to block 1, block 0 is still the source of truth
//        return FTL_REGION_0;
//    }
//    else if (region_1_gc_meta_buffer == GC_META_TRANSFERING_OUT_BLOCK)
//    {
//        // Power went out mid transfer from block 1 to block 0, block 1 is still the source of truth
//        return FTL_REGION_1;
//    }
//    else if (region_0_gc_meta_buffer == GC_META_OBSOLETE_BLOCK)
//    {
//        // Power went out mid hardware erase of block 0, block 1 is now the source of truth
//        // TODO: In this case, power went our mid block 0 hardware erase, re-issue
//        return FTL_REGION_1;
//    }
//    else if (region_1_gc_meta_buffer == GC_META_OBSOLETE_BLOCK)
//    {
//        // Power went out mid hardware erase of block 1, block 0 is now the source of truth
//        // TODO: In this case, power went our mid block 1 hardware erase, re-issue
//        return FTL_REGION_0;
//    }
//    else if (region_1_gc_meta_buffer == GC_META_ERASED_BLOCK)
//    {
//    	return FTL_REGION_0;
//    }
//    else if (region_0_gc_meta_buffer == GC_META_ERASED_BLOCK)
//	{
//		return FTL_REGION_1;
//	}
//    else
//    {
//        // If both GC state machines show that they are "not in use", that means this is a fresh run
//        // power did not go out, do not change anything
//        return FTL_REGION_0;
//    }
//}

static void Set_GC_State_Machine(int region, uint8_t state) {
	int write_GC_adr = block_sector_page_offset_to_adr(region, 0, 0, 2);
	// Currently at 0xFC or 0 b 1111 1100
	// Set to 0xF8 or 0 b 1111 1000 -> Write 0xF8

	uint8_t payload_buff_GC = state;

	Flash_Page_Program(write_GC_adr, &payload_buff_GC, sizeof(uint8_t));
}

static void build_L2P_and_Phys_Meta(void)
{
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
    for (int curr_Flash_Sector = 0; curr_Flash_Sector < FTL_SECTORS_PER_BLOCK; curr_Flash_Sector++)
    {
        uint32_t adr = block_sector_page_offset_to_adr(block_in_use, curr_Flash_Sector, 0, 0);

        uint16_t sector_metadata = 0xFFFF;
        Flash_Read_Data(adr, (uint8_t *)&sector_metadata, sizeof(uint16_t));

        // sector_metadata now holds the sector metadata

        // This means that this sector isn't even getting its metadata tracked
        // This just means the metadata is in an ERASED part of memory, haven't gotten there yet.
        if (sector_metadata == FTL_UNMAPPED)
        {
            // This sector is completely clean / erased, there is no L2P mapping to this
            FTL_Phys_Page_Meta_Arr[curr_Flash_Sector].logical_sector_owner = 0xFF; // Default: No L2P mapping
            FTL_Phys_Page_Meta_Arr[curr_Flash_Sector].state = FREE_SECTOR;         // Default: This is all clean
            if (!nxt_cln_sector_set)
            {
                next_clean_sector_idx = curr_Flash_Sector;
                nxt_cln_sector_set = true;
            }
            continue; // don't even try to index 0xFF in L2P, max is idx = 7
        }

        // Do the checks before throwing into L2P
        uint16_t L2P_Val = L2P[sector_metadata];
        if (L2P_Val == FTL_UNMAPPED)
        {
            // This means this is the first L2P is seeing of this physical sector
            // This is a valid mapping, just throw it in there
            FTL_Phys_Page_Meta_Arr[curr_Flash_Sector].state = VALID_SECTOR;
            FTL_Phys_Page_Meta_Arr[curr_Flash_Sector].logical_sector_owner = sector_metadata;
        }
        else
        {
            FTL_Phys_Page_Meta_Arr[L2P_Val].state = DIRTY_SECTOR;
            // Logical owner doesn't matter now, its dirty

            FTL_Phys_Page_Meta_Arr[curr_Flash_Sector].state = VALID_SECTOR;
            FTL_Phys_Page_Meta_Arr[curr_Flash_Sector].logical_sector_owner = sector_metadata;
            // Logical owner doesn't matter anymore
        }
        // Both if and else should execute this after updating data structures
        L2P[sector_metadata] = curr_Flash_Sector;
    }

    if (!nxt_cln_sector_set)
        next_clean_sector_idx = FTL_SECTORS_PER_BLOCK;
}


static void mid_GC_powerloss_reboot(void) {
	uint8_t block_zero_GC_state = 0xFF;
	uint8_t block_one_GC_state = 0xFF;

	uint32_t block_zero_adr = block_sector_page_offset_to_adr(0, 0, 0, 2);
	uint32_t block_one_adr = block_sector_page_offset_to_adr(1, 0, 0, 2);

	Flash_Read_Data(block_zero_adr, &block_zero_GC_state, 1);
	Flash_Read_Data(block_one_adr, &block_one_GC_state, 1);

	// Mid Transfer or erase power-loss recovery Re-Issues


	// Case Zero: Completely Blank Regions - Clean Start
	if (block_zero_GC_state == GC_META_ERASED_BLOCK && block_one_GC_state == GC_META_ERASED_BLOCK) {
		Set_GC_State_Machine(0, GC_META_VALID_BLOCK);
		block_in_use = 0;
		return;
	}

	// Case One: Power cut during HW erase of block / region 0
	if (block_zero_GC_state == GC_META_OBSOLETE_BLOCK && block_one_GC_state == GC_META_VALID_BLOCK) {
		Flash_Erase_Block(block_sector_page_offset_to_adr(0, 0, 0, 0)); // Finish erasing Block 0
		block_in_use = 1;
	}
	// Inverse case of prev
	else if (block_one_GC_state == GC_META_OBSOLETE_BLOCK && block_zero_GC_state == GC_META_VALID_BLOCK) {
		Flash_Erase_Block(block_sector_page_offset_to_adr(1, 0, 0, 0)); // Finish erasing Block 1
		block_in_use = 0;
	}
	// Case Two: Power cut after transfer finished before old block marked obsolete / HW erase began
	else if (block_zero_GC_state == GC_META_TRANSFERING_OUT_BLOCK && block_one_GC_state == GC_META_VALID_BLOCK) {
		Set_GC_State_Machine(0, GC_META_OBSOLETE_BLOCK);
		Flash_Erase_Block(block_sector_page_offset_to_adr(0, 0, 0, 0));
		block_in_use = 1;
	}
	else if (block_one_GC_state == GC_META_TRANSFERING_OUT_BLOCK && block_zero_GC_state == GC_META_VALID_BLOCK) {
		Set_GC_State_Machine(1, GC_META_OBSOLETE_BLOCK);
		Flash_Erase_Block(block_sector_page_offset_to_adr(1, 0, 0, 0));
		block_in_use = 0;
	}
	// Case Three: Power cut mid transfer | Target block is blank or corrupted, so wipe it, just
	// rebuild the RAM data structures from the orig block and let the GC Transfer software
	// run naturally on the next write
	else if (block_zero_GC_state == GC_META_TRANSFERING_OUT_BLOCK) {
		// Target Block 1 was incomplete. Erase Block 1 and mount Block 0.
		Flash_Erase_Block(block_sector_page_offset_to_adr(1, 0, 0, 0));
		block_in_use = 0;
	}
	// Inverse case of prev
	else if (block_one_GC_state == GC_META_TRANSFERING_OUT_BLOCK) {
		Flash_Erase_Block(block_sector_page_offset_to_adr(0, 0, 0, 0));
		block_in_use = 1;
	}
	// Case Four: Standard boot up, power-loss occurred amidst no part of FTL_GarbageCollect
	else if (block_zero_GC_state == GC_META_VALID_BLOCK) {
		block_in_use = 0;
	}
	// Inverse case of prev
	else if (block_one_GC_state == GC_META_VALID_BLOCK) {
		block_in_use = 1;
	}
}

//static void set_reset_meta(void) {
//	// Block erase both blocks
//	uint32_t b0_adr = block_sector_page_offset_to_adr(0, 0, 0, 0);
//	uint32_t b1_adr = block_sector_page_offset_to_adr(1, 0, 0, 0);
//
//	Flash_Erase_Block(b0_adr);
//	Flash_Erase_Block(b1_adr);
//}



// Non-Helper Functions

void build_first_free_page_table_and_page_payload_len_table(void)
{
    // TODO: Look into using a magic number
    /*
     * Scan through the sectors of current in use block - i
     *
     * For each sector, scan through every page, and read the first two bytes only - j
     *
     * if the two bytes we get are 0xFFFF, then that means this is a clean page
     *  - Set arr[i] = j and bounce out of the inner loop
     */

    for (int sector = 0; sector < FTL_SECTORS_PER_BLOCK; sector++)
    {
        for (int page = 0; page < FTL_PAGES_PER_SECTOR; page++)
        {
            uint32_t adr = block_sector_page_offset_to_adr(block_in_use, sector, page, 0);
            uint32_t sector_metadata = 0xFFFFFFFF;
            // grab two bytes
            Flash_Read_Data(adr, (uint8_t *)&sector_metadata, sizeof(sector_metadata));

            if (sector_metadata == 0xFFFFFFFF)
            {
                // This is the first clean page, set this value
                first_free_page_table[sector] = page;
                break;
            }
            else
            {
                // in this page per sector loop, if meta not empty, page has been written to, read num bytes in page and set in page_payload_len_table
                page_payload_len_table[sector][page] = (sector_metadata >> 24);
            }
        }
        // Essentially if using one block per region, is it
        if (first_free_page_table[sector] == FTL_UNMAPPED)
        {
            // This sector never found a clean page, set to 16, indicating we went passed page
            // limit and never found a clean page, this sector is completely used up
            first_free_page_table[sector] = FTL_PAGES_PER_SECTOR;
        } // ELSE: We found a free page in this sector, we are good to go, we hit prev break
    }
}

static void FTL_Init(void)
{

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
    for (int i = 0; i < FTL_LOGICAL_SECTORS; i++)
    {
        // Set default 0xFFFF
        L2P[i] = FTL_UNMAPPED; // Default in RAM mapping value, means not mapped to phys
    }

    // Initialize physical sector metadata
    for (int i = 0; i < FTL_SECTORS_PER_BLOCK; i++)
    {
        FTL_Phys_Page_Meta_Arr[i].logical_sector_owner = FTL_UNMAPPED;
        FTL_Phys_Page_Meta_Arr[i].state = FREE_SECTOR;
    }

    for (int i = 0; i < FTL_SECTORS_PER_BLOCK; i++)
    {
        first_free_page_table[i] = FTL_UNMAPPED;
    }

    for (int i = 0; i < FTL_SECTORS_PER_BLOCK * NUM_BLOCKS_PER_REGION; i++)
    {
        for (int j = 0; j < FTL_PAGES_PER_SECTOR; j++)
        {
            page_payload_len_table[i][j] = 0; // Zero bytes have been written to this page
        }
    }

    block_in_use = 0;
    next_clean_sector_idx = 0; // TODO: Set this to either 0 or 16 based on which block is currently in use
}

void FTL_Mount(void)
{
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

#ifdef FORCE_FLASH_RESET
	set_reset_meta();
#endif


    // Re-issue Hardware erases if necessary (Rebooting after power-loss MID-GC function)
    mid_GC_powerloss_reboot();

    // Build L2P and FTL_Phys_Page_Meta_Arr
    build_L2P_and_Phys_Meta();

    /*
     * TODO:
     * By this point at least make sure block 0 / 1, sector 0, page 0, offset zero in
     * Flash meta data is written otherwise it will mess up the following function
     */

    // Build first_free_page_table and page_payload_len_table
    build_first_free_page_table_and_page_payload_len_table();
}
/*
 * TODO: PAYLOAD MAX: 252 bytes, Make it more bytes
 */
bool FTL_Write_Sector(uint16_t logical_sector, const uint8_t *payload_buf, int payload_len)
{
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

    // For now only take 252 bytes
    if (payload_len > FTL_USABLE_BYTES_PER_PAGE)
        return false;
    if (logical_sector > FTL_LOGICAL_SECTORS - 1)
        return false;
    if (payload_buf == 0)
        return false;

    // First do the write

    // If we are at first physical sector in our region, skip three bytes

    int write_adr = block_sector_page_offset_to_adr(block_in_use, next_clean_sector_idx, 0, 0);
    uint8_t meta_payload_buff[FTL_METADATA_PER_PAGE] = CLEAN_META;
    // Every start of sector write gets logical sector metadata
    *(uint16_t *)meta_payload_buff = logical_sector;
    // Only start of region / in this case block gets GC STATE as well
    if (next_clean_sector_idx == 0)
        *(meta_payload_buff + 2) = GC_META_VALID_BLOCK;
    // Lastly, since we are writing to the start of a page always, throw in the page metadata; the const payload length
    *(meta_payload_buff + 3) = payload_len;
    Flash_Page_Program(write_adr, (uint8_t *)meta_payload_buff, FTL_METADATA_PER_PAGE);

    int write_adr2 = block_sector_page_offset_to_adr(block_in_use, next_clean_sector_idx, 0, FTL_METADATA_PER_PAGE);
    Flash_Page_Program(write_adr2, (uint8_t *)payload_buf, payload_len);

    // Update first_free_page_table, we just wrote to page zero of next_clean_sector_idx, so
    // the next clean page in that sector is incremented to 1.
    first_free_page_table[next_clean_sector_idx] = 1;

    // Do RAM data structure updates after confirmed Flash write
    if (L2P[logical_sector] != FTL_UNMAPPED)
    {
        // This logical sector is not unused, there was already a prev phys mapping to this
        // Set the physMeta Table for this old sector as stale, keep logical owner the same for magic num, doesn't matter tho
        FTL_Phys_Page_Meta_Arr[L2P[logical_sector]].state = DIRTY_SECTOR;
    }
    L2P[logical_sector] = next_clean_sector_idx;
    FTL_Phys_Page_Meta_Arr[next_clean_sector_idx].logical_sector_owner = logical_sector;
    FTL_Phys_Page_Meta_Arr[next_clean_sector_idx].state = VALID_SECTOR;

    // Update page_payload_len_table in RAM
    page_payload_len_table[next_clean_sector_idx][0] = payload_len;

    next_clean_sector_idx++;
    // If our next clean sector is close to the end of physical sectors in one block, jump to
    // the other
    if (next_clean_sector_idx >= FTL_URGENT_GC_NEEDED)
    {
        FTL_GarbageCollect(); // TODO: In the meantime save writes to RAM BUFFER
    }
    return true;
}

// Writes a new page if possible
bool FTL_Append_Sector(uint16_t logical_sector, const uint8_t *payload_buf, int payload_len)
{
    /*
     * Do same initial check as FTL_Write_Sector
     *
     * Now, if L2P[logical_sector] is 0xFFFF, unmapped, then just call Write Sector
     *  - This handles accidental new / clean sector attempts to append, so this will take care of setting up the new sector and writing block and/or sector level metadata in RAM + Flash
     *  - Increment of the first_free_page[physSectorIDX] value, taken care of in called function, DON'T DO HERE!
     *  - Leave the function immediately, the rest is only for mid sector, new page writes
     *
     *
     * Otherwise, L2P[logical_sector] = physSectorIDX
     *
     * look at first_free_page[physSectorIDX] = first_free_page
     *
     * if (first_free_page > 15) this whole sector is full of written pages, call write sector to get a new physical sector for this logical sector, that will mark this sector stale, and all other bookkeeping automatically
     *
     * else ie. first_free_page <= 15 --> if starting with 0xFFFF write page meta(logical sector owner), followed by 252 bytes of payload and then first_free_page[physSectorIDX]++
     */
    if (payload_len > FTL_USABLE_BYTES_PER_PAGE)
        return false;
    if (logical_sector > FTL_LOGICAL_SECTORS - 1)
        return false;
    if (payload_buf == 0)
        return false;

    if (L2P[logical_sector] == FTL_UNMAPPED)
    {
        // Returns the same bool as Write_Sector(ie, if that returns a fail, so does this, vice versa)
        // At the same time bounces out of function, not-executing the rest of this for mid-sector writes
        return FTL_Write_Sector(logical_sector, payload_buf, payload_len);
    }

    uint16_t physSectorIDX = L2P[logical_sector];
    uint16_t first_free_page = first_free_page_table[physSectorIDX];
    if (first_free_page > 15)
    {
        // That means this whole physical sector we want to append to is full, just write their payload in a new physical sector, but passing in this logical one
        return FTL_Write_Sector(logical_sector, payload_buf, payload_len);
    }
    // else
    uint32_t write_adr = block_sector_page_offset_to_adr(block_in_use, physSectorIDX, first_free_page, 0);
    uint8_t page_payload[FTL_PAGE_SIZE]; // Fixed size array to populate one page
    //*(uint16_t*) page_payload = logical_sector; // DO NOT WRITE LOGICAL SECTOR # AT START OF EVERY PAGE, UNNECESSARY FOR IDENTIFYING CLEAN PAGES

    // Three bytes of padding metadata, followed by metadata for page pyld len
    *(uint16_t *)page_payload = 0xFFFF;
    *(page_payload + 2) = 0xFF;
    *(page_payload + 3) = payload_len;

    for (int i = 0; i < payload_len; i++)
    {
        *(page_payload + FTL_METADATA_PER_PAGE + i) = *(payload_buf + i);
    }
    int total_write_len = payload_len + FTL_METADATA_PER_PAGE; // payload + four bytes for the page metadata
    Flash_Page_Program(write_adr, (uint8_t *)page_payload, total_write_len);

    // Increment next free page in this sector and then return
    first_free_page_table[physSectorIDX]++;
    page_payload_len_table[physSectorIDX][first_free_page] = payload_len;
    return true;
}

int read_page(uint8_t physical_sector, uint8_t *buf, uint8_t curr_page, uint16_t bytes_left_to_read)
{
    // If read a partial page, return false, else true

    /*
     * if num bytes in this page < bytes_left
     *  -> do a page read start at start of mem, len is page_bytes, ret page_bytes
     * else
     *  -> even is = do the read start at start of mem to bytes_left, ret bytes_left
     */

    if (curr_page > FTL_PAGES_PER_SECTOR - 1)
        return 0;
    if (bytes_left_to_read == 0)
        return 0;

    uint8_t bytes_in_curr_page = page_payload_len_table[physical_sector][curr_page];
    if (bytes_in_curr_page == 0)
        return 0;
    uint32_t adr = block_sector_page_offset_to_adr(block_in_use, physical_sector, curr_page, FTL_METADATA_PER_PAGE);
    if (bytes_in_curr_page < bytes_left_to_read)
    {
        Flash_Read_Data(adr, buf, bytes_in_curr_page);
        return bytes_in_curr_page;
    }
    Flash_Read_Data(adr, buf, bytes_left_to_read);
    return bytes_left_to_read;
}

// A return of zero means there was a critical failure
// No bytes were explicitly placed in incoming_payload_buff
uint16_t FTL_Read_Sector(uint16_t logical_sector, uint8_t *incoming_payload_buff, int sector_offset, int payload_len)
{
    /*
     * HIGH LEVEL:
     * User wants to read from some sector, and they want x bytes at a y offset of bytes
     *
     * Do basic checks first
     *
     * Go through page_payload_len_table working through the correct phys
     * sector and sum up the values until sum > offset
     *
     * That means at that page, got to the page that has the y'th byte
     *
     * We also know how many bytes in we need to go to get to that y'th byte
     *
     * Then continue the summing up with previous sum and see when we get to
     * x + y.  If we get to the last col and never get the sum = x + y, that
     * means there weren't enough bytes in that sector.  TODO: Return int
     * num_bytes_read, so the user knows and can just do a quick if returned
     * value == payload_len -> read successful, and then we can return
     * whatever sum - y is if we fall short, and then we can just return 0 for
     * the false returns at the start for faulty parameters.
     *
     * Essentially once we get to the offset value we now start reads from that
     * page and offset within the page, and then while !(end of sector or next
     * page payload_len_meta > sum) next page and read, until end, then do
     * the section of page read again as much as needed.
     */

    // Basic checks
    if (payload_len > FTL_SECTOR_SIZE - (FTL_METADATA_PER_PAGE * FTL_PAGES_PER_SECTOR))
        return 0;
    if (logical_sector > FTL_LOGICAL_SECTORS - 1)
        return 0;
    if (L2P[logical_sector] == FTL_UNMAPPED)
        return 0;
    if (incoming_payload_buff == 0)
        return 0;

    uint8_t physical_sector = L2P[logical_sector];

    // Quick check: Are we reading too far in this sector(not enough bytes for the offset) or are there no bytes in this sector, eiother way return immediately
    uint16_t total_sector_bytes = 0;
    for (int p = 0; p < FTL_PAGES_PER_SECTOR; p++)
    {
        total_sector_bytes += page_payload_len_table[physical_sector][p];
    }

    if (sector_offset >= total_sector_bytes || total_sector_bytes == 0)
        return 0;

    uint8_t curr_page = 0;
    uint16_t iter_bytes_sum = page_payload_len_table[physical_sector][curr_page];
    uint16_t bytes_left_to_read = payload_len;
    uint16_t total_bytes_read = 0;
    bool reading = false;

    while (iter_bytes_sum < sector_offset)
    {
        // curr_page about to overflow, offset is to large for bytes in sector
        if (curr_page >= FTL_PAGES_PER_SECTOR - 1)
            return 0;
        // Iterating while we haven't landed on readable page
        curr_page++;
        iter_bytes_sum += page_payload_len_table[physical_sector][curr_page];
    }
    // At this point we have reaches a page that exceeds the offset
    // To see where in this page to start reading from:

    reading = true;

    // For first page read

    uint8_t back = iter_bytes_sum - sector_offset;
    uint8_t page_offset = page_payload_len_table[physical_sector][curr_page] - back;
    if (bytes_left_to_read <= back)
    {
        // This page has too many bytes, we need to read only bytes_left_to_read
        uint32_t adr = block_sector_page_offset_to_adr(block_in_use, physical_sector, curr_page, FTL_METADATA_PER_PAGE + page_offset);
        // uint8_t buf[back];
        Flash_Read_Data(adr, incoming_payload_buff, bytes_left_to_read);
        // Return number of bytes read from first page.
        reading = false;
        return bytes_left_to_read;
    }
    else
    {
        uint32_t adr = block_sector_page_offset_to_adr(block_in_use, physical_sector, curr_page, FTL_METADATA_PER_PAGE + page_offset);
        // uint8_t num_bytes_to_read_in_page = page_payload_len_table[physical_sector][curr_page] - page_offset;
        // uint8_t buf[num_bytes_to_read_in_page];
        Flash_Read_Data(adr, incoming_payload_buff, back); // Read the rest of this page
        // // copy over this bytes read from this page to main return array
        // memcpy(incoming_payload_buff, buf, back);
        // Decrement bytes_left_to_read to keep track of progress
        bytes_left_to_read -= back;
        total_bytes_read += back;
        curr_page++;
        iter_bytes_sum += page_payload_len_table[physical_sector][curr_page]; // Technically no longer needed / tracked, since now in reading mode
    }

    // For reading all other pages
    while (reading && curr_page < FTL_PAGES_PER_SECTOR)
    {
        uint8_t transition_buf[FTL_PAGE_SIZE];
        int bytes_read = read_page(physical_sector, transition_buf, curr_page, bytes_left_to_read);
        if (bytes_read == 0)
        {
            reading = false;
            break;
        }
        memcpy(incoming_payload_buff + total_bytes_read, transition_buf, bytes_read);

        total_bytes_read += bytes_read;
        bytes_left_to_read -= bytes_read;
        curr_page++;
    }

    return total_bytes_read;
}

void FTL_GarbageCollect(void)
{
	/*
	 * High Level Double Buffer GC: Sequentially transfer
	 * over Valid Sectors maintaining RAM data
	 * structures, in-Flash metadata, and GC State
	 * Machine Metadata, and finally issue Block Erase on old
	 *
	 * currentregion = 0
	 * targetregion = 1
	 *
	 *
	 * Next_Writable_Sector_In_Target_Region = 0;
	 *
	 * HANDLE GC STATE MACHINE: Before starting software transfer
	 *  -> Currentregion GC = GC_META_TRANSFERING_OUT_BLOCK | Other stays erased 0xFF
	 *
	 * Loop through PhysMeta - for (i < PhysMeta.size())
	 *  - If PageState == VALID
	 *  	-> Read in how many every pages there are with actual writes in this sector to RAM (If physMeta idx == 0 -> Set GC State to 0xFF) and then write to targetRegion's sector : Next_Writable_Sector_In_Target_Region in Flash
	 *  	-> phys_meta[Next_Writable_Sector_In_Target_Region] = physMeta[i].logical_sector + State
	 *  	-> L2P[phsyMeta[i].logical_sector] = Next_Writable_Sector_In_Target_Region
	 *  	-> first_free_page_table[Next_Writable_Sector_In_Target_Region] = first_free_page_table[i]
	 *  	-> page_payload_len_table[Next_Writable_Sector_In_Target_Region] = page_payload_len_table[i] | Copy that old complete row to the new correct sector row
	 *  	** ALSO RUN CLEANUP ON REMAINING INDICES OF ALL DATA STRUCTURES NOT WRITTEN TO FOR NEW BLOCK **
	 *
	 * ** We do not need temporary data structures since we are sequencing through phys_meta sectors and placing them sequentially in the new block's sectors, so in iterating through phys_meta i always >= Next_Writable_Sector_In_Target_Region **
	 *
	 * Now sectors are places in new block, RAM data structures are setup properly, GC State machine still shows transferring, and Flash Metadata is correct.
	 *
	 * Now set currentblockinuse to the other block
	 * Also update GC State Machine of old block to GC_META_OBSOLETE_BLOCK, new block to GC_META_VALID_BLOCK
	 *
	 * This way if there is a power-loss in following hardware erase it is clear one block shows either GC_META_OBSOLETE_BLOCK or GC_META_ERASED_BLOCK and the new one shows GC_META_VALID_BLOCK
	 *
	 * Issue Hardware Erase on non-currentblockinuse
	 */

	int current_region = 0;
	int target_region = 0;
	if (block_in_use == 0) {
		current_region = 0;
		target_region = 1;
	} else {
		current_region = 1;
		target_region = 0;
	}
	uint8_t Next_Writable_Sector_In_Target_Region = 0;

	// Before starting the transfer --> Set GC State Machine for current_region transferring out
	Set_GC_State_Machine(current_region, GC_META_TRANSFERING_OUT_BLOCK);

	for (int curr_phys_sector_idx = 0; curr_phys_sector_idx < FTL_SECTORS_PER_BLOCK; curr_phys_sector_idx++) {
		PhysicalSectorMetadata_t Phys_Sector = FTL_Phys_Page_Meta_Arr[curr_phys_sector_idx];
		if (Phys_Sector.state == VALID_SECTOR) { // if this sector ought to be transferred
			// Check how many pages are written to in this sector
			uint8_t num_written_pages = first_free_page_table[curr_phys_sector_idx]; // If first free page is idx 2, 0 and 1 are written (ie. two pages are written)
			uint8_t logi_owner = Phys_Sector.logical_sector_owner;
			// Now read in that many pages - Page level transfer from old block sector to new block sector, for this one sector
			for (int i = 0; i < num_written_pages; i++) {
				// Read in page i - if curr_phys_sector_idx == 0 and i == 0, set GC State to 0xFF
				uint32_t read_adr_page_i = block_sector_page_offset_to_adr(current_region, curr_phys_sector_idx, i, 0);
				uint16_t user_bytes_in_this_page = page_payload_len_table[curr_phys_sector_idx][i];
				uint16_t num_bytes_to_read = user_bytes_in_this_page + FTL_METADATA_PER_PAGE;
				uint8_t buf[256] = {0};
				Flash_Read_Data(read_adr_page_i, buf, num_bytes_to_read);
				// buf now contains the exact metadata header + user bytes of this pager
				if (Next_Writable_Sector_In_Target_Region == 0 && i == 0) {
					// This is the initial sector, and page, make sure to set GC State before transfer
					*(buf + 2) = 0xFF;
				}
				// For rest of sectors' pages there is no metadata change (logical sector # and num_bytes stays the same)

				// Write back to same page, i, in sector Next_Writable_Sector_In_Target_Region of target_region

				uint32_t write_adr_page_i = block_sector_page_offset_to_adr(target_region, Next_Writable_Sector_In_Target_Region, i, 0);
				// Write back to correct sector and page as many bytes as we read in from current_region
				Flash_Page_Program(write_adr_page_i, buf, num_bytes_to_read);
			}
			FTL_Phys_Page_Meta_Arr[Next_Writable_Sector_In_Target_Region] = Phys_Sector;
			L2P[logi_owner] = Next_Writable_Sector_In_Target_Region;
			first_free_page_table[Next_Writable_Sector_In_Target_Region] = num_written_pages;
			// page_payload_len_table[Next_Writable_Sector_In_Target_Region] = page_payload_len_table[i] | Copy that old complete row to the new correct sector row
			uint8_t old_row = curr_phys_sector_idx;
			uint8_t new_row = Next_Writable_Sector_In_Target_Region;
			for (int page_iter = 0; page_iter < FTL_PAGES_PER_SECTOR; page_iter++) {
				page_payload_len_table[new_row][page_iter] = page_payload_len_table[old_row][page_iter];
			}
			// If we found a valid sector and filled in a new sector in the other block with the old blocks sector, increment to a new target sector to transfer to next
			Next_Writable_Sector_In_Target_Region++;
		}
	}
	// Fill in remaining for the RAM data structures, setting to default
	// Fill all RAM data structures from sector num_valid_sectors to sector 15 with default values - L2P is just a mapping, all defaults have stayed defaults
	for (int fill_defaults = Next_Writable_Sector_In_Target_Region; fill_defaults < FTL_SECTORS_PER_BLOCK; fill_defaults++) {
		FTL_Phys_Page_Meta_Arr[fill_defaults].logical_sector_owner = FTL_UNMAPPED;
		FTL_Phys_Page_Meta_Arr[fill_defaults].state = FREE_SECTOR;

		first_free_page_table[fill_defaults] = FTL_UNMAPPED;

		// Fill remaining sector rows with all unwritten pages, zero bytes written in all pages
		for (int page_iter = 0; page_iter < FTL_PAGES_PER_SECTOR; page_iter++) {
			page_payload_len_table[fill_defaults][page_iter] = 0;
		}
	}

	// TODO: Change power-loss boot up code to check GC State and re-issue whatever is needed

	// Hand over RAM switch for current block in use
	if (current_region == 0) {
		// flip regions
		current_region = 1;
		target_region = 0;
		block_in_use = 1;
	} else {
		// flip regions
		current_region = 0;
		target_region = 1;
		block_in_use = 0;
	}

	// Change Flash GC State Machine Metadata to reflect this prev RAM region control
	// update GC State Machine of old block to GC_META_OBSOLETE_BLOCK, new block to GC_META_VALID_BLOCK

	int old_block = target_region; // just for symmantic reasons
	Set_GC_State_Machine(old_block, GC_META_OBSOLETE_BLOCK); // Set the region we just transfered out of as obsolete before hardware erase of that old block
	Set_GC_State_Machine(current_region, GC_META_VALID_BLOCK); // Set the new block that we transfered into as the valid block as source of truth from now

	// From here after the erase, the obsolete block will get wiped to an erase block, either one means the other block is the source of truth

	// Issue Hardware Block Erase
	uint32_t hardware_block_erase_adr = block_sector_page_offset_to_adr(old_block, 0, 0, 0);
	Flash_Erase_Block(hardware_block_erase_adr);
}
