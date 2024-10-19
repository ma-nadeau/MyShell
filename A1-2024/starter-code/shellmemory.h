#include <stdio.h>

#define MEM_SIZE 1000
#define MAX_VALUE_SIZE 5
#define MAX_TOKEN_SIZE 200
#define MAX_VARIABLE_VALUE_SIZE \
    ((MAX_VALUE_SIZE * MAX_TOKEN_SIZE) + MAX_VALUE_SIZE)
#define WORKERS_NUMBER 2

typedef enum policy_t {
    FCFS = 0,
    SJF,
    RR,
    RR30,
    AGING,
    INVALID_POLICY
} policy_t;

void mem_init();
void mem_get_value(char *var, char *buffer);
void mem_set_value(char *var_in, char *values_in[], int number_values);
int mem_get_variable_index(char *var_in);

int mem_load_script(FILE *p);
void schedulerRun(policy_t policy, int isRunningBackground, int isRunningInBackground);
void joinAllThreads();
