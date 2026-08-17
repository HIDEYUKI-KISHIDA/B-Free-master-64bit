# STOS kernel bring-up (Phase 2 QEMU / Phase 3 lena Image)
#
#   make BOARD=qemu_virt
#   make BOARD=qemu_virt qemu
#   make BOARD=lena

BOARD        ?= qemu_virt
CROSS        ?= aarch64-linux-gnu-
CC           := $(CROSS)gcc
OBJCOPY      := $(CROSS)objcopy
BUILD        := build/$(BOARD)

ifeq ($(BOARD),qemu_virt)
  CPPFLAGS   += -DSTOS_BOARD_QEMU_VIRT=1
  LINK_BASE  := 0x40000000
else ifeq ($(BOARD),lena)
  CPPFLAGS   += -DSTOS_BOARD_LENA=1
  LINK_BASE  := 0xA4000000
else
  $(error BOARD must be qemu_virt or lena)
endif

CPPFLAGS += -I../bsp/include -Ibringup
CFLAGS   := -ffreestanding -fno-builtin -fno-pic -fno-pie \
            -fno-stack-protector -mgeneral-regs-only \
            -nostdlib -Wall -Wextra -Werror -O2
ASFLAGS  := $(CFLAGS) $(CPPFLAGS)
LDFLAGS  := -nostdlib -static -Wl,--build-id=none \
            -Wl,--defsym=STOS_LINK_BASE=$(LINK_BASE) \
            -T bringup/linker.ld

OBJS := $(BUILD)/head.o $(BUILD)/uart.o $(BUILD)/main.o

.PHONY: all clean qemu check

all: $(BUILD)/stos.Image

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/head.o: bringup/head.S bringup/board.h | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD)/uart.o: bringup/uart.c bringup/uart.h bringup/board.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/main.o: bringup/main.c bringup/uart.h bringup/board.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD)/stos.elf: $(OBJS) bringup/linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJS) -o $@

$(BUILD)/stos.Image: $(BUILD)/stos.elf
	$(OBJCOPY) -O binary $< $@
	@echo "built $@  BOARD=$(BOARD)  LINK_BASE=$(LINK_BASE)"

qemu: $(BUILD)/stos.Image
	@test "$(BOARD)" = "qemu_virt" || (echo "qemu target requires BOARD=qemu_virt" >&2; exit 1)
	timeout --signal=TERM 3 qemu-system-aarch64 \
		-machine virt \
		-cpu cortex-a72 \
		-m 128M \
		-nographic \
		-kernel $(BUILD)/stos.Image \
		|| true

check:
	$(MAKE) BOARD=qemu_virt all
	$(MAKE) BOARD=lena all
	../tools/check_bsp.py
	../tools/run_qemu_bringup.sh

clean:
	rm -rf build
