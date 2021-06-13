#
# Makefile
#

CC=gcc
CFLAGS=-Wall -Wextra -Werror -I../evfs/include -L../evfs
LOADLIBES=-levfs
PROG=test

all: depend $(PROG)

$(PROG): test.o ../evfs/libevfs.a
	$(CC) $(CFLAGS) -o $@ $< $(LOADLIBES)
	
.PHONY: clean
clean:
	rm -f .depend $(PROG) *.o
	
depend:
	$(CC) -MM *.c > .depend

ifeq (.depend,$(wildcard .depend))
include .depend
endif	
