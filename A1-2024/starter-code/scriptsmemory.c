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

/*** FUNCTIONS FOR SCRIPT MEMORY ***/

/**
 * This function intializes the memory for the scripts as well as
 * resources required for script memory management.
 */
void scripts_memory_init() {
    // Initialize variable and code shellmemory
    int mem_idx, frameIdx;
    for (mem_idx = 0; mem_idx < FRAME_STORE_SIZE; mem_idx++) {
        shellmemoryCode[mem_idx] = NULL;
    }

    // Initialize frames metadata
    // Note that the associated pageNumber doesn't need to be initialized
    for (frameIdx = 0; frameIdx < FRAME_NUMBER; frameIdx++){
        framesMetadata[frameIdx].associatedScript = NULL;
        framesMetadata[frameIdx].LRU_idx = FRAME_NUMBER - frameIdx - 1;
    }
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

char *fetchInstructionVirtual(int instructionVirtualAddress, struct scriptFrames *scriptInfo) {
    int physicalAddress;
    char *rv;

    physicalAddress = virtualToPhysicalAddress(instructionVirtualAddress, scriptInfo);
    if (physicalAddress >= 0){
        rv = shellmemoryCode[physicalAddress];
        updateLRURanking(physicalAddress/PAGE_SIZE);
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

void declareVictimePage(int victimePage, struct scriptFrames *scriptInfo){
    int pageOffset;
    char *instruction;

    printf("Page fault! Victim page contents:\n\n");
    for(pageOffset = 0; pageOffset < PAGE_SIZE; pageOffset++){
        instruction = fetchInstructionVirtual(victimePage * PAGE_SIZE + pageOffset, scriptInfo);
        printf("%s", instruction);
        free(instruction);
    }
    printf("\nEnd of victim page contents.\n");
}

void pageAssignment(int pageNumber, struct scriptFrames *scriptInfo, int setup) {
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
    } else if (!setup){
        printf("Page fault!\n");
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

struct scriptFrames *findExistingScript(char script[]){
    int frameIdx;
    struct scriptFrames *rv = NULL;

    for(frameIdx = 0; frameIdx < FRAME_NUMBER; frameIdx++){
        if (framesMetadata[frameIdx].associatedScript && strcmp(framesMetadata[frameIdx].associatedScript->scriptName, script) == 0) {
            rv = framesMetadata[frameIdx].associatedScript;
            break;
        }
    }

    return rv;
}
