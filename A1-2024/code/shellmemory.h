#include <stdio.h>

#define MAX_VALUE_SIZE 5
#define MAX_TOKEN_SIZE 200
#define MAX_VARIABLE_VALUE_SIZE \
    ((MAX_VALUE_SIZE * MAX_TOKEN_SIZE) + MAX_VALUE_SIZE)
    
#ifndef VAR_MEMSIZE
#define VAR_MEMSIZE 10
#endif

void mem_init();
void mem_get_value(char *var, char *buffer);
void mem_set_value(char *var_in, char *values_in[], int number_values);
int mem_get_variable_index(char *var_in);
