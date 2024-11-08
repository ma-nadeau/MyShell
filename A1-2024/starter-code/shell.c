#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "shell.h"
#include "interpreter.h"
#include "scheduler.h"
#include "scriptsmemory.h"
#include "shellmemory.h"

/*** FUNCTION SIGNATURES ***/

int parseInput(char ui[]);
int convertInputToOneLiners(char input[]);
int wordEnding(char c);
int countChar(char input[], char search);

/**
 * Start of everything
 *
 * @param argc The number of command-line arguments passed to the program.
 * @param argv An array of pointers to the command-line arguments.
 * @return Returns an integer status code, 0 for success
 */
int main(int argc, char *argv[]) {
    printf("Frame Store Size = %d; Variable Store Size = %d\n", FRAME_STORE_SIZE, VAR_MEMSIZE);
    // help();  //Not printing the help text anymore at start of shell

    char prompt = '$';               // Shell prompt
    char userInput[MAX_USER_INPUT];  // user's input stored here
    int errorCode = 0;               // zero means no error, default

    // initialize user input
    for (int i = 0; i < MAX_USER_INPUT; i++) {
        userInput[i] = '\0';
    }

    // initialize shell memory array and associated concurrency variable
    mem_init();
    // initialize scheduler scripts memory array and associated concurrency
    // variables
    scheduler_init();
    // initialize memory for scripts and associated concurrency variables
    scripts_memory_init();

    while (1) {
        // In batch mode, check if eof is reached in which case we quit
        if (feof(stdin)) {
            break;
        }

        // if print the prompt only if the stdin is pointing to terminal
        if (isatty(fileno(stdin))) {
            printf("%c ", prompt);
        }

        fgets(userInput, MAX_USER_INPUT - 1, stdin);
        convertInputToOneLiners(userInput);
        memset(userInput, 0, sizeof(userInput));
    }

    return 0;
}

/*** PARSING FUNCTIONS ***/

/**
 * Converts a single line of commands into executable statements.
 * This function takes a string containing multiple commands separated by
 * semicolons (';') and processes each command to execute them one by one.
 *
 * @param input A string containing commands separated by semicolons.
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int convertInputToOneLiners(char input[]) {
    int count;
    char *start = input;
    int errorCode = 0;  // zero means no error, default
    int tokenCommandLen, inputRemainingLen;

    if (strlen(input) > MAX_USER_INPUT) {
        return 1;  // Error, more than 1000 char
    }
    count = countChar(input, ';');
    if (count > 9) {
        return 2;  // Error, more than 10 command
    }
    // strcspn(start, ";") will output length of the string if it doesn't find
    // ";"
    do {
        tokenCommandLen = strcspn(start, ";");
        inputRemainingLen = strlen(start);
        // Temporary place to store the command to be called
        char temp[MAX_USER_INPUT + 1];
        // Store in temp the string from the pointer start to the index of 1st
        // ";"
        strncpy(temp, start, tokenCommandLen);
        temp[tokenCommandLen] = '\0';  // Null-terminate the substring

        errorCode = parseInput(temp);   // Copied from the original code
        if (errorCode == -1) exit(99);  // ignore all other errors
        start += tokenCommandLen + 1;   // Move the pointer to go after the ";"
    } while (tokenCommandLen < inputRemainingLen);
    return errorCode;
}

/**
 * The function takes as input a string containing the command and associated
 * arguments and it executes it.
 *
 * @param input A string containing the command and arguments.
 * @return Returns 0 on success, or a non-zero value on failure.
 */
int parseInput(char inp[]) {
    char tmp[MAX_TOKEN_SIZE], *words[100];
    int ix = 0, w = 0, wordlen, errorCode, token_idx;

    // skip white spaces
    for (ix = 0; inp[ix] == ' ' && ix < MAX_USER_INPUT; ix++);
    while (inp[ix] != '\n' && inp[ix] != '\0' && ix < MAX_USER_INPUT) {
        // extract a word
        for (wordlen = 0; !wordEnding(inp[ix]) && ix < MAX_USER_INPUT &&
                          wordlen < MAX_TOKEN_SIZE - 1;
             ix++, wordlen++) {
            tmp[wordlen] = inp[ix];
        }
        tmp[wordlen] = '\0';
        words[w] = strdup(tmp);
        w++;
        if (inp[ix] == '\0') break;
        ix++;
    }
    errorCode = interpreter(words, w);

    // free allocated memory for input tokens
    for (token_idx = 0; token_idx < w; token_idx++) {
        free(words[token_idx]);
    }

    return errorCode;
}

/*** HELPER FUNCTIONS ***/

/**
 * Predicate determining whether a char is a word-ending character
 *
 * @param c The character to be checked.
 * @return Returns 1 (true) if the character is a word-ending character,
 * otherwise returns 0 (false)
 */
int wordEnding(char c) { return c == '\0' || c == '\n' || c == ' '; }

/**
 * Function that determines how many "search" chars are in the string "input"
 *
 * @param input A string in which to count occurrences of the character.
 * @param search The character to search from within the input string.
 * @return Returns the number of times the specified character appears in the string.
 */
int countChar(char input[], char search) {
    int count = 0;
    for (int i = 0; input[i] != '\0'; i++) {
        if (input[i] == search) {
            count++;
        }
    }
    return count;
}