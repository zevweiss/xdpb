
CC = gcc
CFLAGS = -Wall $(shell pkg-config --cflags --libs x11 xi xfixes)

xdpb: xdpb.c
	$(CC) $(CFLAGS) -o $@ $<
