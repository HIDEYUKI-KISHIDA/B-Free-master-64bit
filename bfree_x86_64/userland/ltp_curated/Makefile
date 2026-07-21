# Curated LTP-style subset for B-Free guest (Multiboot module, text at 0x500000).
.PHONY: all clean
.DEFAULT_GOAL := all

CC = musl-gcc
CFLAGS = -static -O2 -Wall -Wextra
LDFLAGS = -Wl,-z,norelro -Wl,-Ttext-segment=0x500000

TARGET = ltp_curated.elf

all: $(TARGET)

$(TARGET): ltp_curated.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
	file $@ || true

clean:
	rm -f $(TARGET)
