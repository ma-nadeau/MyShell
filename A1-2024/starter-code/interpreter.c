#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
    COMMAND_ERROR_NON_ALPHANUM
} commandError_t;

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
        default:
            break;
    }

    return (int) errorCode;
}

/*** Function signatures ***/

int help();
int quit();
int set(char *var, char *values[], int number_values);
int print(char *var);
int run(char *script);
int echo(char *input);
int my_ls(void);
int my_touch(char *input);
int my_mkdir(char *input);
int my_cd(char *input);
int is_alphanumeric(char *str);
int filterOutParentAndCurrentDirectory(const struct dirent *entry);
int custom_sort(const struct dirent **d1, const struct dirent **d2);
int is_alphanumeric_list(char **lst, int len_lst);

// Interpret commands and their arguments
int interpreter(char *command_args[], int args_size) {
    int i;

    if (args_size < 1) {
        return badcommand(COMMAND_ERROR_BAD_COMMAND);
    } else if (args_size > MAX_ARGS_SIZE) {
        return badcommand(COMMAND_ERROR_TOO_MANY_TOKENS);
    }

    for (i = 0; i < args_size; i++) {  // terminate args at newlines
        command_args[i][strcspn(command_args[i], "\r\n")] = 0;
    }

    if (strcmp(command_args[0], "help") == 0) {
        // help
        if (args_size != 1) return badcommand(COMMAND_ERROR_BAD_COMMAND);
        return help();

    } else if (strcmp(command_args[0], "quit") == 0) {
        // quit
        if (args_size != 1) return badcommand(COMMAND_ERROR_BAD_COMMAND);
        return quit();

    } else if (strcmp(command_args[0], "set") == 0) {
        // set
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

    } else {
        return badcommand(COMMAND_ERROR_BAD_COMMAND);
    }
}

int help() {
    // note the literal tab characters here for alignment
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

int quit() {
    printf("Bye!\n");
    exit(0);
}

int set(char *var, char *values[], int number_values) {
    char *link = "=";

    /* PART 1: You might want to write code that looks something like this.
         You should look up documentation for strcpy and strcat.

    char buffer[MAX_USER_INPUT];
    strcpy(buffer, var);
    strcat(buffer, link);
    strcat(buffer, value);
    */

    // Input validation
    if (!is_alphanumeric(var) || !is_alphanumeric_list(values, number_values)) {
        return badcommand(COMMAND_ERROR_NON_ALPHANUM);  // Input validation error
    }

    mem_set_value(var, values, number_values);

    return 0;
}

int print(char *var) {
    char buffer[MAX_VARIABLE_VALUE_SIZE];
    mem_get_value(var, buffer);
    printf("%s\n", buffer);
    return 0;
}

int echo(char *input) {
    char buffer[MAX_VARIABLE_VALUE_SIZE];  // Buffer to store the variable value

    if (input[0] == '$') {           // Case for variable in memory
        char *var_name = input + 1;  // Ignore the '$'

        // Input validation
        if (!is_alphanumeric(var_name)) {
            return badcommand(COMMAND_ERROR_NON_ALPHANUM);  // Input validation error
        }
        mem_get_value(var_name, buffer);  // Retrieve variable value into buffer
        if (strcmp(buffer, "Variable does not exist") != 0 ||
            mem_get_variable_index(var_name) > -1) {
            printf("%s\n", buffer);  // Print the value (empty if not found)
        } else {
            printf("\n");
        }

    } else {  // Case for displaying string on a new line
        // Input validation
        if (!is_alphanumeric(input)) {
            return badcommand(COMMAND_ERROR_NON_ALPHANUM);  // Input validation error
        }
        printf("%s\n", input);
    }

    return 0;
}

int my_ls(void) {
    // Pointer to an array of pointer that points to the content
    struct dirent **content;

    // store in content the list of file/folder names in the proper order
    int n =
        scandir(".", &content, filterOutParentAndCurrentDirectory, custom_sort);
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

int my_touch(char *input) {
    FILE *f;

    // Input validation
    if (!is_alphanumeric(input)) {
        return badcommand(COMMAND_ERROR_NON_ALPHANUM);  // Input validation error
    }

    // Check if the file exists
    if (access(input, F_OK) != 0) {
        // Create an empty file with name input
        f = fopen(input, "w");
        if (f == NULL) {
            return badcommand(COMMAND_ERROR_FILE_OPEN);  // Error while creating the file
        }

        fclose(f);  // Closing the empty file
    }

    return 0;
}

int my_mkdir(char *input) {
    char buffer[MAX_VARIABLE_VALUE_SIZE];  // Buffer to store the variable value

    if (input[0] == '$') {           // Case for variable in memory
        char *var_name = input + 1;  // Ignore the '$'

        // Input validation
        if (!is_alphanumeric(var_name)) {
            return badcommand(COMMAND_ERROR_NON_ALPHANUM);  // Input validation error
        }
        mem_get_value(var_name, buffer);  // Retrieve variable value into buffer
        if (strcmp(buffer, "Variable does not exist") != 0 &&
            strstr(buffer, " ") == NULL) {
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

int run(char *script) {
    int errCode = 0;
    errCode = mem_load_script(script);
    if (!errCode) {
        schedulerRun(FCFS);
    } else if(errCode == -1) {
        errCode =  badcommand(COMMAND_ERROR_FILE_INEXISTENT);
    }
    return errCode;
}

/*** HELPER FUNCTIONS ***/

/**
 * Helper predicate function that returns whether the string is alphanumeric
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

int filterOutParentAndCurrentDirectory(const struct dirent *entry) {
    // Skip Over: "." -> Current Directory & ".." -> Parent Directory
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        return 0;  // False (i.e. Ignore this one)
    }
    return 1;  // True  (i.e Don't Ignore)
}

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
        // ith char are different
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
