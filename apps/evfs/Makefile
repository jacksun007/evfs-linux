#
# Makefile
#

CC=gcc
CFLAGS=-Wall -Wextra -Werror -ggdb -I./include
PROG=libevfs.a
EVFS_H=../../../include/uapi/linux/evfspub.h
EVFSPRIV_H=../../include/uapi/linux/evfspriv.h

all: depend $(PROG)

$(PROG): evfs.o atomic.o
	$(AR) rcs $@ $^

.PHONY: clean
clean:
	rm -f .depend $(PROG) *.o

depend: evfs.h
	$(CC) -MM *.c > .depend

evfs.h:
	ln -s $(EVFS_H) ./include/evfs.h
	ln -s $(EVFSPRIV_H) ./evfspriv.h

ifeq (.depend,$(wildcard .depend))
include .depend
endif	
