#include <stdio.h>
#include <stdlib.h>
#include <string.h> // memset
#include "shell.h" // MAX_USER_INPUT
#include "shellmemory.h"
#include "pcb.h"

int pcb_has_next_instruction(struct PCB *pcb) {
    // have next if pc < line_count.
    // Sanity check: count = 0  ==> never have next. Good!
    return pcb->pc < pcb->line_count;
}

size_t pcb_next_instruction(struct PCB *pcb) {
    size_t i = pcb->line_base + pcb->pc;
    pcb->pc++;
    return i;
}

struct PCB *create_process(const char *filename) {
    // We have 2 main tasks:
    // load all the code in the script file into shellmemory, and
    // allocate+fill a PCB.

    // We don't want to allocate a PCB until we know we actually need one,
    // so let's first make sure we can open the file.
    FILE *script = fopen(filename, "rt");
    if (!script) {
        perror("failed to open file for create_process");
        return NULL;
    }
    struct PCB *pcb = create_process_from_FILE(script);
    // Update the pcb name according to the filename we received.
    pcb->name = strdup(filename);
}

struct PCB *create_process_from_FILE(FILE *script) {
    // We can open the file, so we'll be making a process.
    struct PCB *pcb = malloc(sizeof(struct PCB));

    // The PID is the only weird part. They need to be distinct,
    // so let's use a static counter.
    static pid fresh_pid = 1;
    pcb->pid = fresh_pid++;

    // name should be the empty string, according to doc comment.
    pcb->name = "";
    // next should be NULL, according to doc comment.
    pcb->next = NULL;

    // pc is always initially 0.
    pcb->pc = 0;

    // create initial values for base and count, in case we fail to read
    // any lines from the file. That way we'll end up with an empty process
    // that terminates as soon as it is scheduled -- reasonable behavior for
    // an empty script.
    // If we don't do this, and the loop below never runs, base would be
    // unitialized, which would be catastrophic.
    pcb->line_count = 0;
    pcb->line_base = 0;

    // We're told to assume lines of files are limited to 100 characters.
    // That's all well and good, but for implementing # we need to read
    // actual user input, and _that_ is limited to 1000 characters.
    // It's unclear if we should assume it's also limited to 100 for this
    // purpose. If you did assume that, that's OK! We didn't.
    char linebuf[MAX_USER_INPUT];
    while (!feof(script)) {
        memset(linebuf, 0, sizeof(linebuf));
        fgets(linebuf, MAX_USER_INPUT, script);

        size_t index = allocate_line(linebuf);
        // If we've run out of memory, clean up the partially-allocated
        // pcb and return NULL.
        if (index == (size_t)(-1)) {
            free_pcb(pcb);
            fclose(script);
            return NULL;
        }

        if (pcb->line_count == 0) {
            // do this on the first iteration only.
            pcb->line_base = index;
        }
        pcb->line_count++;
    }

    // We're done with the file, don't forget to close it!
    fclose(script);

    // duration should initially match line_count.
    pcb->duration = pcb->line_count;

    return pcb;
}

void free_pcb(struct PCB *pcb) {
    for (size_t ix = pcb->line_base; ix < pcb->line_base + pcb->line_count; ++ix) {
        free_line(ix);
    }
    // Free the process name, but only if it's not the empty string.
    // The empty name (for the shell input process) was not malloc'd.
    if (strcmp("", pcb->name)) {
        free(pcb->name);
    }
    free(pcb);
}
