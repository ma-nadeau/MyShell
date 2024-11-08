CC=gcc
CFLAGS=-D FRAME_STORE_SIZE=$(framesize) -D VAR_MEMSIZE=$(varmemsize)

mysh: shell.c interpreter.c shellmemory.c scheduler.c scriptsmemory.c
	$(CC) $(CFLAGS) -c shell.c interpreter.c shellmemory.c scheduler.c scriptsmemory.c
	$(CC) $(CFLAGS) -o mysh shell.o interpreter.o shellmemory.o scheduler.o scriptsmemory.o

clean: 
	rm mysh; rm *.o
