#include <stdio.h>
#include <pthread.h>

#include "scriptsmemory.h"

#define WORKERS_NUMBER 2

typedef enum policy_t {
    FCFS = 0,
    SJF,
    RR,
    RR30,
    AGING,
    INVALID_POLICY
} policy_t;

struct PCB {
    int pid;
    int memoryStartIdx;
    int lengthCode;
    int lengthScore;
    int virtualAddress;
    struct PCB *next;
    struct PCB *prev;
    int pageTable[FRAME_STORE_SIZE];
};

extern int startExitProcedure;
extern pthread_mutex_t finishedWorkLock;
extern pthread_cond_t finishedWorkCond;


void scheduler_init();
struct PCB *mem_load_script(FILE *p, policy_t policy);
void schedulerRun(policy_t policy, int isRunningBackground, int isRunningInBackground);
void joinAllThreads();
int isMainThread(pthread_t runningPthread);
struct PCB *createPCB(policy_t policy, int mem_idx, int scriptLength);
