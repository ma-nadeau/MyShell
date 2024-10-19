#include "shellmemory.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell.h"
#include <semaphore.h>


struct memory_struct {
    char *var;
    char *value[MAX_VALUE_SIZE];
};

struct PCB {
    int pid;
    int memoryStartIdx;
    int lengthCode;
    int lengthScore;
    int programCounter;
    struct PCB *next;
    struct PCB *prev;
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

pthread_t workers[WORKERS_NUMBER];
int isRunningWorkers;
int isTimeToExit;

sem_t isThereWorkSem;

pthread_mutex_t pickHeadReadyQueue;

policy_t policyGlobal;
int isRunningBackgroundGlobal;

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

    isRunningWorkers = 0;
    sem_init(&isThereWorkSem, 0, 0);
    policyGlobal = INVALID_POLICY;
    isRunningBackgroundGlobal = 0;
    isTimeToExit = 0;

    pthread_mutex_init(&pickHeadReadyQueue, NULL);
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
                currentMemoryBlock->prev->length += currentMemoryBlock->length;
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

void detachPCBFromQueue(struct PCB *p1) {
    // Case where p1 is at the head
    if (readyQueue.head == p1) {
        readyQueue.head = readyQueue.head->next;
        // Check if there are any PCBs left in the queue
        if (readyQueue.head) {
            readyQueue.head->prev = NULL;
        } else {
            // If not then update the tail
            readyQueue.tail = NULL;
        }
    } else if (readyQueue.tail == p1) {
        readyQueue.tail = readyQueue.tail->prev;

        if (readyQueue.tail) {
            readyQueue.tail->next = NULL;
        }

    } else {
        p1->prev->next = p1->next;
        p1->next->prev = p1->prev;
    }

    // Remove all attachment (free from desire)
    p1->next = NULL;
    p1->prev = NULL;
}

void removePCBFromQueue(struct PCB *p1) {
    detachPCBFromQueue(p1);
    deallocateMemoryScript(p1);
}

// Function to load a new script in memory
int mem_load_script(FILE *p) {
    char line[MAX_USER_INPUT];
    int scriptLength = 0, line_idx, mem_idx;
    struct PCB *newPCB;
    char *scriptLines[MEM_SIZE];

    if (p == NULL) {
        return -1;
    }

    while (1) {
        fgets(line, MAX_USER_INPUT - 1, p);
        scriptLines[scriptLength] = strdup(line);
        scriptLength++;

        memset(line, 0, sizeof(line));

        if (feof(p)) {
            break;
        }
    }

    mem_idx = allocateMemoryScript(scriptLength);

    for (line_idx = mem_idx; line_idx < mem_idx + scriptLength; line_idx++) {
        shellmemoryCode[line_idx] = scriptLines[line_idx - mem_idx];
    }

    // Initialize new PCB
    newPCB = (struct PCB *)malloc(sizeof(struct PCB));
    newPCB->pid = rand();
    newPCB->memoryStartIdx = mem_idx;
    newPCB->lengthCode = scriptLength;
    newPCB->lengthScore = scriptLength;
    newPCB->programCounter = mem_idx;
    newPCB->next = NULL;

    if (readyQueue.tail) {
        readyQueue.tail->next = newPCB;
        newPCB->prev = readyQueue.tail;
    }
    readyQueue.tail = newPCB;
    if (!readyQueue.head) {
        readyQueue.head = readyQueue.tail;
        readyQueue.head->prev = NULL;
    }

    return 0;
}

void switchPCBs(struct PCB *p1, struct PCB *p2) {
    struct PCB *tmp;
    // Updating nodes around p1
    if (p1->prev && p1->prev != p2) {
        p1->prev->next = p2;
    }
    if (p1->next && p1->next != p2) {
        p1->next->prev = p2;
    }
    // Updating nodes around p2
    if (p2->prev && p2->prev != p1) {
        p2->prev->next = p1;
    }
    if (p2->next && p2->next != p1) {
        p2->next->prev = p1;
    }

    // Updating next att for p1 and p2
    tmp = p2->next;
    if (p1->next != p2) {
        p2->next = p1->next;
    } else {
        p2->next = p1;
    }
    if (tmp != p1) {
        p1->next = tmp;
    } else {
        p1->next = p2;
    }
    // Updating prev att for p1 and p2
    tmp = p2->prev;
    if (p1->prev != p2) {
        p2->prev = p1->prev;
    } else {
        p2->prev = p1;
    }
    if (tmp != p1) {
        p1->prev = tmp;
    } else {
        p1->prev = p2;
    }

    // Update head
    if (p1 == readyQueue.head) {
        readyQueue.head = p2;
    } else if (p2 == readyQueue.head) {
        readyQueue.head = p1;
    }
    // Update tail
    if (p1 == readyQueue.tail) {
        readyQueue.tail = p2;
    } else if (p2 == readyQueue.tail) {
        readyQueue.tail = p1;
    }
}

void executeReadyQueuePCBs() {
    int line_idx;
    struct PCB *currentPCB;

    while (readyQueue.head) {

        // Check if there are any processes left
        // Pick the first one if so
        pthread_mutex_lock(&pickHeadReadyQueue);
        if (readyQueue.head){
            currentPCB = readyQueue.head;
            detachPCBFromQueue(readyQueue.head);
        }
        pthread_mutex_unlock(&pickHeadReadyQueue);

        // Execute all lines of code
        for (line_idx = currentPCB->programCounter;
             line_idx < currentPCB->memoryStartIdx + currentPCB->lengthCode;
             line_idx++, currentPCB->programCounter++) {
            convertInputToOneLiners(shellmemoryCode[line_idx]);
        }
        deallocateMemoryScript(currentPCB);
    }
}

void orderIncreasingPCBs(int isRunningBackground) {
    struct PCB *smallest;
    struct PCB *currentPCB;
    struct PCB *headWithoutMain;

    if (isRunningBackground) {
        headWithoutMain = readyQueue.head->next;
    }

    smallest = headWithoutMain;
    currentPCB = headWithoutMain->next;

    // Here we are simply comparing the head with the other values, and
    // selecting the one with the smallest number of lines
    while (currentPCB) {
        if (currentPCB->lengthScore < smallest->lengthScore) {
            smallest = currentPCB;
        }
        currentPCB = currentPCB->next;
    }

    if (smallest != headWithoutMain) {
        switchPCBs(smallest, headWithoutMain);
    }
    if (headWithoutMain->next && headWithoutMain->next->next &&
        headWithoutMain->next->next->lengthScore <
            headWithoutMain->next->lengthScore) {
        switchPCBs(headWithoutMain->next, headWithoutMain->next->next);
    }
}

void placePCBHeadAtEndOfDLL() {
    struct PCB *tmp;

    // Check for case where list is empty or one PCB only
    if (readyQueue.tail == readyQueue.head) {
        return;
    }
    tmp = readyQueue.head;

    // Update the head
    readyQueue.head = readyQueue.head->next;
    readyQueue.head->prev = NULL;

    // Update the tail
    readyQueue.tail->next = tmp;
    tmp->prev = readyQueue.tail;
    tmp->next = NULL;
    readyQueue.tail = tmp;
}

void runRR(int lineNumber) {
    struct PCB *currentPCB;
    int line_idx;
    int programCounterTmp;
    while (readyQueue.head) {
        currentPCB = readyQueue.head;

        // Execute lineNumber lines of code
        programCounterTmp = currentPCB->programCounter;
        for (line_idx = currentPCB->programCounter;
             line_idx < currentPCB->memoryStartIdx + currentPCB->lengthCode &&
             line_idx < programCounterTmp + lineNumber;
             line_idx++, currentPCB->programCounter++) {
            convertInputToOneLiners(shellmemoryCode[line_idx]);
        }
        // Check if process has finished running
        if (currentPCB->programCounter ==
            currentPCB->memoryStartIdx + currentPCB->lengthCode) {
            removePCBFromQueue(currentPCB);
        } else {
            placePCBHeadAtEndOfDLL();
        }
    }
}

void runAging(int isRunningBackground) {
    struct PCB *currentPCB, *smallest, *currentHead;
    int line_idx, programCounterTmp;
    // First Assessment
    orderIncreasingPCBs(isRunningBackground);
    while (readyQueue.head) {
        currentHead = readyQueue.head;

        // Time slice
        convertInputToOneLiners(
            shellmemoryCode[readyQueue.head->programCounter]);
        readyQueue.head->programCounter++;

        // Aging all processes
        currentPCB = readyQueue.head->next;
        while (currentPCB) {
            // Make sure that the length score is not null
            if (currentPCB->lengthScore) {
                currentPCB->lengthScore--;
            }
            currentPCB = currentPCB->next;
        }

        // Check if process has stopped running
        if (readyQueue.head->programCounter ==
            readyQueue.head->memoryStartIdx + readyQueue.head->lengthCode) {
            removePCBFromQueue(readyQueue.head);
        }

        // Check if there are any other processes left
        if (!readyQueue.head) {
            break;
        }

        // Find the process with lowest score closest to Head of list
        currentPCB = readyQueue.head;
        smallest = currentPCB;
        while (currentPCB) {
            if (currentPCB->lengthScore < smallest->lengthScore) {
                smallest = currentPCB;
            }
            currentPCB = currentPCB->next;
        }

        // Preempt the head if it didn't finish running
        if (currentHead == readyQueue.head) {
            placePCBHeadAtEndOfDLL();
        }
        // Put the smallest PCB at the start of the queue
        detachPCBFromQueue(smallest);
        smallest->next = readyQueue.head;
        smallest->prev = NULL;
        if (readyQueue.head) {
            readyQueue.head->prev = smallest;
        } else {
            readyQueue.tail = smallest;
        }
        readyQueue.head = smallest;
    }
}

void selectSchedule(policy_t policy, int isRunningBackground) {
    switch (policy) {
        case FCFS:
            executeReadyQueuePCBs();
            break;
        case SJF:
            orderIncreasingPCBs(isRunningBackground);
            executeReadyQueuePCBs();
            break;
        case RR:
            runRR(2);
            break;
        case RR30:
            runRR(30);
            break;
        case AGING:
            runAging(isRunningBackground);
            break;
    }
}

void *workerThread(void *arg) {
    while (1) {
        sem_wait(&isThereWorkSem);
        if (isTimeToExit) {
            pthread_exit(NULL);
        }
        selectSchedule(policyGlobal, isRunningBackgroundGlobal);
    }
}

void schedulerRun(policy_t policy, int isRunningBackground,
                  int isRunningConcurrently) {
    struct PCB *currentPCB, *smallest, *currentHead;
    int line_idx, programCounterTmp;

    if (isRunningConcurrently && !isRunningWorkers) {
        for (int i = 0; i < WORKERS_NUMBER; i++) {
            pthread_create(&workers[i], NULL, workerThread, NULL);
        }
        isRunningWorkers = 1;
    }

    if (isRunningConcurrently) {
        policyGlobal = policy;
        if (isRunningBackground) {
            isRunningBackgroundGlobal = isRunningBackground;
        }
        for (int i = 0; i < WORKERS_NUMBER; i++) {
            sem_post(&isThereWorkSem);
        }
    } else {
        selectSchedule(policy, isRunningBackground);
    }
}


void joinAllThreads() {
    if (isRunningWorkers){
        isTimeToExit = 1;
        for (int i = 0; i < WORKERS_NUMBER; i++) {
            sem_post(&isThereWorkSem);
        }
        for (int i = 0; i < WORKERS_NUMBER; i++) {
            pthread_join(workers[i], NULL);
        }
    }
}