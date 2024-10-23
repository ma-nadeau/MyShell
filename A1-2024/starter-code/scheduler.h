#include <stdio.h>
#define WORKERS_NUMBER 2

typedef enum policy_t {
    FCFS = 0,
    SJF,
    RR,
    RR30,
    AGING,
    INVALID_POLICY
} policy_t;

void scheduler_init();
int mem_load_script(FILE *p);
void schedulerRun(policy_t policy, int isRunningBackground, int isRunningInBackground);
void joinAllThreads();
