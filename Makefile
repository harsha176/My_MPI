CC=gcc
CFLAGS=-g -Wall -Werror
DFLAGS=
EXECUTABLE=rtt

all:mympic.o mymsg.o
	$(CC) $(CFLAGS) $(DFLAGS) rtt.c mympi.o mymsg.o -o $(EXECUTABLE)
mympic.o:mympi.c mympi.h
	$(CC) $(CFLAGS) $(DFLAGS) -c mympi.c
mymsg.o:mymsg.c mymsg.h
	$(CC) $(CFLAGS) $(DFLAGS) -c mymsg.c
clean:
	rm -rf mympi.o mymsg.o rtt tags msg.txt a.out
tags:
	ctags *
