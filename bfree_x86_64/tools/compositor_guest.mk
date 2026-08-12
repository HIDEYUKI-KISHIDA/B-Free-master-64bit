# Cross-build gui_server → compositor.elf (B-Free ring3 PID1).
# Invoked by tools/build_compositor_guest_elf.sh — not the host tron_gui_server target.
ROOT ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
GS   ?= $(ROOT)/gui_server
GSG  ?= $(ROOT)/tools/gui_server_guest
STUB ?= $(ROOT)/tools/cross-stubs

CC      ?= x86_64-elf-gcc
LD      ?= x86_64-elf-ld
OBJCOPY ?= x86_64-elf-objcopy

# Guest fbdev replaces host Linux /dev/fb0 implementation.
FBDEV_SRC := $(GSG)/fbdev_guest.c
CORE_SRCS := gui_server_main.c input.c input_subsystem.c \
             surface.c surface_ops.c wayland_server_runtime.c
SRCS := $(CORE_SRCS) $(notdir $(FBDEV_SRC))

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

ALL_SRCS := $(SRCS) $(EXTRA_SRCS)
OBJS := $(patsubst %.c,$(GS)/%.o,$(filter %.c,$(ALL_SRCS)))
OBJS := $(filter-out $(GS)/fbdev_guest.o,$(OBJS))
OBJS += $(GSG)/fbdev_guest.o
OBJS += $(patsubst %.S,$(GS)/%.o,$(filter %.S,$(ALL_SRCS)))
TARGET := $(GS)/compositor.elf

$(foreach s,$(CORE_SRCS) $(EXTRA_SRCS),$(if $(wildcard $(GS)/$(s)),,$(error missing $(GS)/$(s))))
$(if $(wildcard $(FBDEV_SRC)),,$(error missing $(FBDEV_SRC)))

MUSL_INC := $(BFREE_ELF_LIBM_DIR)/prefix/include
CFLAGS := -O2 -Wall -Wextra -g \
	-m64 -mcmodel=large -mno-red-zone -fno-stack-protector \
	-DBFREE_GUEST_COMPOSITOR=1 -DBFREE_COMPOSITOR=1 \
	-I$(STUB) -I$(ROOT)/include -I$(GS)
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

$(GSG)/%.o: $(GSG)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(GS)/%.o: $(GS)/%.S
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
