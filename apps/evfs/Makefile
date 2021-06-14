#
# Makefile
#

CC=gcc
CFLAGS=-Wall -Wextra -Werror -ggdb -I./include
PROG=libevfs.a

all: depend $(PROG)

$(PROG): evfs.o atomic.o
	$(AR) rcs $@ $^
	
.PHONY: clean
clean:
	rm -f .depend $(PROG) *.o
	
depend:
	$(CC) -MM *.c > .depend

ifeq (.depend,$(wildcard .depend))
include .depend
endif	
