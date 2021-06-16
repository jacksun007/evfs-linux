#
# Makefile
#

CC=gcc
EVFSDIR=../evfs
EVFSLIB=$(EVFSDIR)/libevfs.a
LOADLIBES=-levfs
CFLAGS=-Wall -Wextra -Werror -I$(EVFSDIR)/include -L../evfs -ggdb
PROG=iinfo iup sbinfo iiter
PROG=iinfo iset sbinfo exalloc exup iiter

all: depend $(EVFSLIB) $(PROG)

iinfo: iinfo.o 
	$(CC) $(CFLAGS) -o $@ $< $(LOADLIBES)

iset: iset.o
	$(CC) $(CFLAGS) -o $@ $< $(LOADLIBES)

sbinfo: sbinfo.o
	$(CC) $(CFLAGS) -o $@ $< $(LOADLIBES)

iiter: iiter.o $(EVFSLIB)
	$(CC) $(CFLAGS) -o $@ $< $(LOADLIBES)

exalloc: exalloc.o
	$(CC) $(CFLAGS) -o $@ $< $(LOADLIBES)

exup: exup.o $(EVFSLIB)
	$(CC) $(CFLAGS) -o $@ $< $(LOADLIBES)    

#exfree: exfree.o $(EVFSLIB)
#	$(CC) $(CFLAGS) -o $@ $< $(LOADLIBES)   

# always execute
.PHONY: $(EVFSLIB)
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
