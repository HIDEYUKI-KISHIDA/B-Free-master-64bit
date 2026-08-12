# Cross-build gui_server → compositor.elf (B-Free ring3 PID1).
# Invoked by tools/build_compositor_guest_elf.sh — not the host tron_gui_server target.
ROOT ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
GS   ?= $(ROOT)/gui_server

CC      ?= x86_64-elf-gcc
LD      ?= x86_64-elf-ld
OBJCOPY ?= x86_64-elf-objcopy

SRCS := gui_server_main.c fbdev.c input.c input_subsystem.c \
        surface.c surface_ops.c wayland_server_runtime.c

# Optional local extensions (guest syscall shims, serial, crt0).
EXTRA_SRCS :=
ifneq (,$(wildcard $(GS)/guest_compositor_main.c))
EXTRA_SRCS += guest_compositor_main.c
endif
ifneq (,$(wildcard $(GS)/guest_serial.c))
EXTRA_SRCS += guest_serial.c
endif
ifneq (,$(wildcard $(GS)/crt0.S))
EXTRA_SRCS += crt0.S
endif

OBJS := $(SRCS:%=$(GS)/%.o) $(EXTRA_SRCS:%=$(GS)/%.o)
TARGET := $(GS)/compositor.elf

MUSL_INC := $(BFREE_ELF_LIBM_DIR)/prefix/include
CFLAGS := -O2 -Wall -Wextra -g \
	-m64 -mcmodel=large -mno-red-zone -fno-stack-protector \
	-DBFREE_GUEST_COMPOSITOR=1 -DBFREE_COMPOSITOR=1 \
	-I$(GS) -I$(ROOT)/include
ifneq (,$(wildcard $(MUSL_INC)/stdio.h))
CFLAGS += -isystem $(MUSL_INC)
endif

LDFLAGS := -nostdlib -static -Wl,-z,norelro -Wl,-Ttext-segment=0x400000
ifneq ($(CRT0),)
LDFLAGS += $(CRT0)
endif
ifneq ($(LIBC),)
LDFLAGS += $(LIBC)
endif
ifneq ($(LIBM),)
LDFLAGS += $(LIBM)
endif
ifneq ($(LIBGCC),)
LDFLAGS += $(LIBGCC)
endif

.PHONY: all clean
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)
	$(OBJCOPY) -R .comment -R .note.gnu.build-id $@ 2>/dev/null || true
	@echo "[compositor-guest] built: $@"

$(GS)/%.o: $(GS)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(GS)/%.o: $(GS)/%.S
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
