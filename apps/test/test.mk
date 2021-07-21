#
# test.mk
#
# 06/25/2021: Updated to use wildcard and pattern rules so we don't have to 
#             add new entries every single time. Should not need to be changed
#             ever again.
# 07/14/2021: Split into two files to deal with external dependencies
#
#

CC := gcc
EVFSDIR := ../evfs
EVFSLIB := $(EVFSDIR)/libevfs.a
LOADLIBES := -levfs
CFLAGS := -Wall -Wextra -Werror -I$(EVFSDIR)/include -L../evfs -ggdb
PROG := $(filter-out common,$(patsubst %.c,%,$(wildcard *.c)))

all: $(PROG)

define PROG_RULE
$(1): $(1).o common.o $(EVFSLIB)
	$(CC) $(CFLAGS) -o $(1) $(1).o common.o $(LOADLIBES)
endef 
    
$(foreach P,$(PROG),$(eval $(call PROG_RULE,$(P)))) 

ifeq (.depend,$(wildcard .depend))
include .depend
endif	
