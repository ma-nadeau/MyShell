#include "shell.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "interpreter.h"
#include "shellmemory.h"

int parseInput(char ui[]);
int convertInputToOneLiners(char input[]);

// Start of everything
int main(int argc, char *argv[]) {
    printf("Shell version 1.3 created September 2024\n");
    help();

    char prompt = '$';               // Shell prompt
    char userInput[MAX_USER_INPUT];  // user's input stored here
    int errorCode = 0;               // zero means no error, default

    // init user input
    for (int i = 0; i < MAX_USER_INPUT; i++) {
        userInput[i] = '\0';
    }

    // init shell memory
    mem_init();
    while (1) {
        // In batch mode, check if eof is reached to switch back to interactive
        // mode
        if (feof(stdin)) {
            freopen("/dev/tty", "r", stdin);
        }

        // if we the stdin is pointing to terminal only print prompt
        if (isatty(fileno(stdin))) {
            printf("%c ", prompt);
        }
        // here you should check the unistd library
        // so that you can find a way to not display $ in the batch mode
        fgets(userInput, MAX_USER_INPUT - 1, stdin);
        convertInputToOneLiners(userInput);
        // errorCode = parseInput(userInput);
        // if (errorCode == -1) exit(99);  // ignore all other errors
        memset(userInput, 0, sizeof(userInput));
    }

    return 0;
}

int wordEnding(char c) {
    // You may want to add ';' to this at some point,
    // or you may want to find a different way to implement chains.
    return c == '\0' || c == '\n' || c == ' ';
}
int countChar(char input[], char search) {
    int count = 0;
    for (int i = 0; input[i] != '\0'; i++) {
        if (input[i] == search) {
            count++;
        }
    }
    return count;
}

int convertInputToOneLiners(char input[]) {
    int count;
    char *start = input;
    int errorCode = 0;  // zero means no error, default

    if (strlen(input) > MAX_USER_INPUT) {
        return 1;  // Error, more than 1000 char
    }
    count = countChar(input, ';');
    if (count > 9) {
        return 2;  // Error, more than 10 command
    }
    int ix = 0;
    int prev_index = 0;
    // strcspn(start, ";") will output length of the string if it doesn't find
    // ";"
    while (strcspn(start, ";") < strlen(start)) {
        int len = strcspn(start, ";");
        // Temporary place to store the command to be called
        char temp[MAX_USER_INPUT + 1];
        // Store in temp the string from thepointer start to the index of 1st
        // ";"
        strncpy(temp, start, len);
        temp[len] = '\0';  // Null-terminate the substring

        errorCode = parseInput(temp);   // Copied from the original code
        if (errorCode == -1) exit(99);  // ignore all other errors
        start += len + 1;               // Move the pointer to go after the ";"
    }
    errorCode = parseInput(start);  // Copied from the original code
    if (errorCode == -1) exit(99);  // ignore all other errors
    return errorCode;
}

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
