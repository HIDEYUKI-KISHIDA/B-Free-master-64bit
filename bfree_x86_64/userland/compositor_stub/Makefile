# init_tramp.elf = PID1 (INIT). compositor.elf = APP after exec_initrd.
# wl_client.elf is mapped as hello.elf (fallback). qt_wl_client.elf is mapped
# as p8test.elf unless qt_wl_hello.elf (real QGuiApplication) exists.
# Do not rebuild the daily kernel. Do not BFREE_BOOT_GUI_FIRST.
# Do not add qt_wl_hello.elf to `all` — that needs the guest Qt prefix.
.PHONY: all clean
.DEFAULT_GOAL := all

CC = x86_64-elf-gcc
CFLAGS = -ffreestanding -fno-pic -fno-stack-protector -O2 -Wall -mno-sse -mno-mmx -mno-3dnow -mno-80387
LD_COMP = -nostdlib -static -Wl,-Ttext-segment=0x500000 -Wl,-z,norelro -e _start
LD_INIT = -nostdlib -static -Wl,-Ttext-segment=0x400000 -Wl,-z,norelro -e _start

all: compositor.elf init_tramp.elf wl_client.elf qt_wl_client.elf wl_stub_flush.o

# BFREE_D3=1: compositor vfork-execs desktop.elf (Wayland env) before p8test.
BFREE_D3 ?= 0
ifeq ($(BFREE_D3),1)
D3_CFLAGS := -DBFREE_D3_DESKTOP
endif

compositor.elf: compositor_stub.c
	$(CC) $(CFLAGS) $(D3_CFLAGS) $(LD_COMP) -o $@ $<
	file $@ || true

# Same source, client _start. Load VA 0x400000 — child's private AS, not compositor.
wl_client.elf: compositor_stub.c
	$(CC) $(CFLAGS) -DWL_AS_CLIENT $(LD_INIT) -o $@ $<
	file $@ || true

# Qt Wayland client slot: same wire, QT_QPA_PLATFORM=wayland. Not desktop.elf.
qt_wl_client.elf: compositor_stub.c
	$(CC) $(CFLAGS) -DWL_AS_CLIENT -DWL_QT_CLIENT $(LD_INIT) -o $@ $<
	file $@ || true

# Helpers for qt_wl_hello.elf (linked only when guest Qt exists).
wl_stub_flush.o: compositor_stub.c wl_stub_client.h
	$(CC) $(CFLAGS) -DWL_AS_CLIENT -DWL_QT_CLIENT -DWL_STUB_LIB -c -o $@ $<

init_tramp.elf: init_trampoline.c
	$(CC) $(CFLAGS) $(LD_INIT) -o $@ $<
	file $@ || true

clean:
	rm -f compositor.elf init_tramp.elf wl_client.elf qt_wl_client.elf wl_stub_flush.o
	rm -f qt_wl_hello.o qbfree_wayland.o qt_wl_hello.link.log qt_wl_hello_syms.o
	rm -f guest_link_compat_hello.o
