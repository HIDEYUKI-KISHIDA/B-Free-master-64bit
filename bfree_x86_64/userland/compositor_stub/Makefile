# Guest compositor.elf stub (PID1 only with a separate GUI_FIRST kernel).
# Daily ISO stays BFREE_BOOT_GUI_FIRST=0 + desktop.elf g1-start0.
.PHONY: all clean
.DEFAULT_GOAL := all

CC = x86_64-elf-gcc
CFLAGS = -ffreestanding -fno-pic -fno-stack-protector -O2 -Wall
LDFLAGS = -nostdlib -static -Wl,-Ttext-segment=0x500000 -Wl,-z,norelro -e _start

TARGET = compositor.elf

all: $(TARGET)

$(TARGET): compositor_stub.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
	file $@ || true

clean:
	rm -f $(TARGET)
