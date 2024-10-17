#include "shellmemory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell.h"

struct memory_struct {
    char *var;
    char *value[MAX_VALUE_SIZE];
};

struct PCB {
    int pid;
    int memoryStartIdx;
    int lengthCode;
    int programCounter;
    struct PCB *next;
};

struct PCBQueue {
    struct PCB *head;
    struct PCB *tail;
} readyQueue;

struct availableMemory {
    int memoryStartIdx;
    int length;
    struct availableMemory *next;
    struct availableMemory *prev;
};

struct memory_struct shellmemory[MEM_SIZE];
char *shellmemoryCode[MEM_SIZE];
struct availableMemory *availableMemoryHead;

// Helper functions
int match(char *model, char *var) {
    int i, len = strlen(var), matchCount = 0;
    for (i = 0; i < len; i++) {
        if (model[i] == var[i]) matchCount++;
    }
    if (matchCount == len) {
        return 1;
    } else {
        return 0;
    }
}

// Shell memory functions

void mem_init() {
    // Initialize variable and code shellmemory
    int mem_idx, val_idx;
    for (mem_idx = 0; mem_idx < MEM_SIZE; mem_idx++) {
        shellmemory[mem_idx].var = NULL;
        shellmemoryCode[mem_idx] = NULL;
        for (val_idx = 0; val_idx < MAX_VALUE_SIZE; val_idx++) {
            shellmemory[mem_idx].value[val_idx] = NULL;
        }
    }

    // Initialize readyQueue
    readyQueue.head = NULL;
    readyQueue.tail = NULL;

    // Initialize available memory
    availableMemoryHead =
        (struct availableMemory *)malloc(sizeof(struct availableMemory));
    availableMemoryHead->memoryStartIdx = 0;
    availableMemoryHead->length = MEM_SIZE;
    availableMemoryHead->next = NULL;
    availableMemoryHead->prev = NULL;
}

// clear value of a variable
void mem_clear_value(int mem_idx) {
    int val_idx = 0;
    while (val_idx < MAX_VALUE_SIZE &&
           shellmemory[mem_idx].value[val_idx] != NULL) {
        free(shellmemory[mem_idx].value[val_idx]);
        shellmemory[mem_idx].value[val_idx] = NULL;
        val_idx++;
    }
    return;
}

// Set key value pair
void mem_set_value(char *var_in, char *values_in[], int number_values) {
    int mem_idx, val_idx;

    // If variable exists, we overwrite the values
    for (mem_idx = 0; mem_idx < MEM_SIZE; mem_idx++) {
        // Check to see if memory spot was not initialized (in which case we
        // reached the end of initialized variables) or whether the memory spot
        // corresponds to the variable passed as argument
        if (shellmemory[mem_idx].var == NULL ||
            strcmp(shellmemory[mem_idx].var, var_in) == 0) {
            if (shellmemory[mem_idx].var ==
                NULL) {  // Case where we reached end of known variables
                // Create our new variable in this spot
                shellmemory[mem_idx].var = strdup(var_in);
            } else {  // Case where variable already existed
                // clear the old values of variable appropriately
                mem_clear_value(mem_idx);
            }
            // In either case we populate the new values
            for (val_idx = 0; val_idx < number_values; val_idx++) {
                shellmemory[mem_idx].value[val_idx] =
                    strdup(values_in[val_idx]);
            }
            return;
        }
    }

    // out of memory here
    return;
}

// get variable entry index in memory
int mem_get_variable_index(char *var_in) {
    int mem_idx, val_idx;

    for (mem_idx = 0; mem_idx < MEM_SIZE; mem_idx++) {
        // Case where we reached end of initialized memory
        if (shellmemory[mem_idx].var == NULL) {
            return -1;
        }
        if (strcmp(shellmemory[mem_idx].var, var_in) == 0) {
            return mem_idx;
        }
    }
    return -1;
}

// get value based on input key
void mem_get_value(char *var_in, char *buffer) {
    int mem_idx, val_idx;
    char *space = " ";

    // Assume there is an error
    strcpy(buffer, "Variable does not exist");

    mem_idx = mem_get_variable_index(var_in);
    if (mem_idx > -1) {
        // Assemble the values
        buffer[0] = '\0';  // Initialize buffer with null character
        val_idx = 0;
        while (val_idx < MAX_VALUE_SIZE &&
               shellmemory[mem_idx].value[val_idx] != NULL) {
            if (val_idx != 0) {
                strcat(buffer, space);
            }
            strcat(buffer, shellmemory[mem_idx].value[val_idx]);
            val_idx++;
        }
        return;
    }
    return;
}

int codeLength(char *script) {
    FILE *p = fopen(script, "rt");  // the program is in a file
    int count = 1;
    char c;

    if (p == NULL) {
        return -1;
    }
    
    do {
        c = getc(p);
        if (c == '\n') {
            count++;
        }
    } while (c != EOF);

    fclose(p);

    return count;
}

int allocateMemoryScript(int scriptLength) {
    int tmp;
    // Fetch available memory list head
    struct availableMemory *blockPointer = availableMemoryHead;
    // Loop over available memory to find enough space
    while (blockPointer) {
        // Enough space found
        if (blockPointer->length >= scriptLength) {
            // Update the available memory block
            tmp = blockPointer->memoryStartIdx;
            blockPointer->memoryStartIdx += scriptLength;
            blockPointer->length -= scriptLength;
            // Check if the block is now empty
            if (blockPointer->length == 0 &&
                blockPointer != availableMemoryHead) {
                blockPointer->prev->next = blockPointer->next;
                free(blockPointer);
            }
            return tmp;
        }
    }

    // If we end up here it means that no memory was available for the script
    return -1;
}

void addMemoryAvailability(int memoryStartIdx, int lengthCode) {
    struct availableMemory *currentMemoryBlock = availableMemoryHead;

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
                currentMemoryBlock->prev->memoryStartIdx += lengthCode;
                currentMemoryBlock->prev->length += lengthCode;
                currentMemoryBlock->prev->next = currentMemoryBlock->next;
                free(currentMemoryBlock);
            }
            break;
        }
        currentMemoryBlock = currentMemoryBlock->next;
    }
}

void deallocateMemoryScript(struct PCB *pcb) {
    int line_idx;

    // Free the code in shell memory
    for (line_idx = pcb->memoryStartIdx;
         line_idx < pcb->memoryStartIdx + pcb->lengthCode; line_idx++) {
        free(shellmemoryCode[line_idx]);
    }
    addMemoryAvailability(pcb->memoryStartIdx, pcb->lengthCode);
    free(pcb);
}

// Function to load a new script in memory
int mem_load_script(char *script) {
    char line[MAX_USER_INPUT];
    int scriptLength = codeLength(script);
    int line_idx, mem_idx;
    FILE *p = fopen(script, "rt");  // the program is in a file
    struct PCB *newPCB;

    if (p == NULL || scriptLength == -1) {
        return -1;
    }

    mem_idx = allocateMemoryScript(scriptLength);

    for (line_idx = mem_idx; line_idx < mem_idx + scriptLength; line_idx++) {
        fgets(line, MAX_USER_INPUT - 1, p);
        shellmemoryCode[line_idx] = strdup(line);
        memset(line, 0, sizeof(line));

        if (feof(p)) {
            break;
        }
    }

    fclose(p);

    // Initialize new PCB
    newPCB = (struct PCB *)malloc(sizeof(struct PCB));
    newPCB->pid = rand();
    newPCB->memoryStartIdx = mem_idx;
    newPCB->lengthCode = scriptLength;
    newPCB->programCounter = mem_idx;
    newPCB->next = NULL;

    if (readyQueue.tail) {
        readyQueue.tail->next = newPCB;
    }
    readyQueue.tail = newPCB;
    if (!readyQueue.head) {
        readyQueue.head = readyQueue.tail;
    }

    return 0;
}

void schedulerRun(policy_t policy) {
    struct PCB *currentPCB;
    int line_idx;

    switch (policy) {
        case FCFS:
            while (readyQueue.head) {
                currentPCB = readyQueue.head;

                // Execute all lines of code
                for (line_idx = currentPCB->programCounter;
                     line_idx <
                     currentPCB->memoryStartIdx + currentPCB->lengthCode;
                     line_idx++, currentPCB->programCounter++) {
                    convertInputToOneLiners(shellmemoryCode[line_idx]);
                }

                readyQueue.head = currentPCB->next;
                deallocateMemoryScript(currentPCB);
            }
    }
}
