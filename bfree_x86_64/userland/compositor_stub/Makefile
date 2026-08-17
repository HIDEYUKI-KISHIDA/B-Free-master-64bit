# Guest compositor.elf. Do not rebuild the daily kernel.
# Clone daily ISO and register this ELF as init.elf (PID1, INIT role).
# In-process Wayland wire + wl_shm blit (not a second client ELF).
.PHONY: all clean
.DEFAULT_GOAL := all

CC = x86_64-elf-gcc
CFLAGS = -ffreestanding -fno-pic -fno-stack-protector -O2 -Wall -mno-sse -mno-mmx -mno-3dnow -mno-80387
LDFLAGS = -nostdlib -static -Wl,-Ttext-segment=0x500000 -Wl,-z,norelro -e _start

TARGET = compositor.elf

all: $(TARGET)

$(TARGET): compositor_stub.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
	file $@ || true

clean:
	rm -f $(TARGET)
