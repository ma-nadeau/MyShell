#define PAGE_SIZE 3

#ifndef FRAME_STORE_SIZE
#define FRAME_STORE_SIZE 99

#endif

void scripts_memory_init();
char *fetchInstruction(int instructionAddress);
void updateInstruction(int instructionAddress, char *newInstruction);
int allocateMemoryScript(int scriptLength);
void addMemoryAvailability(int memoryStartIdx, int lengthCode);
