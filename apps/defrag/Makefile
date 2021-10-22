#
# Makefile
#

CC := gcc
CFLAGS := -Wall -Wextra -Werror -I../evfs/include -ggdb -L../evfs
LOADLIBES := -levfs -lpopt
EVFSDIR := ../evfs
EVFSLIB := $(EVFSDIR)/libevfs.a
PROG := defrag

all: depend $(PROG)

$(PROG): defrag.o $(EVFSLIB)
	$(CC) $(CFLAGS) -o $@ $< $(LOADLIBES)

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
