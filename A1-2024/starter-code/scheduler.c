#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "scheduler.h"
#include "scriptsmemory.h"
#include "shell.h"
#include "shellmemory.h"

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
void detachPCBFromQueue(struct PCB *p1);
struct PCB *popHeadFromPCBQueue();
void placePCBAtEndOfDLL(struct PCB *p1);

/**
 * This function intializes the ready queue and associated required resources.
 */
void scheduler_init() {
    // Initialize readyQueue
    readyQueue.head = NULL;
    readyQueue.tail = NULL;

    // Initialize global variables
    isRunningWorkers = 0;
    policyGlobal = INVALID_POLICY;
    isTimeToExit = 0;
    isThereWorkToDo = 0;
    startExitProcedure = 0;
    finishedWork = 0;

    // Initialize concurrency variables
    pthread_mutex_init(&readyQueueLock, NULL);
    pthread_mutex_init(&isThereWorkToDoLock, NULL);
    pthread_cond_init(&isThereWorkToDoCond, NULL);
    pthread_mutex_init(&finishedWorkLock, NULL);
    pthread_cond_init(&finishedWorkCond, NULL);
}

/**
 * This function frees the memory that was allocated for the script linked
 * to the specified process control block (PCB).
 *
 * @param pcb A pointer to the PCB structure that contains the script whose
 * memory is to be deallocated.
 * @return void This function does not return a value.
 */
void terminateProcess(struct PCB *pcb) {
    pcb->scriptInfo->PCBsInUse--;
    free(pcb);
}

/**
 * This function takes the name of a script file and creates a process for it.
 *
 * @param script A pointer to the name of the script to be loaded.
 * @return Returns a non-null integer for an error and 0 otherwise
 */
int mem_load_script(char script[], policy_t policy) {
    char line[MAX_USER_INPUT];
    int scriptLength = 0, line_idx, mem_idx, pageIdx;
    FILE *p;
    
    // A null script signals that the stdin (background execution)
    // is to be loaded
    if (!script){
        p = stdin;
    } else{
        p = fopen(script, "rt");  // the program is in a file
    }

    // Make sure the given File is valid
    if (p == NULL) {
        return -1;
    }

    // Count the number of lines in the script
    while (1) {
        fgets(line, MAX_USER_INPUT - 1, p);
        scriptLength++;

        if (feof(p)) {
            break;
        }
    }
    fclose(p);

    // Initialize the page table and related information
    struct scriptFrames *scriptInfo = (struct scriptFrames *)malloc(sizeof(struct scriptFrames));
    scriptInfo->scriptName = strdup(script);
    scriptInfo->lengthCode = scriptLength;
    scriptInfo->PCBsInUse = 0;
    scriptInfo->FramesInUse = 0;
    for(pageIdx = 0; pageIdx < PAGE_TABLE_SIZE; pageIdx++){
        scriptInfo->pageTable[pageIdx] = -1;
    }

    // Assign the first few pages of the script to frames
    for (pageIdx = 0; pageIdx < PAGES_LOADED_NUMBER; pageIdx++){
        pageAssignment(pageIdx, scriptInfo, 1);
    }

    createPCB(policy, scriptInfo);
    
    return 0;
}

/**
 * Creates a new PCB for a process and inserts it into the ready queue.
 * 
 * @param policy The scheduling policy to be used.
 * @param scriptInfo The struct containing the page table associated with a script
*/
void createPCB(policy_t policy, struct scriptFrames *scriptInfo){
    struct PCB *newPCB;
    int pageIdx;

    // Initialize new PCB for new process being created
    newPCB = (struct PCB *)malloc(sizeof(struct PCB));
    newPCB->pid = rand();
    newPCB->lengthScore = scriptInfo->lengthCode;
    newPCB->virtualAddress = 0;
    newPCB->scriptInfo = scriptInfo;
    newPCB->scriptInfo->PCBsInUse++;
    // Initialize readyQueue related fields
    newPCB->next = NULL;
    newPCB->prev = NULL;

    // Insert into the PCB readyQueue differently depending on policy
    pthread_mutex_lock(&readyQueueLock);
    // The INVALID_POLICY is used to load the main shell program
    // (when # is used) at the start of the ready queue
    if (policy == INVALID_POLICY) {
        if (readyQueue.head) {
            readyQueue.head->prev = newPCB;
            newPCB->next = readyQueue.head;
        }
        readyQueue.head = newPCB;
        if (!readyQueue.tail) {
            readyQueue.tail = readyQueue.head;
            readyQueue.tail->next = NULL;
        }
    // For the SJF and AGING policy, the PCB is inserted
    // by comparing the "lengthScore" so that the PCB
    // with min. lengthScore is at the head of the queue
    } else if (policy == SJF || policy == AGING) {
        insertPCBFromTailSJF(newPCB);
    // In all the other cases (RR, FCFS), the PCB is inserted
    // at the end of the queue
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
}

/*** FUNCTIONS FOR EXECUTING THE SCRIPTS ***/

/**
 * This function iterates through the ready queue of Process Control Blocks
 * (PCBs) and executes each PCB one after the other starting at the head (i.e.,
 * head, head->next, ..., tail).
 * This function is used for FCFS, and SJF
 * 
 * @param policy The scheduling policy to determine how to insert preempted PCBs back in the queue
 */
void executeReadyQueuePCBs(policy_t policy) {
    int line_idx;
    struct PCB *currentPCB;
    char *instr;

next_timeslice_execute:
    while ((currentPCB = popHeadFromPCBQueue())) {
        // Execute all lines of code
        for (line_idx = currentPCB->virtualAddress;
             line_idx < currentPCB->scriptInfo->lengthCode;
             line_idx++, currentPCB->virtualAddress++) {
                // Attempt to fetch next instruction
                if (instr = fetchInstructionVirtual(line_idx, currentPCB->scriptInfo)){
                    convertInputToOneLiners(instr);
                } else { // Fix page fault and preempt the process
                    pageAssignment(line_idx/PAGE_SIZE, currentPCB->scriptInfo, 0);

                    // Reinsert preempted page faulted PCBs depending on policy
                    if (policy == FCFS){
                        placePCBAtEndOfDLL(currentPCB);
                    } else {
                        insertPCBFromTailSJF(currentPCB);
                    }
                    goto next_timeslice_execute;
                }
        }
        terminateProcess(currentPCB);
    }
}

/**
 * Executes the Round Robin (RR) scheduling policy. This function implements the
 * Round Robin (RR) scheduling algorithm for a set of processes based on the
 * specified line number.
 *
 * @param lineNumber The number of line to execute before switching processes.
 */
void runRR(int lineNumber) {
    struct PCB *currentPCB;
    int line_idx, programCounterTmp;
    char *instr;

next_timeslice_RR:
    while ((currentPCB = popHeadFromPCBQueue())) {
        // Execute lineNumber lines of code
        programCounterTmp = currentPCB->virtualAddress;
        for (line_idx = currentPCB->virtualAddress;
             line_idx < currentPCB->scriptInfo->lengthCode &&
             line_idx < programCounterTmp + lineNumber;
             line_idx++, currentPCB->virtualAddress++) {
                // Attempt to fetch next instruction
                if (instr = fetchInstructionVirtual(line_idx, currentPCB->scriptInfo)){
                    convertInputToOneLiners(instr);
                } else { // Fix page fault and preempt the process
                    pageAssignment(line_idx/PAGE_SIZE, currentPCB->scriptInfo, 0);
                    placePCBAtEndOfDLL(currentPCB);
                    goto next_timeslice_RR;
                }
        }
        // Check if process has finished running
        if (currentPCB->virtualAddress ==
            currentPCB->scriptInfo->lengthCode) {
            terminateProcess(currentPCB);
        } else {
            placePCBAtEndOfDLL(currentPCB);
        }
    }
}

/**
 *Executes the Aging scheduling policy.This function implements the Aging
 *scheduling algorithm for the process in the readyQueue
 */
void runAging() {
    struct PCB *currentPCB, *tmp, *smallest, *currentHead;
    int line_idx, programCounterTmp, needToSwitch = 0;
    char *instr;

    currentPCB = popHeadFromPCBQueue();
    while (currentPCB) {
        // Time slice
        // Attempt to fetch next instruction
        if (instr = fetchInstructionVirtual(currentPCB->virtualAddress, currentPCB->scriptInfo)){
            convertInputToOneLiners(instr);
        } else { // Fix page fault and preempt the process
            pageAssignment(currentPCB->virtualAddress/PAGE_SIZE, currentPCB->scriptInfo, 0);
            insertPCBFromTailSJF(currentPCB);
            currentPCB = popHeadFromPCBQueue();
            continue;
        }
        currentPCB->virtualAddress++;

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
        if (currentPCB->virtualAddress ==
            currentPCB->scriptInfo->lengthCode) {
            terminateProcess(currentPCB);
            currentPCB = popHeadFromPCBQueue();
        } else {  // Preempt the head if it has a bigger score than other
                  // processes
            pthread_mutex_lock(&readyQueueLock);
            if (readyQueue.head &&
                readyQueue.head->lengthScore < currentPCB->lengthScore) {
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
 * Selects the scheduling strategy based on the specified policy.This function
 * takes a scheduling policy as input and runs the readyQueue accordingly.
 *
 * @param policy A value of type `policy_t` that represents the scheduling
 * policy to be used. (i.e., FCFS, SJF, RR, RR30, AGING)
 */
void selectSchedule(policy_t policy) {
    switch (policy) {
        // Since the readyQueue for SJF was sorted in schedulerRun beforehand
        // when inserting, it becomes the same as FCFS
        case FCFS:
        case SJF:
            executeReadyQueuePCBs(policy);
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
 * The main function for the worker thread. This function runs in a loop,
 * waiting for work to be available via condition variables. When work is signaled, it
 * selects the scheduling policy to manage process execution. The loop continues
 * until a termination signal is received.
 */
void *workerThread(void *args) {
    int startWorkerExitProcedure = 0;
    policy_t workerPolicy;

    while (1) {
        // Wait for workToDo or termination signal
        pthread_mutex_lock(&isThereWorkToDoLock);
        while (!isTimeToExit && !isThereWorkToDo) {
            pthread_cond_wait(&isThereWorkToDoCond, &isThereWorkToDoLock);
        }

        // Work to do case
        if (isThereWorkToDo) {
            isThereWorkToDo--;
            workerPolicy = policyGlobal;
        }
        // Termination case
        if (isTimeToExit) {
            startWorkerExitProcedure = 1;
        }
        pthread_mutex_unlock(&isThereWorkToDoLock);

        // Exit procedure and thread termination
        // in the case where the main thread signaled for termination
        if (startWorkerExitProcedure) {
            pthread_exit(NULL);
        }
        selectSchedule(workerPolicy);

        // Signal the main thread that worker finished working
        pthread_mutex_lock(&finishedWorkLock);
        finishedWork++;
        pthread_cond_signal(&finishedWorkCond);
        pthread_mutex_unlock(&finishedWorkLock);
    }
}

/**
 * Executes the scheduling process based on the specified policy and
 * execution modes. This function initiates the scheduling of processes
 * according to the provided policy. It manages the cases where the exec
 * is run in the background (#) and the use of concurrent threads (MT),
 * adjusting the scheduling behavior accordingly.
 *
 * @param policy A value of type 'policy_t' representing the scheduling policy
 * to be applied.
 * @param isRunningBackground An integer flag indicating if exec
 * should execute in the background. (1 if True, 0 if False)
 * @param isRunningConcurrently An integer flag indicating if processes should
 * run concurrently. (1 if True, 0 if False)
 */
void schedulerRun(policy_t policy, int isRunningBackground,
                  int isRunningConcurrently) {
    struct PCB *currentPCB, *smallest, *currentHead;
    int line_idx, programCounterTmp, startMainExitProcedure = 0;

    // For the concurrency case, start the threads if they weren't
    // already running
    if (isRunningConcurrently && !isRunningWorkers) {
        for (int i = 0; i < WORKERS_NUMBER; i++) {
            pthread_create(&workers[i], NULL, workerThread, NULL);
        }
        isRunningWorkers = 1;
    }

    // Concurrency enabled case
    if (isRunningConcurrently) {
        // Signal the threads to start working
        // according to the policyGlobal
        pthread_mutex_lock(&isThereWorkToDoLock);
        policyGlobal = policy;
        isThereWorkToDo = 2;
        pthread_cond_broadcast(&isThereWorkToDoCond);
        pthread_mutex_unlock(&isThereWorkToDoLock);

        // Wait for the worker threads to finish
        pthread_mutex_lock(&finishedWorkLock);
        while (!startExitProcedure && finishedWork < 2) {
            pthread_cond_wait(&finishedWorkCond, &finishedWorkLock);
        }

        // Reset finishedWork variable if applicable
        // for the next time the workers are running
        if (finishedWork >= 2) {
            finishedWork = 0;
        }

        // Check if a 'quit' command was called from a
        // worker thread
        if (startExitProcedure) {
            startMainExitProcedure = 1;
        }
        pthread_mutex_unlock(&finishedWorkLock);

        
        if (startMainExitProcedure) {
            joinAllThreads();
            exit(0);
        }
    } else {
        selectSchedule(policy);
    }
}

/*** HELPER FUNCTIONS */

/**
 * Waits for all running worker threads to finish execution. This function sets
 * a termination flag and signals the worker threads to exit. It then calls
 * 'pthread_join' for each thread, ensuring the main thread waits for their
 * completion before proceeding.
 *
 * @return void
 */
void joinAllThreads() {
    if (isRunningWorkers) {
        // Signal worker threads to terminate
        pthread_mutex_lock(&isThereWorkToDoLock);
        isTimeToExit = 1;
        pthread_cond_broadcast(&isThereWorkToDoCond);
        pthread_mutex_unlock(&isThereWorkToDoLock);

        // Join all threads
        for (int i = 0; i < WORKERS_NUMBER; i++) {
            pthread_join(workers[i], NULL);
        }
    }
}

/** 
 * This predicate function returns whether the runningPthread is the main thread
 *
 * @param runningPthread The thread ID (of type pthread_t) to check.
 * @return Returns 1 if the specified thread is the main thread; returns 0 if it
 * matches a worker thread.
 */
int isMainThread(pthread_t runningPthread) {
    for (int i = 0; i < WORKERS_NUMBER; i++) {
        if (pthread_equal(runningPthread, workers[i])) {
            return 0;
        }
    }

    return 1;
}

/** This function adds a new PCB to the end of the ready queue which is being
 * used in  Shortest Job First (SJF) scheduling policy.
 *
 * @param pcb A pointer to the PCB structure representing the process to be
 * added to the queue.
 */
void insertPCBFromTailSJF(struct PCB *pcb) {
    struct PCB *currentPCB;
    int wasInserted = 0;

    // Case where the readyQueue is empty
    if (readyQueue.tail == NULL) {
        readyQueue.tail = pcb;
        readyQueue.tail->next = NULL;
        readyQueue.tail->prev = NULL;
        readyQueue.head = readyQueue.tail;
        wasInserted = 1;
    }

    if (!wasInserted) {
        // Otherwise, we iterate/propagate backwards until we find a pcb
        // with a lengthScore higher than the pcb to insert
        currentPCB = readyQueue.tail;
        while (currentPCB) {
            if (pcb->lengthScore >= currentPCB->lengthScore) {
                pcb->prev = currentPCB;
                pcb->next = currentPCB->next;
                if (currentPCB->next) {
                    currentPCB->next->prev = pcb;
                }
                currentPCB->next = pcb;

                if (currentPCB == readyQueue.tail) {
                    readyQueue.tail = pcb;
                }
                wasInserted = 1;
                break;
            }
            currentPCB = currentPCB->prev;
        }
    }

    // If the pcb wasn't inserted, that means that it has the smallest
    // lengthscore and should be the new head
    if (!wasInserted) {
        pcb->next = readyQueue.head;
        pcb->prev = NULL;
        readyQueue.head->prev = pcb;
        readyQueue.head = pcb;
    }
}

/**
 * This function detaches the specified Process Control Block (PCB) from the
 * ready queue and reattaches the previous and next nodes (if any) together.
 *
 * @param pcb A pointer to the PCB to be removed from the queue.
 * 
 * @return void
 */
void detachPCBFromQueue(struct PCB *pcb) {
    // Case where pcb is at the head
    if (readyQueue.head == pcb) {
        readyQueue.head = readyQueue.head->next;
        // Check if there are any PCBs left in the queue
        if (readyQueue.head) {
            readyQueue.head->prev = NULL;
        } else {
            // If not then update the tail
            readyQueue.tail = NULL;
        }
    // Case where pcb is at the tail
    } else if (readyQueue.tail == pcb) {
        readyQueue.tail = readyQueue.tail->prev;

        if (readyQueue.tail) {
            readyQueue.tail->next = NULL;
        }
    // Generic case where pcb is in the middle of the queue
    } else {
        pcb->prev->next = pcb->next;
        pcb->next->prev = pcb->prev;
    }

    // Remove all attachment (free from desire)
    pcb->next = NULL;
    pcb->prev = NULL;
}

/**
 * This function retrieves and removes the first PCB from the PCB queue.
 * If the queue is empty, it returns NULL.
 *
 * @return A pointer to the PCB that was removed from the head of the queue,
 *         or NULL if the queue is empty.
 */
struct PCB *popHeadFromPCBQueue() {
    struct PCB *rv;

    pthread_mutex_lock(&readyQueueLock);
    if (readyQueue.head) {
        rv = readyQueue.head;
        detachPCBFromQueue(readyQueue.head);
    } else {
        rv = NULL;
    }
    pthread_mutex_unlock(&readyQueueLock);

    return rv;
}

/**
 * This function takes a pointer to a PCB structure and appends it to the end
 * (tail) of the readyQueue doubly linked list.
 *
 * @param pcb A pointer to the PCB structure to be placed at the end of the
 * linked list.
 */
void placePCBAtEndOfDLL(struct PCB *pcb) {
    pthread_mutex_lock(&readyQueueLock);
    // Check for case where list is empty
    if (!readyQueue.head) {
        readyQueue.head = pcb;
        readyQueue.tail = pcb;
        pcb->next = NULL;
        pcb->prev = NULL;
    // Update the tail only otherwise
    } else {
        readyQueue.tail->next = pcb;
        pcb->prev = readyQueue.tail;
        pcb->next = NULL;
        readyQueue.tail = pcb;
    }
    pthread_mutex_unlock(&readyQueueLock);
}
