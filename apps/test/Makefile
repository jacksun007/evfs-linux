#
# Makefile
#
# upper level Makefile for remaking library and then the test programs
#
# TODO: currently does not support making just one program (e.g., make iinfo)
#

EVFSDIR := ../evfs
EVFSLIB := $(EVFSDIR)/libevfs.a
PROG := $(patsubst %.c,%,$(wildcard *.c))

all: depend libevfs
	$(MAKE) -f test.mk
    
# always execute
.PHONY: libevfs
libevfs:
	cd $(EVFSDIR) && $(MAKE)

.PHONY: clean
clean:
	rm -f .depend $(PROG) *.o

depend:
	$(CC) -MM *.c > .depend

