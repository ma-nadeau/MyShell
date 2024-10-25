#include "shellmemory.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shell.h"

struct memory_struct {
    char *var;
    char *value[MAX_VALUE_SIZE];
};

pthread_mutex_t memoryVariableArrayLock;

struct memory_struct shellmemory[MEM_SIZE];

/** SHELL MEMORY FUNCTIONS */

/**
 * This function sets up the necessary memory structures and resources required
 * for the shell's operation.
 *
 * @return void
 */
void mem_init() {
    // Initialize variable and code shellmemory
    int mem_idx, val_idx;
    for (mem_idx = 0; mem_idx < MEM_SIZE; mem_idx++) {
        shellmemory[mem_idx].var = NULL;
        for (val_idx = 0; val_idx < MAX_VALUE_SIZE; val_idx++) {
            shellmemory[mem_idx].value[val_idx] = NULL;
        }
    }

    pthread_mutex_init(&memoryVariableArrayLock, NULL);
}

/**
 * This function takes the memory index of a variable and clears its value,
 * effectively resetting it to an uninitialized state.
 *
 * @param mem_idx The memory index of the variable whose value is to be cleared.
 * @return void
 */
void mem_clear_value(int mem_idx) {
    int val_idx = 0;
    while (val_idx < MAX_VALUE_SIZE &&
           shellmemory[mem_idx].value[val_idx] != NULL) {
        free(shellmemory[mem_idx].value[val_idx]);
        shellmemory[mem_idx].value[val_idx] = NULL;
        val_idx++;
    }
    return;
}

/**
 * This function takes a variable name and an array of values, associating the
 * values with the given variable name in memory. The number of values is
 * specified by the 'number_values' parameter.
 *
 * @param var_in A pointer to a string representing the variable name (key) to
 * be set.
 * @param values_in An array of strings representing the values to be associated
 * with the variable.
 * @param number_values The number of values in the `values_in` array.
 *
 * @return void
 */
void mem_set_value(char *var_in, char *values_in[], int number_values) {
    int mem_idx, val_idx, wasSet = 0;

    // If variable exists, we overwrite the values
    for (mem_idx = 0; mem_idx < MEM_SIZE; mem_idx++) {
        pthread_mutex_lock(&memoryVariableArrayLock);
        // Check to see if memory spot was not initialized (in which case we
        // reached the end of initialized variables) or whether the memory spot
        // corresponds to the variable passed as argument
        if (shellmemory[mem_idx].var == NULL ||
            strcmp(shellmemory[mem_idx].var, var_in) == 0) {
            if (shellmemory[mem_idx].var ==
                NULL) {  // Case where we reached end of known variables
                // Create our new variable in this spot
                shellmemory[mem_idx].var = strdup(var_in);
            } else {  // Case where variable already existed
                // clear the old values of variable appropriately
                mem_clear_value(mem_idx);
            }
            // In either case we populate the new values
            for (val_idx = 0; val_idx < number_values; val_idx++) {
                shellmemory[mem_idx].value[val_idx] =
                    strdup(values_in[val_idx]);
            }
            wasSet = 1;
        }
        pthread_mutex_unlock(&memoryVariableArrayLock);
        if (wasSet) {
            break;
        }
    }
}

/**
 * This function takes a variable name as input and searches for its
 * corresponding index in memory. It returns the index if the variable is found,
 * or -1 if the variable does not exist in memory.
 *
 * @param var_in A pointer to a string representing the variable name to be
 * searched.
 * @return Returns the index of the variable entry on success, or -1 if the
 * variable is not found.
 */
int mem_get_variable_index(char *var_in) {
    int mem_idx, val_idx;

    for (mem_idx = 0; mem_idx < MEM_SIZE; mem_idx++) {
        // Case where we reached end of initialized memory
        if (shellmemory[mem_idx].var == NULL) {
            return -1;
        }
        if (strcmp(shellmemory[mem_idx].var, var_in) == 0) {
            return mem_idx;
        }
    }
    return -1;
}

/**
 * This function takes an input key (variable name) and retrieves its
 * corresponding value from memory, storing the result in the provided buffer.
 *
 * @param var_in A pointer to a string representing the input key (variable
 * name).
 * @param buffer A pointer to a buffer where the retrieved value will be stored.
 *
 * @return void
 */
void mem_get_value(char *var_in, char *buffer) {
    int mem_idx, val_idx;
    char *space = " ";

    // Assume there is an error
    strcpy(buffer, "Variable does not exist");

    mem_idx = mem_get_variable_index(var_in);
    if (mem_idx > -1) {
        // Assemble the values
        buffer[0] = '\0';  // Initialize buffer with null character
        val_idx = 0;
        while (val_idx < MAX_VALUE_SIZE &&
               shellmemory[mem_idx].value[val_idx] != NULL) {
            if (val_idx != 0) {
                strcat(buffer, space);
            }
            strcat(buffer, shellmemory[mem_idx].value[val_idx]);
            val_idx++;
        }
        return;
    }
    return;
}
