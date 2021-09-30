#
# Makefile
#

CC := gcc
CFLAGS := -Wall -Wextra -Werror -I../evfs/include -ggdb -L../evfs
LOADLIBES := -levfs
EVFSDIR := ../evfs
EVFSLIB := $(EVFSDIR)/libevfs.a
PROG := fsc

all: depend $(PROG)

$(PROG): $(PROG).o set.o $(EVFSLIB)
	$(CC) $(CFLAGS) -o $@ $< set.o $(LOADLIBES)

test: set.o test.o
	$(CC) $(CFLAGS) -o $@ $^

$(EVFSLIB): | libevfs
	
.PHONY: clean
clean:
	rm -f .depend $(PROG) *.o
	
# always execute
.PHONY: libevfs
libevfs:
	cd $(EVFSDIR) && $(MAKE)	
	
depend:
	$(CC) -MM *.c > .depend

ifeq (.depend,$(wildcard .depend))
include .depend
endif	
