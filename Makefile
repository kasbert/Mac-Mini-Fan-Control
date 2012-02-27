CC = gcc
CFLAGS = -mmacosx-version-min=10.4  -Wall -g -framework IOKit -framework CoreFoundation
all: fantune 

fantune: smc.o main.o
	$(CC) $(CFLAGS) -o fantune smc.o main.o

smc.o: smc.h smc.c
	$(CC) -c smc.c

main.o: main.c
	$(CC) -c main.c

clean:
	-rm -f smc smc.o main.o
