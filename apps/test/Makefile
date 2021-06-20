#
# Makefile
#

CC=gcc
EVFSDIR=../evfs
EVFSLIB=$(EVFSDIR)/libevfs.a
LOADLIBES=-levfs
CFLAGS=-Wall -Wextra -Werror -I$(EVFSDIR)/include -L../evfs -ggdb
PROG=iinfo iup sbinfo iiter
PROG=iinfo iset sbinfo exalloc exup exfree iiter myextest

all: depend $(EVFSLIB) $(PROG)

iinfo: iinfo.o $(EVFSLIB)
	$(CC) $(CFLAGS) -o $@ $< $(LOADLIBES)

iset: iset.o $(EVFSLIB)
	$(CC) $(CFLAGS) -o $@ $< $(LOADLIBES)

sbinfo: sbinfo.o $(EVFSLIB)
	$(CC) $(CFLAGS) -o $@ $< $(LOADLIBES)

iiter: iiter.o $(EVFSLIB)
	$(CC) $(CFLAGS) -o $@ $< $(LOADLIBES)

exalloc: exalloc.o $(EVFSLIB)
	$(CC) $(CFLAGS) -o $@ $< $(LOADLIBES)

exup: exup.o $(EVFSLIB)
	$(CC) $(CFLAGS) -o $@ $< $(LOADLIBES)
	
exfree: exfree.o $(EVFSLIB)
	$(CC) $(CFLAGS) -o $@ $< $(LOADLIBES)	
	
myextest: myextest.o $(EVFSLIB)
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
