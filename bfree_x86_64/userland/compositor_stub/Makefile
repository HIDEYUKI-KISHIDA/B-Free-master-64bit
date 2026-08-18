# init_tramp.elf = PID1 (INIT). compositor.elf = APP after exec_initrd.
# wl_client.elf is mapped as hello.elf on the stub ISO (g1-desk named module).
# Do not rebuild the daily kernel. Do not BFREE_BOOT_GUI_FIRST.
.PHONY: all clean
.DEFAULT_GOAL := all

CC = x86_64-elf-gcc
CFLAGS = -ffreestanding -fno-pic -fno-stack-protector -O2 -Wall -mno-sse -mno-mmx -mno-3dnow -mno-80387
LD_COMP = -nostdlib -static -Wl,-Ttext-segment=0x500000 -Wl,-z,norelro -e _start
LD_INIT = -nostdlib -static -Wl,-Ttext-segment=0x400000 -Wl,-z,norelro -e _start

all: compositor.elf init_tramp.elf wl_client.elf

compositor.elf: compositor_stub.c
	$(CC) $(CFLAGS) $(LD_COMP) -o $@ $<
	file $@ || true

# Same source, client _start. Load VA 0x400000 — child's private AS, not compositor.
wl_client.elf: compositor_stub.c
	$(CC) $(CFLAGS) -DWL_AS_CLIENT $(LD_INIT) -o $@ $<
	file $@ || true

init_tramp.elf: init_trampoline.c
	$(CC) $(CFLAGS) $(LD_INIT) -o $@ $<
	file $@ || true

clean:
	rm -f compositor.elf init_tramp.elf wl_client.elf
