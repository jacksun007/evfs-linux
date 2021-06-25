#
# Makefile
#
# 06/25/2021: Updated to use wildcard and pattern rules so we don't have to 
#             add new entries every single time. Should not need to be changed
#             ever again.
#

CC := gcc
EVFSDIR := ../evfs
EVFSLIB := $(EVFSDIR)/libevfs.a
LOADLIBES := -levfs
CFLAGS := -Wall -Wextra -Werror -I$(EVFSDIR)/include -L../evfs -ggdb
PROG := $(patsubst %.c,%,$(wildcard *.c))

all: depend libevfs $(PROG)

define PROG_RULE
$(1): $(1).o $(EVFSLIB)
	$(CC) $(CFLAGS) -o $(1) $(1).o $(LOADLIBES)
endef 
    
$(foreach P,$(PROG),$(eval $(call PROG_RULE,$(P)))) 
    
# always execute
.PHONY: libevfs
libevfs:
	cd $(EVFSDIR) && $(MAKE)

.PHONY: clean
clean:
	rm -f .depend $(PROG) *.o

depend:
	$(CC) -MM *.c > .depend

ifeq (.depend,$(wildcard .depend))
include .depend
endif	
