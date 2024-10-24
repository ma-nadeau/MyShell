#include <stdio.h>
#include <pthread.h>
#define WORKERS_NUMBER 2

typedef enum policy_t {
    FCFS = 0,
    SJF,
    RR,
    RR30,
    AGING,
    INVALID_POLICY
} policy_t;

extern int startExitProcedure;

void scheduler_init();
int mem_load_script(FILE *p);
void schedulerRun(policy_t policy, int isRunningBackground, int isRunningInBackground);
void joinAllThreads();
int isMainThread(pthread_t runningPthread);
