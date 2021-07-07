#
# Makefile
#

CC=gcc
CFLAGS=-Wall -Wextra -Werror -ggdb -I./include
PROG=libevfs.a
OBJECT:=$(patsubst %.c,%.o,$(wildcard *.c))

all: depend $(PROG)

$(PROG): $(OBJECT) 
	$(AR) rcs $@ $^

.PHONY: clean
clean:
	rm -f .depend $(PROG) *.o

depend:
	$(CC) -MM *.c > .depend

ifeq (.depend,$(wildcard .depend))
include .depend
endif	
