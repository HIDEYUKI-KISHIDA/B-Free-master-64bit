# 64bit B-Free OS minimal build Makefile (boot64.S + kernel)

CROSS=nasm
CC=gcc
LD=ld
OBJCOPY=objcopy
CFLAGS=-ffreestanding -m64 -nostdlib -fno-builtin -fno-stack-protector -Wall -Wextra
LDFLAGS=-T linker64.ld -nostdlib

all: boot64.img

boot64.bin: boot64.S
	$(NASM) -f bin -o boot64.bin boot64.S

kernel64.elf: ../kernel64/main.c
	$(CC) $(CFLAGS) -c ../kernel64/main.c -o main.o
	$(LD) $(LDFLAGS) -o kernel64.elf main.o

kernel64.bin: kernel64.elf
	$(OBJCOPY) -O binary kernel64.elf kernel64.bin

boot64.img: boot64.bin kernel64.bin
	cat boot64.bin kernel64.bin > boot64.img

clean:
	rm -f *.bin *.o *.elf *.img
