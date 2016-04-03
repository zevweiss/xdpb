
CC = cc
CFLAGS = -Wall

INC = /usr/include
SYSTREE = sys/tree.h
BSDINC = $(INC)/bsd

# Crude simulation of './configure'...
exists = $(shell [ -e $1 ] && echo yes || echo no)

ifeq ($(call exists, $(INC)/$(SYSTREE)),yes)
# nothing to do here, #include <sys/tree.h> works by default
else ifeq ($(call exists, $(BSDINC)/$(SYSTREE)),yes)
CFLAGS += -idirafter $(BSDINC)
else
$(error no sys/tree.h header found)
endif

ifneq ($(DEBUG),)
CFLAGS += -ggdb3 -DDEBUG=1
else
CFLAGS += -O2
endif

XSUBLIBS = x11 xi xfixes xrandr
XCFLAGS := $(shell pkg-config --cflags --libs $(XSUBLIBS))

CFLAGS += $(XCFLAGS)

default: xdpb

xdpb: xdpb.c
	$(CC) $(CFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f xdpb
