#include <stdio.h>
#include <pthread.h>

#define WORKERS_NUMBER 2
#define PAGES_LOADED_NUMBER 2

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
    int lengthScore;
    int virtualAddress;
    struct scriptFrames *scriptInfo;
    struct PCB *next;
    struct PCB *prev;
};

extern int startExitProcedure;
extern pthread_mutex_t finishedWorkLock;
extern pthread_cond_t finishedWorkCond;


void scheduler_init();
int mem_load_script(char script[], policy_t policy);
void schedulerRun(policy_t policy, int isRunningBackground, int isRunningInBackground);
void joinAllThreads();
int isMainThread(pthread_t runningPthread);
void createPCB(policy_t policy, struct scriptFrames *scriptInfo);
