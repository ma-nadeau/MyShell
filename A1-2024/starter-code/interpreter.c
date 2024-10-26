#include <ctype.h>
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "scheduler.h"
#include "shell.h"
#include "shellmemory.h"

// Max arg size for a single command (name of the command inclusive)
int MAX_ARGS_SIZE = 7;

typedef enum commandError_t {
    COMMAND_ERROR_BAD_COMMAND = 1,
    COMMAND_ERROR_TOO_MANY_TOKENS,
    COMMAND_ERROR_FILE_INEXISTENT,
    COMMAND_ERROR_MKDIR,
    COMMAND_ERROR_CD,
    COMMAND_ERROR_SCANDIR,
    COMMAND_ERROR_FILE_OPEN,
    COMMAND_ERROR_NON_ALPHANUM,
    COMMAND_ERROR_MEM_LOAD_SCRIPT
} commandError_t;

// Global variable that indicates whether an exec command with '#' was run
// in which case the subsequent exec commands would only load scripts in the
// readyQueue
int execOnlyLoading = 0;

/*** FUNCTION SIGNATURES ***/

int badcommand(commandError_t errorCode);
int help();
int quit();
int set(char *var, char *values[], int number_values);
int print(char *var);
int run(char *script);
int echo(char *input);
int my_ls();
int my_touch(char *input);
int my_mkdir(char *input);
int my_cd(char *input);
int is_alphanumeric(char *str);
int filterOutParentAndCurrentDirectory(const struct dirent *entry);
int custom_sort(const struct dirent **d1, const struct dirent **d2);
int is_alphanumeric_list(char **lst, int len_lst);
policy_t policy_parser(char policy_str[]);
int exec(char *scripts[], int scripts_number, policy_t policy,
         int isRunningInBackground, int isRunningConcurrently);

/**
 * Function that interprets commands and their arguments
 *
 * @param command_args List of command arguments
 * @param args_size Total number of command arguments
 * @return Returns an zero indicating success (0) or non-zero integer indicating
 * an error
 */
int interpreter(char *command_args[], int args_size) {
    int i, isRunningInBackground, isRunningConcurrently;
    policy_t policy;

    // Make sure that the number of arguments isn't out of bounds
    if (args_size < 1) {
        return badcommand(COMMAND_ERROR_BAD_COMMAND);
    } else if (args_size > MAX_ARGS_SIZE) {
        return badcommand(COMMAND_ERROR_TOO_MANY_TOKENS);
    }

    for (i = 0; i < args_size; i++) {  // terminate args at newlines
        command_args[i][strcspn(command_args[i], "\r\n")] = 0;
    }

    if (strcmp(command_args[0], "help") == 0) {
        if (args_size != 1) return badcommand(COMMAND_ERROR_BAD_COMMAND);
        return help();

    } else if (strcmp(command_args[0], "quit") == 0) {
        if (args_size != 1) return badcommand(COMMAND_ERROR_BAD_COMMAND);
        return quit();

    } else if (strcmp(command_args[0], "set") == 0) {
        if (args_size < 3) return badcommand(COMMAND_ERROR_BAD_COMMAND);
        // The case where there are too many values is handled for all commands
        // simultaneously above
        return set(command_args[1], command_args + 2, args_size - 2);

    } else if (strcmp(command_args[0], "print") == 0) {
        if (args_size != 2) return badcommand(COMMAND_ERROR_BAD_COMMAND);
        return print(command_args[1]);

    } else if (strcmp(command_args[0], "run") == 0) {
        if (args_size != 2) return badcommand(COMMAND_ERROR_BAD_COMMAND);
        return run(command_args[1]);

    } else if (strcmp(command_args[0], "echo") == 0) {
        if (args_size != 2) return badcommand(COMMAND_ERROR_BAD_COMMAND);
        return echo(command_args[1]);

    } else if (strcmp(command_args[0], "my_ls") == 0) {
        if (args_size != 1) return badcommand(COMMAND_ERROR_BAD_COMMAND);
        return my_ls();

    } else if (strcmp(command_args[0], "my_touch") == 0) {
        if (args_size != 2) return badcommand(COMMAND_ERROR_BAD_COMMAND);
        return my_touch(command_args[1]);

    } else if (strcmp(command_args[0], "my_mkdir") == 0) {
        if (args_size != 2) return badcommand(COMMAND_ERROR_BAD_COMMAND);
        return my_mkdir(command_args[1]);

    } else if (strcmp(command_args[0], "my_cd") == 0) {
        if (args_size != 2) return badcommand(COMMAND_ERROR_BAD_COMMAND);
        return my_cd(command_args[1]);

    } else if (strcmp(command_args[0], "exec") == 0) {
        // Determine whether to execute the command using multithreading
        isRunningConcurrently = strcmp(command_args[args_size - 1], "MT") == 0 ? 1 : 0;
        // Check if the exec command needs to run the rest of the main shell in
        // the background
        isRunningInBackground =
            strcmp(command_args[args_size - 1 - isRunningConcurrently], "#") == 0 ? 1 : 0;
        // Retrieve the policy associated with the exec command
        policy =
            policy_parser(command_args[args_size - 1 - isRunningConcurrently - isRunningInBackground]);

        if (args_size < 3 || args_size > 7 || policy == INVALID_POLICY)
            return badcommand(COMMAND_ERROR_BAD_COMMAND);

        return exec(
            command_args + 1,
            args_size - 2 - isRunningConcurrently - isRunningInBackground,
            policy, isRunningInBackground, isRunningConcurrently);
    } else {
        return badcommand(COMMAND_ERROR_BAD_COMMAND);
    }
}

/*** FUNCTIONS FOR SHELL COMMANDS ***/

/**
 * Function implementing the help command
 * which prints the list of commands available to the user
 * that the shell provides.
 * 
 * @return Returns an integer indicating success (0)
 */
int help() {
    char help_string[] =
        "COMMAND			DESCRIPTION\n \
help			Displays all the commands\n \
quit			Exits / terminates the shell with “Bye!”\n \
set VAR STRING		Assigns a value to shell memory\n \
print VAR		Displays the STRING assigned to VAR\n \
run SCRIPT.TXT		Executes the file SCRIPT.TXT\n ";
    printf("%s\n", help_string);
    return 0;
}

/**
 * Function implementing the quit command.
 * Note that depending on whether a worker thread or the main thread is calling
 * quit the behaviour differs. Worker threads can only signal the main thread to
 * exit while the main shell immediately starts the exit procedure.
 * 
 * @return Returns an integer indicating success (0)
 */
int quit() {
    printf("Bye!\n");

    // Case where the main thread is exiting
    if (isMainThread(pthread_self())) {
        joinAllThreads();
        exit(0);
    } else {
        // Case where the worker threads signal the main thread to exit
        pthread_mutex_lock(&finishedWorkLock);
        startExitProcedure = 1;
        pthread_cond_signal(&finishedWorkCond);
        pthread_mutex_unlock(&finishedWorkLock);
    }
}

/**
 * Function implementing the set command
 * which associates 5 string values to a variable name.
 * Note that if the variable already exists then the function overwrites the old
 * values.
 *
 * @param var A pointer to the variable name as a string.
 * @param values An array of pointers to the string values to be associated with
 * the variable.
 * @param number_values The number of values to associate (should not exceed 5).
 * @return Returns an integer indicating success (0)
 */
int set(char *var, char *values[], int number_values) {
    char *link = "=";

    // Input validation ensuring the given arguments are strings
    if (!is_alphanumeric(var) || !is_alphanumeric_list(values, number_values)) {
        return badcommand(COMMAND_ERROR_NON_ALPHANUM);
    }

    mem_set_value(var, values, number_values);

    return 0;
}

/**
 * Function implementing the print command
 * which outputs to the user the value of a variable.
 *
 * @param var A pointer to the variable name whose value is to be printed.
 * @return Returns an integer indicating success (0)
 */
int print(char *var) {
    char buffer[MAX_VARIABLE_VALUE_SIZE];
    mem_get_value(var, buffer);
    printf("%s\n", buffer);
    return 0;
}

/**
 * Function implementing the echo command
 * which outputs to the user the given string
 * or the variable associated to the string.
 *
 * @param input A pointer to the string to be echoed, which may be a direct
 * string or a variable name.
 * @return Returns an integer indicating success (0) or non-zero on failure
 */
int echo(char *input) {
    char buffer[MAX_VARIABLE_VALUE_SIZE];  // Buffer to store the variable value

    if (input[0] == '$') {           // Case for variable in memory
        char *var_name = input + 1;  // Ignore the '$'

        // Input validation
        if (!is_alphanumeric(var_name)) {
            return badcommand(COMMAND_ERROR_NON_ALPHANUM);
        }

        mem_get_value(var_name, buffer);  // Retrieve variable value into buffer
        if (strcmp(buffer, "Variable does not exist") != 0 ||
            mem_get_variable_index(var_name) > -1) {
            printf("%s\n", buffer);  // Print the value (empty if not found)
        } else {
            printf("\n");
        }

    } else {  // Case for displaying string on a new line
        printf("%s\n", input);
    }

    return 0;
}

/**
 * Function implementing the ls command
 * which lists the directories and files in the cd.
 *
 * @return Returns an integer indicating success (0) or non-zero on failure
 */
int my_ls() {
    // Pointer to an array of pointers that point to the content
    struct dirent **content;

    // store in content the list of file/folder names in the proper order
    int n = scandir(".", &content, filterOutParentAndCurrentDirectory, custom_sort);

    // Check for potential errors from scandir
    if (n < 0) {
        return badcommand(COMMAND_ERROR_SCANDIR);
    } else {
        for (int i = 0; i < n; i++) {
            // Print each file/directory name
            printf("%s\n", content[i]->d_name);
            free(content[i]);  // Free allocated memory for each entry
        }
    }
    free(content);  // Free the array of directory entries
    return 0;
}

/**
 * Function implementing the touch command
 * which creates a new file named after the given argument.
 *
 * @param input A pointer to the string representing the name of the file to be
 * created.
 * @return Returns an integer indicating success (0) or or non-zero on failure
 */
int my_touch(char *input) {
    FILE *f;

    // Input validation
    if (!is_alphanumeric(input)) {
        return badcommand(COMMAND_ERROR_NON_ALPHANUM);
    }

    // Check if the file exists
    if (access(input, F_OK) != 0) {
        // Create an empty file with name input
        f = fopen(input, "w");
        if (f == NULL) {
            return badcommand(
                COMMAND_ERROR_FILE_OPEN);  // Error while creating the file
        }
        fclose(f);  // Closing the empty file
    }

    return 0;
}

/**
 * Function implementing the mkdir command which creates a new directory named
 * after the given argument.
 * 
 * @param input A pointer to the string representing the name of the directory
 * to be created.
 * @return Returns an integer indicating success (0) or non-zero on failure
 */
int my_mkdir(char *input) {
    char buffer[MAX_VARIABLE_VALUE_SIZE];  // Buffer to store the variable value

    if (input[0] == '$') {           // Case for variable in memory
        char *var_name = input + 1;  // Ignore the '$'

        // Input validation
        if (!is_alphanumeric(var_name)) {
            return badcommand(COMMAND_ERROR_NON_ALPHANUM);  // Input validation error
        }
        mem_get_value(var_name, buffer);  // Retrieve variable value into buffer

        // Make sure that the value of the variable is valid
        if (strcmp(buffer, "Variable does not exist") != 0 && strstr(buffer, " ") == NULL) {
            mkdir(buffer, 0777);
        } else {
            return badcommand(COMMAND_ERROR_MKDIR);
        }
    } else {  // Case where dirname is not a variable
        // Input validation
        if (!is_alphanumeric(input)) {
            return badcommand(COMMAND_ERROR_NON_ALPHANUM);  // Input validation error
        }
        mkdir(input, 0777);
    }

    return 0;
}

/**
 * Changes the current working directory.
 *
 * @param input A string representing the path to the target directory.
 * @return 0 on successful execution or non-zero on failure
 */
int my_cd(char *input) {
    // Input validation
    if (!is_alphanumeric(input)) {
        return badcommand(COMMAND_ERROR_NON_ALPHANUM);  // Input validation error
    } else {
        // attempt to chdir
        if (chdir(input) != 0) {
            return badcommand(COMMAND_ERROR_CD);
        }
    }

    return 0;
}

/**
 * This function takes a script as input and executes it through the scheduler.
 *
 * @param script  A string representing the path to the script to be executed.
 * @return 0 on successful execution, or non-zero on failure
 */
int run(char *script) {
    int errCode = 0;
    FILE *p;

    p = fopen(script, "rt");  // the program is in a file
    if (p == NULL) {
        errCode = badcommand(COMMAND_ERROR_FILE_INEXISTENT);
    } else {
        // First load the script into memory
        // Here the FCFS policy is passed but it doesn't really matter
        // because run is only executing one script
        errCode = mem_load_script(p, FCFS);

        if (!errCode) {
            // The schedulerRun function is passed the FCFS policy
            // but it doesn't really matter because only one script
            // is executing. The two other arguments are 0 (false),
            // because run doesn't use background (#) or multithreading.
            schedulerRun(FCFS, 0, 0);
        } else if (errCode == -1) {
            errCode = badcommand(COMMAND_ERROR_MEM_LOAD_SCRIPT);
        }

        fclose(p);
    }

    return errCode;
}

/**
 * This function takes an array of script names and executes them according to
 * the specified policy. The execution can occur in the background and/or
 * concurrently, depending on the provided flags.
 *
 * @param scripts An array of strings representing the scripts to be executed.
 * @param scripts_number The number of scripts in the array.
 * @param policy The policy governing the execution of the scripts (e.g., FCFS,
 * SJF, RR, RR30, AGING).
 * @param isRunningInBackground A flag indicating whether the scripts should run
 * in the background (1 for true, 0 for false).
 * @param isRunningConcurrently A flag indicating whether the scripts should run
 * concurrently (1 for true, 0 for false).
 * @return Returns non-zero value on failure.
 */
int exec(char *scripts[], int scripts_number, policy_t policy,
         int isRunningInBackground, int isRunningConcurrently) {
    int script_idx, errCode = 0;
    FILE *p;

    // Making sure that script filenames are different
    if (scripts_number > 1) {
        if (strcmp(scripts[0], scripts[1]) == 0) {
            return badcommand(COMMAND_ERROR_BAD_COMMAND);
        }
        if (scripts_number == 3) {
            if ((strcmp(scripts[0], scripts[2]) == 0) ||
                (strcmp(scripts[1], scripts[2]) == 0)) {
                return badcommand(COMMAND_ERROR_BAD_COMMAND);
            }
        }
    }

    // Loading scripts into memory and checking for any errors
    for (script_idx = 0; script_idx < scripts_number; script_idx++) {
        p = fopen(scripts[script_idx], "rt");  // the program is in a file
        // Check if file can be opened
        if (p == NULL) {
            return badcommand(COMMAND_ERROR_FILE_INEXISTENT);
        }

        // Check for errors when loading the script
        if (mem_load_script(p, policy)) {
            return badcommand(COMMAND_ERROR_MEM_LOAD_SCRIPT);
        }

        fclose(p);
    }

    // Loading main shell if isRunningInBackground (#) set to True
    if (isRunningInBackground) {
        if (mem_load_script(stdin, INVALID_POLICY)) {
            return badcommand(COMMAND_ERROR_MEM_LOAD_SCRIPT);
        }
        execOnlyLoading = 1;
    }

    // Only executing the schedulerRun function if the exec command
    // wasn't preceded by another exec command with # option
    if (!execOnlyLoading || isRunningInBackground) {
        schedulerRun(policy, isRunningInBackground, isRunningConcurrently);
    }
}

/*** HELPER FUNCTIONS ***/

/**
 * Helper predicate function that returns whether the string is alphanumeric
 *
 * @param str A pointer to the string to be checked for alphanumeric characters.
 * @return Returns 1 if the string is alphanumeric, 0 otherwise.
 */
int is_alphanumeric(char *str) {
    int i = 0;
    // Loop through all characters
    while (str[i] != '\0') {
        if (!isalnum(str[i])) {
            return 0;
        }
        i++;
    }

    return 1;
}

/**
 * Helper predicate function that returns whether all the strings in the string
 * array are alphanumeric
 *
 * @param lst A pointer to an array of strings to be checked for alphanumeric
 * characters.
 * @param len_lst The number of strings in the array.
 * @return Returns 1 if all strings in the array are alphanumeric, 0 otherwise.
 */
int is_alphanumeric_list(char **lst, int len_lst) {
    // Loop over all strings in list
    for (int i = 0; i < len_lst; i++) {
        if (!is_alphanumeric(lst[i])) {
            return 0;
        }
    }

    return 1;
}

/**
 * Helper function that filters out current and parent direction during my_ls
 *
 * @param entry A pointer to the directory entry structure to be checked.
 * @return Returns 0 if the entry is . or .. returns 1 others
 */
int filterOutParentAndCurrentDirectory(const struct dirent *entry) {
    // Skip Over: "." -> Current Directory & ".." -> Parent Directory
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        return 0;  // False (i.e. Ignore this one)
    }
    return 1;  // True  (i.e Don't Ignore)
}

/**
 * This function is used to sort entries during my_ls with numeric entries
 * first, followed by alphabetic entries, prioritizing capital letters over
 * lowercase ones.
 *
 * @param d1  A pointer to the my_ls entry to compare.
 * @param d2  A pointer to the second my_ls entry to compare.
 * @return    A -1 if d1 is less than d2,
 *            a 1 if d1 is greater than d2,
 *            and 0 if they are equal.
 */
int custom_sort(const struct dirent **d1, const struct dirent **d2) {
    // Store name to compare in name1 and name2
    const char *name1 = (*d1)->d_name;
    const char *name2 = (*d2)->d_name;

    // Case when both start with same type (i.e. int or letters)
    int i = 0;
    while (name1[i] != '\0' && name2[i] != '\0') {
        // Case when one starts with a [0-9] and the other with [A-Za-z]
        if (isdigit(name1[i]) && isalpha(name2[i])) {
            return -1;  // name1 before name2
        } else if (isalpha(name1[i]) && isdigit(name2[i])) {
            return 1;  // name2 before name1
        }
        // ith char are different
        if (tolower(name1[i]) != tolower(name2[i])) {
            if (tolower(name1[i]) < tolower(name2[i])) {
                return -1;  // name1 before name2
            } else {
                return 1;  // name2 before name1
            }
        }
        // Same character but different capitalization
        if (name1[i] != name2[i]) {
            if (name1[i] < name2[i]) {
                return -1;  // name1 before name2
            } else {
                return 1;  // name2 before name1
            }
        }
        i++;
    }

    // Case when two string have the same beginning, but different ending (e.g.
    // shell.c and shellmemory.c)
    if (name1[i] == '\0' && name2[i] != '\0') {
        return -1;  // name1 is shorter, should come first
    } else if (name2[i] == '\0' && name1[i] != '\0') {
        return 1;  // name2 is shorter, should come first
    }
    return 0;  // When names are identical
}

/**
 * This function takes a string representing a policy and parses it to return
 * the corresponding 'policy_t' enumeration. Choices are FCFS, SJF, RR, RR30,
 * AGING or INVALID_POLICY
 *
 * @param policy_str A pointer to a string representing the policy to be parsed.
 * @return Returns the corresponding policy_t value on success, or
 * INVALID_POLICY if parsing fails.
 */
policy_t policy_parser(char policy_str[]) {
    // Checking if policy is available
    if (strcmp(policy_str, "FCFS") == 0) {
        return FCFS;
    } else if (strcmp(policy_str, "SJF") == 0) {
        return SJF;
    } else if (strcmp(policy_str, "RR") == 0) {
        return RR;
    } else if (strcmp(policy_str, "RR30") == 0) {
        return RR30;
    } else if (strcmp(policy_str, "AGING") == 0) {
        return AGING;
    } else {
        return INVALID_POLICY;
    }
}

/**
 * Helper function that manages error messages for the interpreter
 * 
 * @param errorCode The error code indicating the type of command error
 * encountered.
 * @return Returns an integer indicating the status of the error handling
 * operation. Typically, returns 0 for successful message display, and non-zero
 * for errors.
 */
int badcommand(commandError_t errorCode) {
    switch (errorCode) {
        case COMMAND_ERROR_BAD_COMMAND:
            printf("Unknown Command\n");
            break;
        case COMMAND_ERROR_TOO_MANY_TOKENS:
            printf("Bad command: Too many tokens\n");
            break;
        case COMMAND_ERROR_FILE_INEXISTENT:
            printf("Bad command: File not found\n");
            break;
        case COMMAND_ERROR_MKDIR:
            printf("Bad command: my_mkdir\n");
            break;
        case COMMAND_ERROR_CD:
            printf("Bad command: my_cd\n");
            break;
        case COMMAND_ERROR_MEM_LOAD_SCRIPT:
            printf("Memory loading for script error: exec\n");
            break;
        default:
            break;
    }

    return (int)errorCode;
}