#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "shellmemory.h"
#include "scriptsmemory.h"
#include "shell.h"

struct availableMemory {
    int memoryStartIdx;
    int length;
    struct availableMemory *next;
    struct availableMemory *prev;
};

struct frameMetaData {
    struct scriptFrames *associatedScript;
    int associatedPageNumber;
    int LRU_idx;
};

pthread_mutex_t memoryAvailabilityDLLLock;
char *shellmemoryCode[FRAME_STORE_SIZE];
struct availableMemory *availableMemoryHead;

struct frameMetaData framesMetadata[FRAME_NUMBER];

/*** FUNCTIONS FOR SCRIPT MEMORY ***/

/**
 * This function intializes the memory for the scripts as well as
 * resources required for script memory management.
 */
void scripts_memory_init() {
    // Initialize variable and code shellmemory
    int mem_idx, frameIdx;
    for (mem_idx = 0; mem_idx < VAR_MEMSIZE; mem_idx++) {
        shellmemoryCode[mem_idx] = NULL;
    }

    // Initialize frames metadata
    // Note that the associated pageNumber doesn't need to be initialized
    for (frameIdx = 0; frameIdx < FRAME_NUMBER; frameIdx++){
        framesMetadata[frameIdx].associatedScript = NULL;
        framesMetadata[frameIdx].LRU_idx = FRAME_NUMBER - frameIdx - 1;
    }

    // Initialize available memory
    availableMemoryHead =
        (struct availableMemory *)malloc(sizeof(struct availableMemory));
    availableMemoryHead->memoryStartIdx = 0;
    availableMemoryHead->length = VAR_MEMSIZE;
    availableMemoryHead->next = NULL;
    availableMemoryHead->prev = NULL;

    pthread_mutex_init(&memoryAvailabilityDLLLock, NULL);
}

/**
 * This function allocates a block of memory of the specified length to hold a
 * script.
 *
 * @param scriptLength The length of the script for which memory needs to be
 * allocated (i.e., the number of lines).
 * @return Returns the starting index (i.e., the address in memory) on success,
 * or -1 if an error occurs.
 */
int allocateMemoryScript(int scriptLength) {
    int tmp = -1;

    pthread_mutex_lock(&memoryAvailabilityDLLLock);
    // Fetch available memory list head
    struct availableMemory *blockPointer = availableMemoryHead;

    // Loop over available memory to find enough space
    while (blockPointer) {
        // Enough space found
        if (scriptLength <= blockPointer->length) {
            // Update the available memory block
            tmp = blockPointer->memoryStartIdx;
            blockPointer->memoryStartIdx += scriptLength;
            blockPointer->length -= scriptLength;

            // Check if the block is now empty
            if (blockPointer->length == 0) {
                // Check if the block is the head
                // in which case we only remove the block
                // if it is not the only one
                if (blockPointer == availableMemoryHead) {
                    if (!availableMemoryHead->next) {
                        break;
                    }
                    // Removing the empty block
                    availableMemoryHead = availableMemoryHead->next;
                    availableMemoryHead->prev = NULL;
                } else {
                    // Removing the empty block
                    blockPointer->prev->next = blockPointer->next;
                    if (blockPointer->next) {
                        blockPointer->next->prev = blockPointer->prev;
                    }
                }
                free(blockPointer);
            }
            break;
        }
        blockPointer = blockPointer->next;
    }

    // If tmp=-1 here it means that no memory was available for the script
    pthread_mutex_unlock(&memoryAvailabilityDLLLock);
    return tmp;
}

/**
 * This function updates the available memory by marking the specified block of
 * memory as free, allowing it to be reused for future allocations.
 *
 * @param memoryStartIdx The index in memory where the previously allocated
 * space begins.
 * @param lengthCode The length of the memory block to be freed.
 */
void addMemoryAvailability(int memoryStartIdx, int lengthCode) {
    pthread_mutex_lock(&memoryAvailabilityDLLLock);
    struct availableMemory *currentMemoryBlock = availableMemoryHead;

    // Looping through the DLL
    while (currentMemoryBlock) {
        // Find the first block of available memory
        // that starts after the block to be freed
        if (memoryStartIdx < currentMemoryBlock->memoryStartIdx) {
            // Check to see if the memory freed is contiguous with the memory
            // afterwards
            if (memoryStartIdx + lengthCode == currentMemoryBlock->memoryStartIdx) {
                // In which cases we merge the two blocks of available memory
                currentMemoryBlock->memoryStartIdx -= lengthCode;
                currentMemoryBlock->length += lengthCode;
            } else {
                // Otherwise we need to add a new node for the available memory
                struct availableMemory *tmp = (struct availableMemory *)malloc(
                    sizeof(struct availableMemory));
                tmp->memoryStartIdx = memoryStartIdx;
                tmp->length = lengthCode;
                tmp->next = currentMemoryBlock;
                tmp->prev = currentMemoryBlock->prev;

                // Arranging prev and next pointers to add the tmp block
                // before the currentMemoryBlock
                if (currentMemoryBlock->prev) {
                    currentMemoryBlock->prev->next = tmp;
                }
                currentMemoryBlock->prev = tmp;
                if (availableMemoryHead == currentMemoryBlock) {
                    availableMemoryHead = tmp;
                }

                // Update currentMemoryBlock for the case where the
                // freed memory is contiguous with the previous block
                currentMemoryBlock = tmp;
            }

            // Check to see if the memory freed is contiguous with the previous memory block
            if (currentMemoryBlock->prev &&
                currentMemoryBlock->prev->memoryStartIdx + currentMemoryBlock->prev->length ==
                    currentMemoryBlock->memoryStartIdx) {
                // We merge the freed memory with the previous memory if they are contiguous
                currentMemoryBlock->prev->length += currentMemoryBlock->length;
                // And update the prev and next pointers to remove the freed memory block that is merged
                currentMemoryBlock->prev->next = currentMemoryBlock->next;
                if (currentMemoryBlock->next) {
                    currentMemoryBlock->next->prev = currentMemoryBlock->prev;
                }
                free(currentMemoryBlock);
            }
            break;
        }
        currentMemoryBlock = currentMemoryBlock->next;
    }
    pthread_mutex_unlock(&memoryAvailabilityDLLLock);
}

/**
 * Fetches the instruction from shell memory at the specified address.
 * 
 * @param instructionAddress The address in shell memory from which to retrieve
 * the instruction.
 * @return Returns a pointer to the instruction string at the specified address.
 */
char *fetchInstruction(int instructionAddress) {
    return shellmemoryCode[instructionAddress];
}

int virtualToPhysicalAddress(int instructionVirtualAddress, struct scriptFrames *scriptInfo){
    int pageNumber, frameNumber, physicalAddress, rv = -1;

    pageNumber = instructionVirtualAddress / PAGE_SIZE;
    frameNumber = scriptInfo->pageTable[pageNumber];
    if (frameNumber >= 0){
        rv = frameNumber * 3 + (instructionVirtualAddress % PAGE_SIZE);
    }
    
    return rv;
}

char *fetchInstructionVirtual(int instructionVirtualAddress, struct scriptFrames *scriptInfo) {
    int physicalAddress;
    char *rv;

    physicalAddress = virtualToPhysicalAddress(instructionVirtualAddress, scriptInfo);
    if (physicalAddress >= 0){
        rv = shellmemoryCode[physicalAddress];
    } else {
        rv = NULL;
    }

    return rv;
}

void updateInstructionVirtual(int instructionVirtualAddress, struct scriptFrames *scriptInfo, char newInstruction[]) {
    int physicalAddress;

    physicalAddress = virtualToPhysicalAddress(instructionVirtualAddress, scriptInfo);
    shellmemoryCode[physicalAddress] = newInstruction;
}

int findLRUFrame(){
    int frameIdx;

    for(frameIdx = 0; frameIdx < FRAME_NUMBER; frameIdx++){
        if(framesMetadata[frameIdx].LRU_idx == FRAME_NUMBER - 1){
            return frameIdx;
        }
    }
}

void updateLRURanking(int frameMostRecentlyUsed){
    int frameIdx, oldRank;

    oldRank = framesMetadata[frameMostRecentlyUsed].LRU_idx;

    for(frameIdx = 0; frameIdx < FRAME_NUMBER; frameIdx++){
        if(framesMetadata[frameIdx].LRU_idx < oldRank){
            framesMetadata[frameIdx].LRU_idx++;
        } else if(framesMetadata[frameIdx].LRU_idx == oldRank){
            framesMetadata[frameIdx].LRU_idx = 0;
        }
    }
}

void declareVictimePage(int victimePage, struct scriptFrames *scriptInfo){
    int pageOffset;
    char *instruction;

    printf("Page fault! Victim page contents:\n");
    for(pageOffset = 0; pageOffset < PAGE_SIZE; pageOffset++){
        instruction = fetchInstructionVirtual(victimePage * PAGE_SIZE + pageOffset, scriptInfo);
        printf("%s\n", instruction);
        free(instruction);
    }
    printf("End of victim page contents.\n");
}

void pageAssignment(int pageNumber, struct scriptFrames *scriptInfo) {
    FILE *p;
    int LRUFrame, pageIdx, pageOffsetIdx;
    char line[MAX_USER_INPUT];

    // First find the frame to use (LRU)
    LRUFrame = findLRUFrame();
    updateLRURanking(LRUFrame);

    // If frame to use had a page then declare victim page and clean up
    if (framesMetadata[LRUFrame].associatedScript){
        // Declare the victim and free the memory allocated lines
        declareVictimePage(framesMetadata[LRUFrame].associatedPageNumber, framesMetadata[LRUFrame].associatedScript);
        
        // Invalidate page in page table
        framesMetadata[LRUFrame].associatedScript->pageTable[framesMetadata[LRUFrame].associatedPageNumber] = -1;

        framesMetadata[LRUFrame].associatedScript->FramesInUse--;
        // Special case where no frames or PCBs are using the script file anymore
        // free the memory allocated for the scriptInformation
        if(!framesMetadata[LRUFrame].associatedScript->PCBsInUse && !framesMetadata[LRUFrame].associatedScript->FramesInUse){
            free(framesMetadata[LRUFrame].associatedScript->scriptName);
            free(framesMetadata[LRUFrame].associatedScript);
            framesMetadata[LRUFrame].associatedScript = NULL;
        }
    }

    // Renew metaData to the frame
    framesMetadata[LRUFrame].associatedScript = scriptInfo;
    framesMetadata[LRUFrame].associatedScript->FramesInUse++;
    framesMetadata[LRUFrame].associatedPageNumber = pageNumber;

    // Validate pageTable of newly allocated page
    framesMetadata[LRUFrame].associatedScript->pageTable[pageNumber] = LRUFrame;

    // Write into memory the new page
    p = fopen(scriptInfo->scriptName, "rt");  // the program is in a file

    // Offset to current page
    for(pageIdx = 0; pageIdx < pageNumber; pageIdx++){
        for(pageOffsetIdx = 0; pageOffsetIdx < PAGE_SIZE; pageOffsetIdx++){
            fgets(line, MAX_USER_INPUT - 1, p);
        }
    }

    for(pageOffsetIdx = 0; pageOffsetIdx < PAGE_SIZE; pageOffsetIdx++){
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
 * Updates the instruction at the specified address in shell memory.
 * Note: this function is used in the scheduler.c file when loading a new script.
 * 
 * @param instructionAddress The address in shell memory where the instruction
 * will be updated.
 * @param newInstruction A pointer to the new instruction string to be stored at
 * the specified address.
 */
void updateInstruction(int instructionAddress, char *newInstruction) {
    shellmemoryCode[instructionAddress] = newInstruction;
}