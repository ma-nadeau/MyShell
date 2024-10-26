void scripts_memory_init();
char *fetchInstruction(int instructionAddress);
void updateInstruction(int instructionAddress, char *newInstruction);
int allocateMemoryScript(int scriptLength);
void addMemoryAvailability(int memoryStartIdx, int lengthCode);
