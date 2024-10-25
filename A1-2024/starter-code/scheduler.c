#include <semaphore.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "shellmemory.h"
#include "shell.h"
#include "scheduler.h"
#include "scriptsmemory.h"

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

pthread_t workers[WORKERS_NUMBER];
int isRunningWorkers;
int isThereWorkToDo;
int isTimeToExit;
int startExitProcedure;
int finishedWork;

pthread_cond_t isThereWorkToDoCond;
pthread_cond_t finishedWorkCond;

pthread_mutex_t readyQueueLock;

pthread_mutex_t isThereWorkToDoLock;
pthread_mutex_t finishedWorkLock;

policy_t policyGlobal;


/*** FUNCTION SIGNATURES ***/

void insertPCBFromTailSJF(struct PCB *pcb);
void placePCBHeadAtEndOfDLL();
void detachPCBFromQueue(struct PCB *p1);
struct PCB *popHeadFromPCBQueue();
void removePCBFromQueue(struct PCB *p1);
void switchPCBs(struct PCB *p1, struct PCB *p2);
void placePCBAtStartOfDLL(struct PCB *p1);
void placePCBAtEndOfDLL(struct PCB *p1);


/*** FUNCTION FOR SCHEDULER ***/

/**
 * @brief This function intializes the ready queue.
 */
void scheduler_init() {    
    // Initialize readyQueue
    readyQueue.head = NULL;
    readyQueue.tail = NULL;

    isRunningWorkers = 0;
    policyGlobal = INVALID_POLICY;
    isTimeToExit = 0;
    isThereWorkToDo = 0;
    startExitProcedure = 0;
    finishedWork = 0;

    pthread_mutex_init(&readyQueueLock, NULL);
    pthread_mutex_init(&isThereWorkToDoLock, NULL);
    pthread_cond_init(&isThereWorkToDoCond, NULL);
    pthread_mutex_init(&finishedWorkLock, NULL);
    pthread_cond_init(&finishedWorkCond, NULL);
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
        free(fetchInstruction(line_idx));
    }

    addMemoryAvailability(pcb->memoryStartIdx, pcb->lengthCode);
    free(pcb);
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
int mem_load_script(FILE *p, policy_t policy) {
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
        updateInstruction(line_idx, scriptLines[line_idx - mem_idx]);
    }

    // Initialize new PCB
    newPCB = (struct PCB *)malloc(sizeof(struct PCB));
    newPCB->pid = rand();
    newPCB->memoryStartIdx = mem_idx;
    newPCB->lengthCode = scriptLength;
    newPCB->lengthScore = scriptLength;
    newPCB->programCounter = mem_idx;
    newPCB->next = NULL;
    newPCB->prev = NULL;

    pthread_mutex_lock(&readyQueueLock);
    if(policy == INVALID_POLICY){
        if (readyQueue.head){
            readyQueue.head->prev = newPCB;
            newPCB->next = readyQueue.head;
        }
        readyQueue.head = newPCB;
        if (!readyQueue.tail){
            readyQueue.tail = readyQueue.head;
            readyQueue.tail->next = NULL;
        }
    } else if(policy == SJF || policy == AGING){
        insertPCBFromTailSJF(newPCB);
    } else {
        if (readyQueue.tail) {
            readyQueue.tail->next = newPCB;
            newPCB->prev = readyQueue.tail;
        }
        readyQueue.tail = newPCB;
        if (!readyQueue.head) {
            readyQueue.head = readyQueue.tail;
            readyQueue.head->prev = NULL;
        }
    }
    pthread_mutex_unlock(&readyQueueLock);

    return 0;
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
            convertInputToOneLiners(fetchInstruction(line_idx));
        }
        deallocateMemoryScript(currentPCB);
    }
}


/*** FUNCTION FOR EXECUTING THE SCRIPTS ***/

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
            convertInputToOneLiners(fetchInstruction(line_idx));
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
    struct PCB *currentPCB, *tmp, *smallest, *currentHead;
    int line_idx, programCounterTmp, needToSwitch = 0;
    
    currentPCB = popHeadFromPCBQueue();
    while (currentPCB) {

        // Time slice
        convertInputToOneLiners(
            fetchInstruction(currentPCB->programCounter));
        currentPCB->programCounter++;

        // Aging all processes
        pthread_mutex_lock(&readyQueueLock);
        tmp = readyQueue.head;
        while (tmp) {
            // Make sure that the length score is not null
            if (tmp->lengthScore) {
                tmp->lengthScore--;
            }
            tmp = tmp->next;
        }
        pthread_mutex_unlock(&readyQueueLock);

        // Check if process has stopped running
        if (currentPCB->programCounter ==
            currentPCB->memoryStartIdx + currentPCB->lengthCode) {
            deallocateMemoryScript(currentPCB);
            currentPCB = popHeadFromPCBQueue();
        } else{ // Preempt the head if it has a bigger score than other processes
            pthread_mutex_lock(&readyQueueLock);
            if (readyQueue.head && readyQueue.head->lengthScore < currentPCB->lengthScore){
                insertPCBFromTailSJF(currentPCB);
                needToSwitch = 1;
            }
            pthread_mutex_unlock(&readyQueueLock);
            if (needToSwitch) {
                currentPCB = popHeadFromPCBQueue();
                needToSwitch = 0;
            }
        }
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
    int startWorkerExitProcedure = 0;
    policy_t workerPolicy;

    while (1) {
        pthread_mutex_lock(&isThereWorkToDoLock);
        while (!isTimeToExit && !isThereWorkToDo){
            pthread_cond_wait(&isThereWorkToDoCond, &isThereWorkToDoLock);
        }

        if(isThereWorkToDo){
            isThereWorkToDo--;
            workerPolicy = policyGlobal;
        }
        if (isTimeToExit){
            startWorkerExitProcedure = 1;
        }
        pthread_mutex_unlock(&isThereWorkToDoLock);

        if (startWorkerExitProcedure) {
            free(workerID);
            pthread_exit(NULL);
        }
        selectSchedule(workerPolicy);
        
        pthread_mutex_lock(&finishedWorkLock);
        finishedWork++;
        pthread_cond_signal(&finishedWorkCond);
        pthread_mutex_unlock(&finishedWorkLock);
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
    int line_idx, programCounterTmp, startMainExitProcedure = 0;

    if (isRunningConcurrently && !isRunningWorkers) {
        for (int i = 0; i < WORKERS_NUMBER; i++) {
            int *workerId = (int *) malloc(sizeof(int));
            *workerId = i;
            pthread_create(&workers[i], NULL, workerThread, workerId);
        }
        isRunningWorkers = 1;
    }

    if (isRunningConcurrently) {
        pthread_mutex_lock(&isThereWorkToDoLock);
        policyGlobal = policy;
        isThereWorkToDo = 2;
        pthread_cond_broadcast(&isThereWorkToDoCond);
        pthread_mutex_unlock(&isThereWorkToDoLock);

        // Wait for the worker threads
        pthread_mutex_lock(&finishedWorkLock);
        while (!startExitProcedure && finishedWork < 2){
            pthread_cond_wait(&finishedWorkCond, &finishedWorkLock);
        }

        if (finishedWork >= 2) {
            finishedWork = 0;
        }

        if(startExitProcedure) {
            startMainExitProcedure = 1;
        }
        pthread_mutex_unlock(&finishedWorkLock);

        // Check if quit called in threads
        if (startMainExitProcedure) {
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
        pthread_mutex_lock(&isThereWorkToDoLock);
        isTimeToExit = 1;
        pthread_cond_broadcast(&isThereWorkToDoCond);
        pthread_mutex_unlock(&isThereWorkToDoLock);
        
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

void insertPCBFromTailSJF(struct PCB *pcb){
    struct PCB *currentPCB;
    int wasInserted = 0;

    // Case where the readyQueue is empty
    if(readyQueue.tail == NULL) {
        readyQueue.tail = pcb;
        readyQueue.tail->next = NULL;
        readyQueue.tail->prev = NULL;
        readyQueue.head = readyQueue.tail;
        wasInserted = 1;
    }

    if(!wasInserted){
        // Otherwise, we iterate until we find a pcb with a lengthScore higher than the pcb to insert
        currentPCB = readyQueue.tail;
        while(currentPCB){
            if(pcb->lengthScore >= currentPCB->lengthScore){
                pcb->prev = currentPCB;
                pcb->next = currentPCB->next;
                if(currentPCB->next){
                    currentPCB->next->prev = pcb;
                }
                currentPCB->next = pcb;

                if(currentPCB == readyQueue.tail){
                    readyQueue.tail = pcb;
                }
                wasInserted = 1;
                break;
            }
            currentPCB = currentPCB->prev;
        }
    }
    

    // If the pcb wasn't inserted, that means that it has the smallest lengthscore and should be the new head
    if(!wasInserted){
        pcb->next = readyQueue.head;
        pcb->prev = NULL;
        readyQueue.head->prev = pcb;
        readyQueue.head = pcb;
    }
}


/*** HELPER FUNCTIONS ***/

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


/**
 * @brief Inserts a PCB at the start (head) of a doubly linked list.
 *
 * This function takes a pointer to a PCB structure and appends it to the start (head) of the 
 * readyQueue doubly linked list.
 *
 * @param p1 A pointer to the PCB structure to be placed at the end of the linked list.
 */
void placePCBAtStartOfDLL(struct PCB *p1) {
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
        readyQueue.head->prev = p1;
        p1->next = readyQueue.head;
        p1->prev = NULL;
        readyQueue.head = p1;
    }
    pthread_mutex_unlock(&readyQueueLock);
}
