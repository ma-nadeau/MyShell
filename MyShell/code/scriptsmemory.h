#define PAGE_SIZE 3
#define PAGE_TABLE_SIZE 100

#ifndef FRAME_STORE_SIZE
#define FRAME_STORE_SIZE 99
#endif

#define FRAME_NUMBER FRAME_STORE_SIZE/PAGE_SIZE

struct scriptFrames {
    char *scriptName;
    int lengthCode;
    int pageTable[PAGE_TABLE_SIZE];
    int PCBsInUse;
    int FramesInUse;
};

void scripts_memory_init();
char *fetchInstructionVirtual(int instructionVirtualAddress, struct scriptFrames *scriptInfo);
void updateInstructionVirtual(int instructionVirtualAddress, struct scriptFrames *scriptInfo, char newInstruction[]);
void pageAssignment(int pageNumber, struct scriptFrames *scriptInfo, int setup);
struct scriptFrames *findExistingScript(char script[]);
