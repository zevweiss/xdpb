
CC = cc
CFLAGS = -Wall

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
	$(CC) -o $@ $< $(CFLAGS)

.PHONY: clean
clean:
	rm -f xdpb
