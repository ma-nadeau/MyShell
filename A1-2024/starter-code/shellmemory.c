#include "shellmemory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct memory_struct {
    char *var;
    char *value[MAX_VALUE_SIZE];
};

struct memory_struct shellmemory[MEM_SIZE];

// Helper functions
int match(char *model, char *var) {
    int i, len = strlen(var), matchCount = 0;
    for (i = 0; i < len; i++) {
        if (model[i] == var[i]) matchCount++;
    }
    if (matchCount == len) {
        return 1;
    } else {
        return 0;
    }
}

// Shell memory functions

void mem_init() {
    int mem_idx, val_idx;
    for (mem_idx = 0; mem_idx < MEM_SIZE; mem_idx++) {
        shellmemory[mem_idx].var = NULL;
        for (val_idx = 0; val_idx < MAX_VALUE_SIZE; val_idx++) {
            shellmemory[mem_idx].value[val_idx] = NULL;
        }
    }
}

// clear value of a variable
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

// Set key value pair
void mem_set_value(char *var_in, char *values_in[], int number_values) {
    int mem_idx, val_idx;

    // If variable exists, we overwrite the values
    for (mem_idx = 0; mem_idx < MEM_SIZE; mem_idx++) {
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
            return;
        }
    }

    // out of memory here
    return;
}

// get value based on input key
void mem_get_value(char *var_in, char *buffer) {
    int mem_idx, val_idx;
    char *space = " ";

    // Assume there is an error
    strcpy(buffer, "Variable does not exist");

    for (mem_idx = 0; mem_idx < MEM_SIZE; mem_idx++) {
        // Case where we reached end of initialized memory
        if (shellmemory[mem_idx].var == NULL) {
            return;
        }
        if (strcmp(shellmemory[mem_idx].var, var_in) == 0) {
            // Assemble the values
            buffer[0] = '\0';  // Initialize buffer with null character
            val_idx = 0;
            while (val_idx < MAX_VALUE_SIZE && shellmemory[mem_idx].value[val_idx] != NULL) {
                if (val_idx != 0) {
                    strcat(buffer, space);
                }
                strcat(buffer, shellmemory[mem_idx].value[val_idx]);
                val_idx++;
            }
            return;
        }
    }
    return;
}
