#
#
#
#


# START_ADDRが未定義ならデフォルト値0x9000をセット
ifndef START_ADDR
START_ADDR=0x9000
endif

CC?=gcc
LD?=ld

# Optional: use an i686-elf cross toolchain (ELF32) when available
# Enable by invoking: make ELF_TOOLCHAIN=1
ELF_TOOLCHAIN?=0
USE_MINGW32?=0
ifeq ($(ELF_TOOLCHAIN),1)
CC=/mingw32/bin/gcc.exe
LD=/mingw32/bin/ld.exe
endif
ifeq ($(USE_MINGW32),1)
CC:=/c/msys64/mingw32/bin/gcc.exe
LD:=/c/msys64/mingw32/bin/ld.exe
endif
GSTRIP = /usr/bin/strip
RM = /bin/rm

CSRC=main.c console.c keyboard.c memory.c \
	date.c idt.c cinterrupt.c fd.c \
	misc.c dma.c page.c version.c lib.c ide.c \
	vga.c evaluate.c memory64.c page64.c gdt_idt_64.c shell.c


OBJ32= start32.o main.o console.o keyboard.o memory.o  \
	date.o io.o idt.o interrupt.o cinterrupt.o fd.o \
	misc.o dma.o page.o version.o lib.o ide.o sfs.o \
	vga.o evaluate.o endmark.o

# start32.oだけ-m32を外す
start32.o: start32.S
	as $< -o $@

OBJ64= start64.o main.o main64.o console.o keyboard.o memory.o \
	date.o io.o idt.o interrupt.o cinterrupt.o fd.o \
	misc.o dma.o page.o version.o lib.o ide.o sfs.o \
	vga.o evaluate.o memory64.o page64.o gdt_idt_64.o shell.o
LIBS =

OBJ16 = start16.o
# Select builder tool based on toolchain

INCPATH = -I.

# -m32を削除（mingw32専用gccで32bitビルドするため）
CFLAGS=-c -msoft-float -fno-builtin -Wall -D__LINUX__ -DSUPPORT_144 $(DEBUG) -DIBMPC $(HDBOOT) $(INCPATH)
# 32ビット強制: -m32 を常に付与
ifeq ($(ELF_TOOLCHAIN),1)
CFLAGS+= -ffreestanding
LDOPT32 = -n -Ttext=$(START_ADDR) -static -e start32 -m elf_i386
else
LDOPT32 = -n -Ttext=$(START_ADDR) -static -e start32
endif
CFLAGS64=-c -msoft-float -fno-builtin -Wall -D__LINUX__ -DSUPPORT_144 -D__x86_64__ $(DEBUG) -DIBMPC $(HDBOOT) $(INCPATH) -fPIC
LDOPT64 = -n -Ttext=$(START_ADDR64) -static -e start64 -m elf_x86_64


mode32: $(OBJ32)
ifeq ($(ELF_TOOLCHAIN),1)
	$(LD) $(LDOPT32) -o mode32 $(OBJ32) $(LIBS)
	cp mode32 mode32.sym
	$(GSTRIP) mode32
else ifeq ($(USE_MINGW32),1)
	# Link PE32 via gcc driver, then convert to flat binary with 32-byte header
	$(CC) -Wl,--image-base,0 -Wl,-e,start32 -Wl,-Ttext=$(START_ADDR) -Wl,--unresolved-symbols=ignore-all -o mode32.pe $(OBJ32) $(LIBS)
	objcopy -O binary mode32.pe mode32.bin
	dd if=/dev/zero bs=1 count=32 of=mode32 1>/dev/null 2>&1
	cat mode32.bin >> mode32
	rm -f mode32.pe mode32.bin
else
	@echo "Please build with ELF_TOOLCHAIN=1またはUSE_MINGW32=1でビルドしてください"; exit 1
endif

2ndboot32: $(TOOLS) start16 mode32
	./build_boot2_elf start16 mode32 > 2ndboot32

# --- 2ndboot, build_boot2, build_boot2_elf関連ターゲットを完全に除去 ---

# mode32単体ターゲットのみでビルド可能な最小構成に


iso64: 2ndboot64 mkiso
	./mkiso 2ndboot64 bfree-64bit.iso

iso: iso64

tags:
	$(ETAGS) $(CSRC)

mkdepend:
	$(CC) -M -D__LINUX__ $(CSRC) > DEPEND

#######################################################################
#
#		T O O L S
#

## build_boot2: build_boot2.c
##	$(CC) -DGCC_USE -o build_boot2 build_boot2.c

# build_boot2: build_boot2.c
#	$(CC) -DGCC_USE -o build_boot2 build_boot2.c

build_boot2_elf: build_boot2_elf.c
	$(CC) -D__LINUX__ -DGCC_USE -o build_boot2_elf build_boot2_elf.c


mkiso: mkiso.c
	$(CC) -o mkiso mkiso.c

build_boot2_elf: build_boot2_elf.c
	$(CC) -D__LINUX__ -DGCC_USE -o build_boot2_elf build_boot2_elf.c


#include DEPEND




