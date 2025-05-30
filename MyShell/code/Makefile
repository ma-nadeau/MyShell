CC=gcc
CFLAGS=
CFLAGFRAME=
CFLAGVAR=

ifdef framesize
  CFLAGFRAME=-D FRAME_STORE_SIZE=$(framesize)
endif

ifdef varmemsize
  CFLAGVAR=-D VAR_MEMSIZE=$(varmemsize)
endif

CFLAGS=$(CFLAGFRAME) $(CFLAGVAR)

mysh: shell.c interpreter.c shellmemory.c scheduler.c scriptsmemory.c
	$(CC) $(CFLAGS) -g -c shell.c interpreter.c shellmemory.c scheduler.c scriptsmemory.c
	$(CC) $(CFLAGS) -g -o mysh shell.o interpreter.o shellmemory.o scheduler.o scriptsmemory.o

clean: 
	rm mysh; rm *.o
