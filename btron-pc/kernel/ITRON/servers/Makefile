#
# @(#) $Header: /cvsroot/bfree-info/B-Free/Program/btron-pc/kernel/ITRON/servers/Makefile,v 1.1 2011/12/27 17:13:35 liu1 Exp $
#
#

TARGET =	port-manager
LOAD_POINT =	0x00010000
ENTRY =	_main
# Prefer the 32-bit MinGW toolchain shipped with MSYS2.
LD = /mingw32/bin/ld

CC = /mingw32/bin/gcc
INC=-I../../ITRON/h -I../../ITRON/i386
CFLAGS=$(INC) -fno-builtin -fno-leading-underscore


OBJS=port-manager.o database.o


all: $(TARGET)


$(TARGET): $(OBJS)
	$(LD) -static --image-base=$(LOAD_POINT) -e $(ENTRY) -o $(TARGET) $(OBJS) ../kernlib/libkernel.a


clean:
	rm -f $(OBJS) $(TARGET)


database.o: port-manager.h

port-manager.o: port-manager.h
