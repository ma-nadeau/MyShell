#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int help();
int quit();
int set(char *var, char *values[], int number_values);
int print(char *var);
int run(char *script);
int echo(char *input);
int badcommandFileDoesNotExist();
int my_ls(void);

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
        printf("%s\n", buffer);  // Print the value (empty if not found)

    } else {  // Case for displaying string on a new line
        printf("%s\n", input);
    }

    return errCode;
}


int my_ls(void) { // TODO: doesn't list them in the proper order just yet
    DIR *directory = opendir(".");  // Open current directory
    struct dirent *entry;
    // Display the the names of the files/repo inside the current repo (not in the right order)
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            printf("%s\n", entry->d_name);
        }
    }
    closedir(directory);
    return 0;
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
        errCode = parseInput(line);  // which calls interpreter()
        memset(line, 0, sizeof(line));

        if (feof(p)) {
            break;
        }
        fgets(line, MAX_USER_INPUT - 1, p);
    }

    fclose(p);

    return errCode;
}
