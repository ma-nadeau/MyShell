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

int badcommand() {
    printf("Unknown Command\n");
    return 1;
}

int badcommandTooManyTokens() {
    printf("Bad command: Too many tokens\n");
    return 2;
}

// For run command only
int badcommandFileDoesNotExist() {
    printf("Bad command: File not found\n");
    return 3;
}

// For my_mkdir command only
int badcommandMy_mkdir() {
    printf("Bad command: my_mkdir\n");
    return 4;
}

// For my_cd command only
int badcommandMy_cd() {
    printf("Bad command: my_cd\n");
    return 5;
}

int help();
int quit();
int set(char *var, char *values[], int number_values);
int print(char *var);
int run(char *script);
int echo(char *input);
int badcommandFileDoesNotExist();
int my_ls(void);
int my_touch(char *input);
int my_mkdir(char *input);
int my_cd(char *input);

// Interpret commands and their arguments
int interpreter(char *command_args[], int args_size) {
    int i;

    if (args_size < 1) {
        return badcommand();
    } else if (args_size > MAX_ARGS_SIZE) {
        return badcommandTooManyTokens();
    }

    for (i = 0; i < args_size; i++) {  // terminate args at newlines
        command_args[i][strcspn(command_args[i], "\r\n")] = 0;
    }

    if (strcmp(command_args[0], "help") == 0) {
        // help
        if (args_size != 1) return badcommand();
        return help();

    } else if (strcmp(command_args[0], "quit") == 0) {
        // quit
        if (args_size != 1) return badcommand();
        return quit();

    } else if (strcmp(command_args[0], "set") == 0) {
        // set
        if (args_size < 3) return badcommand();
        // The case where there are too many values is handled for all commands
        // simultaneously above
        return set(command_args[1], command_args + 2, args_size - 2);

    } else if (strcmp(command_args[0], "print") == 0) {
        if (args_size != 2) return badcommand();
        return print(command_args[1]);

    } else if (strcmp(command_args[0], "run") == 0) {
        if (args_size != 2) return badcommand();
        return run(command_args[1]);
    } else if (strcmp(command_args[0], "echo") == 0) {
        if (args_size != 2) return badcommand();
        return echo(command_args[1]);
    } else if (strcmp(command_args[0], "my_ls") == 0) {
        if (args_size != 1) return badcommand();
        return my_ls();
    } else if (strcmp(command_args[0], "my_touch") == 0) {
        if (args_size != 2) return badcommand();
        return my_touch(command_args[1]);
    } else if (strcmp(command_args[0], "my_mkdir") == 0) {
        if (args_size != 2) return badcommand();
        return my_mkdir(command_args[1]);
    } else if (strcmp(command_args[0], "my_cd") == 0) {
        if (args_size != 2) return badcommand();
        return my_cd(command_args[1]);
    } else
        return badcommand();
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

    // Could add input validation here at some point

    mem_set_value(var, values, number_values);

    return 0;
}

int print(char *var) {
    char buffer[MAX_VARIABLE_VALUE_SIZE];
    mem_get_value(var, buffer);
    printf("%s\n", buffer);
    return 0;
}

int echo(char *input) {  // TODO: 1 - Can it echo non-alphanumeric? 2- Do I need
                         // to check for token size?
    int errCode = 0;     // No error by default
    char buffer[MAX_VARIABLE_VALUE_SIZE];  // Buffer to store the variable value

    if (input[0] == '$') {                // Case for variable in memory
        char *var_name = input + 1;       // Ignore the '$'
        mem_get_value(var_name, buffer);  // Retrieve variable value into buffer
        if (strcmp(buffer, "Variable does not exist") != 0 || mem_get_variable_index(var_name) > -1) {
            printf("%s\n", buffer);  // Print the value (empty if not found)
        } else {
            printf("\n");
        }

    } else {  // Case for displaying string on a new line
        printf("%s\n", input);
    }

    return errCode;
}

int filterOutParentAndCurrentDirectory(const struct dirent *entry) {
    // Skip Over: "." -> Current Directory & ".." -> Parent Directory
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        return 0;  // False (i.e. Ingnore this one)
    }
    return 1;  // True  (i.e Don't Ignore)
}

int my_ls(void) {
    // Pointer to an array of pointer that points to the content
    struct dirent **content;

    // store in content the list of file/folder names in the proper order
    int n =
        scandir(".", &content, filterOutParentAndCurrentDirectory, alphasort);
    if (n < 0)
        return 6;
    else {
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

    // Check if the file exists
    if (access(input, F_OK) != 0) {
        // Create an empty file with name input
        f = fopen(input, "w");
        if (f == NULL) {
            return 7;  // Error while creating the file
        }

        fclose(f);  // Closing the empty file
    }

    return 0;
}

int my_mkdir(char *input) {
    int errCode = 0;                       // No error by default
    char buffer[MAX_VARIABLE_VALUE_SIZE];  // Buffer to store the variable value

    if (input[0] == '$') {                // Case for variable in memory
        char *var_name = input + 1;       // Ignore the '$'
        mem_get_value(var_name, buffer);  // Retrieve variable value into buffer
        if (strcmp(buffer, "Variable does not exist") != 0 &&
            strstr(buffer, " ") == NULL) {
            mkdir(buffer, 0777);
        } else {
            errCode = badcommandMy_mkdir();
        }
    } else {  // Case where dirname is not a variable
        mkdir(input, 0777);
    }

    return errCode;
}

int my_cd(char *input) {
    int errCode = 0;  // No error by default

    // attempt to chdir
    if (chdir(input) != 0) {
        errCode = badcommandMy_cd();
    }

    return errCode;
}

int run(char *script) {
    int errCode = 0;
    char line[MAX_USER_INPUT];
    FILE *p = fopen(script, "rt");  // the program is in a file

    if (p == NULL) {
        return badcommandFileDoesNotExist();
    }

    fgets(line, MAX_USER_INPUT - 1, p);
    while (1) {
        errCode = convertInputToOneLiners(line);  // which calls interpreter()
        memset(line, 0, sizeof(line));

        if (feof(p)) {
            break;
        }
        fgets(line, MAX_USER_INPUT - 1, p);
    }

    fclose(p);

    return errCode;
}
