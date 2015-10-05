
CC=gcc
CFLAGS=-std=c99 -Wall $(COPT)

all: lizar

lizar: lizar.o util.o
	$(CC) $(CFLAGS) lizar.o util.o -o lizar

lizar.o: lizar.c util.h
	$(CC) $(CFLAGS) -c lizar.c

util.o: util.h util.c
	$(CC) $(CFLAGS) -c util.c

clean:
	$(RM) *.o
	$(RM) lizar
