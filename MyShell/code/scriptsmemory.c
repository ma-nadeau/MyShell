#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "shellmemory.h"
#include "scriptsmemory.h"
#include "shell.h"

struct frameMetaData {
    struct scriptFrames *associatedScript;
    int associatedPageNumber;
    int LRU_idx;
};

char *shellmemoryCode[FRAME_STORE_SIZE];

struct frameMetaData framesMetadata[FRAME_NUMBER];

/*** FUNCTION SIGNATURES ***/

int virtualToPhysicalAddress(int instructionVirtualAddress, struct scriptFrames *scriptInfo);
void updateLRURanking(int frameMostRecentlyUsed);

/*** FUNCTIONS FOR SCRIPT MEMORY ***/

/**
 * This function intializes the memory for the scripts as well as
 * resources required for script memory management.
 * @param void
 * @return void
 */
void scripts_memory_init() {
    // Initialize variable and code shellmemory
    int mem_idx, frameIdx;
    for (mem_idx = 0; mem_idx < FRAME_STORE_SIZE; mem_idx++) {
        shellmemoryCode[mem_idx] = NULL;
    }

    // Initialize frames metadata
    // Note that the associated pageNumber doesn't need to be initialized
    // because it's value is only relevant if the associatedScript field
    // is non NULL
    for (frameIdx = 0; frameIdx < FRAME_NUMBER; frameIdx++) {
        framesMetadata[frameIdx].associatedScript = NULL;
        framesMetadata[frameIdx].LRU_idx = FRAME_NUMBER - frameIdx - 1;
    }
}

/**
 * Function that returns the instruction associated with a virtual address
 *
 * @param instructionVirtualAddress the user address at which to fetch the
 * instructions
 * @param scriptInfo the struct containing the page table needed to decode the
 * virtual address
 *
 * @return the instruction associated if the virtual address is valid, NULL
 * otherwise
 */
char *fetchInstructionVirtual(int instructionVirtualAddress, struct scriptFrames *scriptInfo) {
    int physicalAddress;
    char *rv;

    physicalAddress = virtualToPhysicalAddress(instructionVirtualAddress, scriptInfo);
    if (physicalAddress >= 0) {
        rv = shellmemoryCode[physicalAddress];
        updateLRURanking(physicalAddress / PAGE_SIZE);
    } else {
        rv = NULL;
    }

    return rv;
}

/**
 * Function that updates the memory slot associated with a virtual address
 * Note that this function assumes that the given instructionVirtualAddress is
 * always valid (i.e. the behaviour for an invalid virtual address is
 * undetermined). This is because the function is only used by the OS
 * internally.
 *
 * @param instructionVirtualAddress the user address at which to update the
 * instruction stored
 * @param scriptInfo the struct containing the page table needed to decode the
 * virtual address
 * @param newInstruction the new instruction to assign to the virtual address
 * 
 * @return void
 */
void updateInstructionVirtual(int instructionVirtualAddress,
                              struct scriptFrames *scriptInfo,
                              char newInstruction[]) {
    int physicalAddress;
    // Fetch translation of virtual address
    physicalAddress = virtualToPhysicalAddress(instructionVirtualAddress, scriptInfo);
    // Update the memory
    shellmemoryCode[physicalAddress] = newInstruction;
}

/**
 * Function that returns the LRU (Least Recently Used) frame to be replaced
 * @param void
 * @return the LRU frame index
 */
int findLRUFrame() {
    int frameIdx;

    // Loop over all the frames to find the LRU which
    // will always have a rank equal to FRAME_NUMBER - 1
    // in this implementation of LRU policy
    for (frameIdx = 0; frameIdx < FRAME_NUMBER; frameIdx++) {
        if (framesMetadata[frameIdx].LRU_idx == FRAME_NUMBER - 1) {
            return frameIdx;
        }
    }
}

/**
 * Function that declares the victime page in stdout and simultaneously frees
 * the lines occupied
 *
 * @param victimPage the page number of the victim page 
 * @param scriptInfo the struct containing the page table of the victim page
 * 
 * @return void
 */
void declareVictimePage(int victimePage, struct scriptFrames *scriptInfo) {
    int pageOffset;
    int virtualAddress;
    char *instruction;

    printf("Page fault! Victim page contents:\n\n");
    // Loop through the lines in frame to declare and free them
    for (pageOffset = 0; pageOffset < PAGE_SIZE; pageOffset++) {
        virtualAddress = victimePage * PAGE_SIZE + pageOffset;
        instruction = fetchInstructionVirtual(virtualAddress, scriptInfo);
        // There might not be an instruction as the frame is three instructions
        // wide but there might be only 1 or 2 instructions stored
        if (instruction) {
            printf("%s", instruction);
            free(instruction);
            // Set the line to NULL to avoid double freeing the same pointer
            updateInstructionVirtual(virtualAddress, scriptInfo, NULL);
        }
    }
    printf("\nEnd of victim page contents.\n");
}

/**
 * Function that assigns a page to a frame with respect to the LRU policy
 *
 * @param pageNumber new page to be stored in memory
 * @param scriptInfo struct containing page table fo the page to be stored
 * @param setup boolean that is True if the script is being allocated its first
 * 2 frames or False if it is a page fault
 * 
 * @return void
 */
void pageAssignment(int pageNumber, struct scriptFrames *scriptInfo, int setup) {
    FILE *p;
    int LRUFrame, pageIdx, pageOffsetIdx;
    char line[MAX_USER_INPUT];

    // First find the frame to use (LRU)
    LRUFrame = findLRUFrame();
    // The LRU becomes the most recently used because it will contain a new page
    updateLRURanking(LRUFrame);

    // If frame to use had a page then declare victim page and clean up
    if (framesMetadata[LRUFrame].associatedScript) {
        // Declare the victim and free the memory allocated lines
        declareVictimePage(framesMetadata[LRUFrame].associatedPageNumber,
                           framesMetadata[LRUFrame].associatedScript);

        // Invalidate page in page table
        framesMetadata[LRUFrame].associatedScript->pageTable[framesMetadata[LRUFrame].associatedPageNumber] = -1;

        framesMetadata[LRUFrame].associatedScript->FramesInUse--;
        // Special case where no frames or PCBs are using the script file
        // anymore free the memory allocated for the scriptInformation
        if (!framesMetadata[LRUFrame].associatedScript->PCBsInUse &&
            !framesMetadata[LRUFrame].associatedScript->FramesInUse) {
            free(framesMetadata[LRUFrame].associatedScript->scriptName);
            free(framesMetadata[LRUFrame].associatedScript);
            framesMetadata[LRUFrame].associatedScript = NULL;
        }
    } else if (!setup) {
        // Special case where there is a page fault but no pages are being
        // evicted
        printf("Page fault!\n");
    }

    // Renew metaData to the frame
    framesMetadata[LRUFrame].associatedScript = scriptInfo;
    framesMetadata[LRUFrame].associatedScript->FramesInUse++;
    framesMetadata[LRUFrame].associatedPageNumber = pageNumber;

    // Validate pageTable of newly allocated page
    framesMetadata[LRUFrame].associatedScript->pageTable[pageNumber] = LRUFrame;

    p = fopen(scriptInfo->scriptName, "rt");

    // Offset to current page
    for (pageIdx = 0; pageIdx < pageNumber; pageIdx++) {
        for (pageOffsetIdx = 0; pageOffsetIdx < PAGE_SIZE; pageOffsetIdx++) {
            fgets(line, MAX_USER_INPUT - 1, p);
        }
    }

    // Write into memory the new page
    for (pageOffsetIdx = 0; pageOffsetIdx < PAGE_SIZE; pageOffsetIdx++) {
        fgets(line, MAX_USER_INPUT - 1, p);
        updateInstructionVirtual(pageNumber * 3 + pageOffsetIdx, scriptInfo, strdup(line));
        memset(line, 0, sizeof(line));
        if (feof(p)) {
            break;
        }
    }
    fclose(p);
}

/**
 * Function that returns an associated page table struct
 * if the associated script has at least one frame in memory
 * This function is important for code sharing between process executing the
 * same script
 *
 * @param script path name of the script to find the page table of in memory
 *
 * @return scriptFrames page table struct of the script in question or NULL if the script is not in memory
 */
struct scriptFrames *findExistingScript(char script[]) {
    int frameIdx;
    struct scriptFrames *rv = NULL;

    // Check in every frame if the associatedScript information
    // matches with the script parameter
    for (frameIdx = 0; frameIdx < FRAME_NUMBER; frameIdx++) {
        if (framesMetadata[frameIdx].associatedScript &&
            strcmp(framesMetadata[frameIdx].associatedScript->scriptName, script) == 0) {
            rv = framesMetadata[frameIdx].associatedScript;
            break;
        }
    }

    return rv;
}

/*** HELPER FUNCTIONS */

/**
 * Function that translates virtual addresses to physical addresses given a page
 * table
 *
 * @param instructionVirtualAddress the virtual address to translate to physical address
 * @param scriptInfo the struct containing the page table needed to decode the
 * virtual address
 *
 * @return the physical address if a valid mapping is available and -1 if the
 * virtual address is invalid
 */
int virtualToPhysicalAddress(int instructionVirtualAddress, struct scriptFrames *scriptInfo) {
    int pageNumber, frameNumber, physicalAddress, rv = -1;

    // First determine the page number 'bits'
    pageNumber = instructionVirtualAddress / PAGE_SIZE;
    frameNumber = scriptInfo->pageTable[pageNumber];
    // The framenumber is invalid if '-1' is stored in the page table
    if (frameNumber >= 0) {
        rv = frameNumber * 3 + (instructionVirtualAddress % PAGE_SIZE);
    }

    return rv;
}

/**
 * Function that updates the LRU ranking by designating a new most recently
 * accessed frame
 *
 * @param frameMostRecentlyUsed the new frame with LRU rank '0' to be updated in the ranking
 * 
 * @return void
 */
void updateLRURanking(int frameMostRecentlyUsed) {
    int frameIdx, oldRank;

    oldRank = framesMetadata[frameMostRecentlyUsed].LRU_idx;

    // Loop over the frames to update the ranking if needed
    for (frameIdx = 0; frameIdx < FRAME_NUMBER; frameIdx++) {
        // Case where the frame was ranked more recently used that the
        // frameMostRecentlyUsed
        if (framesMetadata[frameIdx].LRU_idx < oldRank) {
            // Update the ranking to "age" the frames concerned
            framesMetadata[frameIdx].LRU_idx++;
        } else if (framesMetadata[frameIdx].LRU_idx == oldRank) {
            // Set the new most recently used frame's LRU rank
            framesMetadata[frameIdx].LRU_idx = 0;
        }
    }
}
