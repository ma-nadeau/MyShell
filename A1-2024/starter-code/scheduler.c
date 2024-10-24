#include <semaphore.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "shellmemory.h"
#include "shell.h"
#include "scheduler.h"


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
sem_t finishedWork[WORKERS_NUMBER];

pthread_mutex_t readyQueueLock;
pthread_mutex_t memoryAvailabilityDLLLock;

policy_t policyGlobal;
int isRunningBackgroundGlobal;

char *shellmemoryCode[MEM_SIZE];
struct availableMemory *availableMemoryHead;



/**
 * @brief This function intializes the ready queue.
 */
void scheduler_init() {
    // Initialize variable and code shellmemory
    int mem_idx, val_idx;
    for (mem_idx = 0; mem_idx < MEM_SIZE; mem_idx++) {
        shellmemoryCode[mem_idx] = NULL;
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
    for(int i = 0; i < WORKERS_NUMBER; i++){
        sem_init(&finishedWork[i], 0, 0);
    }
    policyGlobal = INVALID_POLICY;
    isRunningBackgroundGlobal = 0;
    isTimeToExit = 0;

    pthread_mutex_init(&readyQueueLock, NULL);
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
            if (blockPointer->length == 0 &&
                blockPointer != availableMemoryHead) {
                blockPointer->prev->next = blockPointer->next;
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
 * @brief Deallocates the memory occupied by a script associated with the given PCB.
 *
 * This function frees the memory that was allocated for the script linked to 
 * the specified process control block (PCB). 
 *
 * @param pcb A pointer to the PCB structure that contains the script whose memory is to be deallocated.
 * @return void This function does not return a value.
 */
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

/**
 * @brief Removes a PCB from the readyQueue.
 *
 * This function detaches the specified Process Control Block (PCB) from the ready queue and
 * reattaches the previous and next nodes (if any) together.
 *
 * @param p1 A pointer to the PCB to be removed from the queue.
 */
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

/**
 * @brief Removes and returns the head process control block (PCB) from the queue.
 *
 * This function retrieves and removes the first PCB from the PCB queue. 
 * If the queue is empty, it returns NULL.
 *
 * @return A pointer to the PCB that was removed from the head of the queue, 
 *         or NULL if the queue is empty.
 */
struct PCB *popHeadFromPCBQueue() {
    struct PCB *rv;

    pthread_mutex_lock(&readyQueueLock);
        if (readyQueue.head){
            rv = readyQueue.head;
            detachPCBFromQueue(readyQueue.head);
        } else {
            rv = NULL;
        }
    pthread_mutex_unlock(&readyQueueLock);

    return rv;
}

/**
 * @brief Removes a PCB from the queue and deallocates associated resources.
 *
 * This function detaches the specified Process Control Block (PCB) from the readyQueue and 
 * deallocates any memory resources associated with it.
 *
 * @param p1 A pointer to the PCB to be removed from the queue.
 */
void removePCBFromQueue(struct PCB *p1) {
    detachPCBFromQueue(p1);
    deallocateMemoryScript(p1);
}

/**
 * @brief Loads a new script into memory.
 *
 * This function takes a file pointer to a script file and loads its contents into the shell memory 
 * for execution. It handles the necessary allocation and initialization of memory as 
 * required by the script's format.
 *
 * @param p A pointer to the FILE object representing the script to be loaded.
 * @return Returns 0 on success, or a negative value on failure (e.g., if the file cannot be read).
 */
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

    pthread_mutex_lock(&readyQueueLock);
    if (readyQueue.tail) {
        readyQueue.tail->next = newPCB;
        newPCB->prev = readyQueue.tail;
    }
    readyQueue.tail = newPCB;
    if (!readyQueue.head) {
        readyQueue.head = readyQueue.tail;
        readyQueue.head->prev = NULL;
    }
    pthread_mutex_unlock(&readyQueueLock);

    return 0;
}

/**
 * @brief Switches two PCB nodes in the readyQueue DLL.
 *
 * This function takes two pointers to Process Control Blocks (PCBs) and
 *  swaps place in the ReadyQueue DLL.
 *
 * @param p1 A pointer to the first PCB to be switched.
 * @param p2 A pointer to the second PCB to be switched.
 */
void switchPCBs(struct PCB *p1, struct PCB *p2) {
    struct PCB *tmp;

    pthread_mutex_lock(&readyQueueLock);
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

    pthread_mutex_unlock(&readyQueueLock);
}



/**
 * @brief Executes the PCBs from the ready queue.
 *
 * This function iterates through the ready queue of Process Control Blocks (PCBs) and 
 * executes each PCB one after the other starting at the head (i.e., head, head->next, ..., tail).
 */
void executeReadyQueuePCBs() {
    int line_idx;
    struct PCB *currentPCB;

    while ((currentPCB = popHeadFromPCBQueue())) {

        // Execute all lines of code
        for (line_idx = currentPCB->programCounter;
             line_idx < currentPCB->memoryStartIdx + currentPCB->lengthCode;
             line_idx++, currentPCB->programCounter++) {
            convertInputToOneLiners(shellmemoryCode[line_idx]);
        }
        deallocateMemoryScript(currentPCB);
    }
}


/**
 * @brief Orders PCBs in increasing order
 *
 * This function rearranges the readyQueue of PCBs such that the biggest processes run fist
 *
 * @param isRunningBackground A flag indicating whether to consider background processes (1 for true, 0 for false).
 */
void orderIncreasingPCBs(int isRunningBackground) {
    struct PCB *smallest;
    struct PCB *currentPCB;
    struct PCB *headWithoutMain;

    headWithoutMain = isRunningBackground ? readyQueue.head->next : readyQueue.head;

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
    
    headWithoutMain = isRunningBackground ? readyQueue.head->next : readyQueue.head;

    
    if (headWithoutMain->next && headWithoutMain->next->next &&
        headWithoutMain->next->next->lengthScore <
            headWithoutMain->next->lengthScore) {
        switchPCBs(headWithoutMain->next, headWithoutMain->next->next);
    }
}

/**
 * @brief Inserts a PCB at the start (head) of a doubly linked list.
 *
 * This function takes a pointer to a PCB structure and adds it to the start (head) of the 
 * readyQueue doubly linked list.
 *
 * @param p1 A pointer to the PCB structure to be placed at the start (head) of the linked list.
 */
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


/**
 * @brief Inserts a PCB at the end (tail) of a doubly linked list.
 *
 * This function takes a pointer to a PCB structure and appends it to the end (tail) of the 
 * readyQueue doubly linked list.
 *
 * @param p1 A pointer to the PCB structure to be placed at the end of the linked list.
 */
void placePCBAtEndOfDLL(struct PCB *p1) {
    int wasPlaced = 0;
    pthread_mutex_lock(&readyQueueLock);
    // Check for case where list is empty
    if (!readyQueue.head) {
        readyQueue.head = p1;
        readyQueue.tail = p1;
        p1->next = NULL;
        p1->prev = NULL;
        wasPlaced = 1;
    }

    // Update the tail
    if (!wasPlaced) {
        readyQueue.tail->next = p1;
        p1->prev = readyQueue.tail;
        p1->next = NULL;
        readyQueue.tail = p1;
    }
    pthread_mutex_unlock(&readyQueueLock);
}

/* ============================================
 * Section: Execute Scripts Functions
 *
 * This section contains functions related to 
 * the execute scripts from the shell.
 * ============================================ */

/**
 * @brief Executes the Round Robin (RR) scheduling policy.
 *
 * This function implements the Round Robin (RR) scheduling algorithm for a set of processes
 * based on the specified line number.
 *
 * @param lineNumber The number of line to execute before switching processes.
 * @return Nothing
 */
void runRR(int lineNumber) {
    struct PCB *currentPCB;
    int line_idx;
    int programCounterTmp;

    while ((currentPCB = popHeadFromPCBQueue())) {

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
            deallocateMemoryScript(currentPCB);
        } else {
            placePCBAtEndOfDLL(currentPCB);
        }
    }
}

/**
 * @brief Executes the Aging scheduling policy.
 *
 * This function implements the Aging scheduling algorithm for the process in the readyQueue
 *
 * @return Nothing
 */
void runAging() {
    struct PCB *currentPCB, *smallest, *currentHead;
    int line_idx, programCounterTmp;
    
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

/**
 * @brief Selects the scheduling strategy based on the specified policy.
 *
 * This function takes a scheduling policy as input and configures the readyQueue accordingly
 *
 * @param policy A value of type `policy_t` that represents the scheduling policy to be used. (i.e., FCSF, SJF, RR, RR30, AGING)
 */
void selectSchedule(policy_t policy) {
    switch (policy) {
        // Since the readyQueue for SJF was sorted in schedulerRun beforehand, it becomes the same as FCFS
        case FCFS:
        case SJF:
            executeReadyQueuePCBs();
            break;
        case RR:
            runRR(2);
            break;
        case RR30:
            runRR(30);
            break;
        case AGING:
            runAging();
            break;
    }
}

/**
 * @brief The main function for the worker thread.
 *
 * This function runs in a loop, waiting for work to be available via a semaphore. 
 * When work is signaled, it selects the scheduling policy to manage process execution. 
 * The loop continues until a termination signal is received
 *
 */
void *workerThread(void *workerID) {
    while (1) {
        sem_wait(&isThereWorkSem);
        selectSchedule(policyGlobal);
        sem_post(&finishedWork[*(int *)workerID]);

        if (isTimeToExit) {
            free(workerID);
            pthread_exit(NULL);
        }
    }
}


/**
 * @brief Executes the scheduling process based on the specified policy and execution modes.
 *
 * This function initiates the scheduling of processes according to the provided 
 * policy. It manages whether the processes should run in the background or 
 * concurrently, adjusting the scheduling behavior accordingly.
 *
 * @param policy A value of type 'policy_t' representing the scheduling policy to be applied.
 * @param isRunningBackground An integer flag indicating if the scheduling should occur in the background. (1 if True, 0 if False)
 * @param isRunningConcurrently An integer flag indicating if processes should run concurrently. (1 if True, 0 if False)
 */
void schedulerRun(policy_t policy, int isRunningBackground,
                  int isRunningConcurrently) {
    struct PCB *currentPCB, *smallest, *currentHead;
    int line_idx, programCounterTmp;

    if (policy == SJF || policy == AGING){
        orderIncreasingPCBs(isRunningBackground);
    }

    if (isRunningConcurrently && !isRunningWorkers) {
        for (int i = 0; i < WORKERS_NUMBER; i++) {
            int *workerId = (int *) malloc(sizeof(int));
            *workerId = i;
            pthread_create(&workers[i], NULL, workerThread, workerId);
        }
        isRunningWorkers = 1;
    }

    if (isRunningConcurrently) {
        policyGlobal = policy;
        if (isRunningBackground) {
            isRunningBackgroundGlobal = isRunningBackground;
        }
        if (readyQueue.head) {
            for (int i = 0; i < WORKERS_NUMBER; i++) {
                sem_post(&isThereWorkSem);
            }
        }

        // Wait for the worker threads
        for(int i = 0; i < WORKERS_NUMBER; i++){
            sem_wait(&finishedWork[i]);
        }

        // Check if quit called in threads
        if (isTimeToExit) {
            joinAllThreads();
            exit(0);
        }
    } else {
        selectSchedule(policy);
    }
}

/**
 * @brief Waits for all running worker threads to finish execution.
 *
 * This function sets a termination flag and signals the worker threads to exit. 
 * It then calls 'pthread_join' for each thread, ensuring the main thread waits 
 * for their completion before proceeding.
 */
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

int isMainThread(pthread_t runningPthread) {
    for(int i = 0; i < WORKERS_NUMBER; i++){
        if(pthread_equal(runningPthread, workers[i])){
            return 0;
        }
    }

    return 1;
}