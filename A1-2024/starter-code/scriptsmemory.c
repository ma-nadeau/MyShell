#include <pthread.h>
#include <stdlib.h>

#include "shellmemory.h"

struct availableMemory {
    int memoryStartIdx;
    int length;
    struct availableMemory *next;
    struct availableMemory *prev;
};

pthread_mutex_t memoryAvailabilityDLLLock;
char *shellmemoryCode[MEM_SIZE];
struct availableMemory *availableMemoryHead;

/*** FUNCTIONS FOR SCRIPT MEMORY ***/

/**
 * @brief This function intializes the memory for the scripts.
 */
void scripts_memory_init() {
    // Initialize variable and code shellmemory
    int mem_idx, val_idx;
    for (mem_idx = 0; mem_idx < MEM_SIZE; mem_idx++) {
        shellmemoryCode[mem_idx] = NULL;
    }

    // Initialize available memory
    availableMemoryHead =
        (struct availableMemory *)malloc(sizeof(struct availableMemory));
    availableMemoryHead->memoryStartIdx = 0;
    availableMemoryHead->length = MEM_SIZE;
    availableMemoryHead->next = NULL;
    availableMemoryHead->prev = NULL;

    pthread_mutex_init(&memoryAvailabilityDLLLock, NULL);
}


/**
 * @brief Allocates memory for a script.
 *
 * This function allocates a block of memory of the specified length to hold a script.
 *
 * @param scriptLength The length of the script for which memory needs to be allocated (i.e., the number of lines).
 * @return Returns the starting index (i.e., the address in memory) on success, or -1 if an error occurs.
 */
int allocateMemoryScript(int scriptLength) {
    pthread_mutex_lock(&memoryAvailabilityDLLLock);
    int tmp = -1;
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
                if(blockPointer == availableMemoryHead) {
                    if (!availableMemoryHead->next){
                        break;
                    }
                    availableMemoryHead = availableMemoryHead->next;
                    availableMemoryHead->prev = NULL;
                } else {
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

    // If we end up here it means that no memory was available for the script
    pthread_mutex_unlock(&memoryAvailabilityDLLLock);
    return tmp;
}

/**
 * @brief Adds back the memory space previously occupied by a script once it is freed.
 *
 * This function updates the available memory by marking the specified block of memory 
 * as free, allowing it to be reused for future allocations.
 *
 * @param memoryStartIdx The index in memory where the previously allocated space begins.
 * @param lengthCode The length of the memory block to be freed.
 * @return void This function does not return a value.
 */
void addMemoryAvailability(int memoryStartIdx, int lengthCode) {
    pthread_mutex_lock(&memoryAvailabilityDLLLock);
    struct availableMemory *currentMemoryBlock = availableMemoryHead;

    // Looping through the DLL
    while (currentMemoryBlock) {
        if (memoryStartIdx < currentMemoryBlock->memoryStartIdx) {
            // Check to see if the memory freed is contiguous with the memory
            // afterwards
            if (memoryStartIdx + lengthCode ==
                currentMemoryBlock->memoryStartIdx) {
                currentMemoryBlock->memoryStartIdx -= lengthCode;
                currentMemoryBlock->length += lengthCode;
            } else {
                struct availableMemory *tmp = (struct availableMemory *)malloc(
                    sizeof(struct availableMemory));
                tmp->memoryStartIdx = memoryStartIdx;
                tmp->length = lengthCode;
                tmp->next = currentMemoryBlock;
                tmp->prev = currentMemoryBlock->prev;
                if (currentMemoryBlock->prev){
                    currentMemoryBlock->prev->next = tmp;
                }
                currentMemoryBlock->prev = tmp;
                if (availableMemoryHead == currentMemoryBlock) {
                    availableMemoryHead = tmp;
                }
                currentMemoryBlock = tmp;
            }

            // Check to see if the memory freed is contiguous with the memory
            // before
            if (currentMemoryBlock->prev &&
                currentMemoryBlock->prev->memoryStartIdx +
                        currentMemoryBlock->prev->length ==
                    currentMemoryBlock->memoryStartIdx) {
                currentMemoryBlock->prev->length += currentMemoryBlock->length;
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
*/
char *fetchInstruction(int instructionAddress) {
    return shellmemoryCode[instructionAddress];
}
/**
*  Updates the instruction at the specified address in shell memory.
*/
void updateInstruction(int instructionAddress, char *newInstruction) {
    shellmemoryCode[instructionAddress] = newInstruction;
}