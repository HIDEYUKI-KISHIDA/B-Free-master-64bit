# $Revision: 1.1 $
#
#

DEBUG=-DDEBUG
IDENT=-DI386 -DIBMPC

CPP=/lib/cpp
ROOT=../../../ITRON
## kernlib依存を除去し、lib等の共通部に合わせる
CFLAGS=-I. -I$(ROOT) -I$(ROOT)/h -I$(ROOT)/i386 -fno-builtin $(IDENT) $(DEBUG)
CC=i686-w64-mingw32-gcc
AS=i686-w64-mingw32-as
KP=kp -input-code euc
MKDEPEND=$(CC) -M
STARTADDR=0x80040000
ENTRY=start
LIBS=../../../ITRON/kernlib/libkernel.a
#LD=/usr/local/bin/ld
LD=$(CC)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.s
	$(CPP) $(ASFLAGS) $*.s | $(AS) -o $*.o

OBJS=gname.o misc.o
SRCS=gname.c misc.c

all: gname

gname: $(OBJS)
	$(LD) -static -nostdlib -Wl,-e,$(ENTRY) -Wl,-Ttext=$(STARTADDR) -o $@ $(OBJS) $(LIBS)

tags:
	etags *.[hcs]

depend:
	$(MKDEPEND)  $(CFLAGS) -x c $(SRCS) > .depend

clean:
	rm -f $(OBJS) gname
