#
# Makefile
#

CC=gcc
EVFSDIR=../evfs
EVFSLIB=$(EVFSDIR)/libevfs.a
LOADLIBES=-levfs
CFLAGS=-Wall -Wextra -Werror -I$(EVFSDIR)/include -L../evfs -ggdb
PROG=iinfo iup


all: depend $(PROG)

iinfo: iinfo.o $(EVFSLIB)
	$(CC) $(CFLAGS) -o $@ $< $(LOADLIBES)

iup: iup.o $(EVFSLIB)
	$(CC) $(CFLAGS) -o $@ $< $(LOADLIBES)

$(EVFSLIB):
	cd $(EVFSDIR) && $(MAKE)
	
.PHONY: clean
clean:
	rm -f .depend $(PROG) *.o
	
depend:
	$(CC) -MM *.c > .depend

ifeq (.depend,$(wildcard .depend))
include .depend
endif	
