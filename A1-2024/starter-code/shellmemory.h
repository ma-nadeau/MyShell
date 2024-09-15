#define MEM_SIZE 1000
#define MAX_VALUE_SIZE 5
#define MAX_TOKEN_SIZE 200
#define MAX_VARIABLE_VALUE_SIZE \
    ((MAX_VALUE_SIZE * MAX_TOKEN_SIZE) + MAX_VALUE_SIZE)

void mem_init();
void mem_get_value(char *var, char *buffer);
void mem_set_value(char *var_in, char *values_in[], int number_values);
