#
# Makefile
#

CC=gcc
CFLAGS=-Wall -Wextra -Werror -I../evfs/include -L../evfs
LOADLIBES=-levfs
PROG=defrag

all: depend $(PROG)

$(PROG): defrag.o
	$(CC) $(CFLAGS) -o $@ $^ $(LOADLIBES)
	
.PHONY: clean
clean:
	rm -f .depend $(PROG) *.o
	
depend:
	$(CC) -MM *.c > .depend

ifeq (.depend,$(wildcard .depend))
include .depend
endif	
