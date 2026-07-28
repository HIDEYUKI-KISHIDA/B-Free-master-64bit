# Curated musl libc-test–style subset (Multiboot module, text at 0x500000).
.PHONY: all clean
.DEFAULT_GOAL := all

CC = musl-gcc
CFLAGS = -static -O2 -Wall -Wextra
LDFLAGS = -Wl,-z,norelro -Wl,-Ttext-segment=0x500000

TARGET = libc_test_curated.elf

all: $(TARGET)

$(TARGET): libc_test_curated.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -lm
	file $@ || true

clean:
	rm -f $(TARGET)
