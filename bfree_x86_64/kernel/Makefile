# T-Kernel 2.0 x86_64 kernel/Makefile (仕様書v2.4準拠)

.PHONY: all install clean
.DEFAULT_GOAL := all
.RECIPEPREFIX := >
#
# [必須ビルドフラグ]
#   -m64 -mcmodel=large -mno-red-zone -fno-stack-protector -fno-pic -ffreestanding -nostdlib
# [設計注意]
#   - ページテーブル等は必ず.data配置
#   - スタックは16バイトアライン
#   - GAS(AT&T)構文のみ使用
#
# Override with: make BFREE_ELF_GCC_ROOT=/root/bfree-native-build/x86_64-elf-gcc-full \
#                   BFREE_ELF_BINUTILS_DIR=/root/bfree-native-build/x86_64-elf-binutils/bin
# Auto-detect Lordmilko toolchain: gcc built with --prefix=/usr/local/x86_64-elf invokes
# /usr/local/x86_64-elf/bin/as (Exec format error on WSL when that path is a Windows binutils).
ifeq ($(BFREE_ELF_GCC_ROOT),)
ifneq ($(wildcard /root/x86_64-elf-toolchain/bin/x86_64-elf-gcc),)
BFREE_ELF_GCC_ROOT := /root/x86_64-elf-toolchain
else ifneq ($(wildcard $(HOME)/x86_64-elf-toolchain/bin/x86_64-elf-gcc),)
BFREE_ELF_GCC_ROOT := $(HOME)/x86_64-elf-toolchain
endif
endif
ifneq ($(BFREE_ELF_GCC_ROOT),)
ifneq ($(wildcard $(BFREE_ELF_GCC_ROOT)/bin/x86_64-elf-gcc),)
TOOLBIN := $(BFREE_ELF_GCC_ROOT)/bin
else
TOOLBIN := $(BFREE_ELF_GCC_ROOT)/x86_64-elf/bin
endif
CC      := $(TOOLBIN)/x86_64-elf-gcc
AS      := $(TOOLBIN)/x86_64-elf-gcc
else
CC      = x86_64-elf-gcc
AS      = x86_64-elf-gcc
endif
ifneq ($(BFREE_ELF_BINUTILS_DIR),)
LD      := $(BFREE_ELF_BINUTILS_DIR)/x86_64-elf-ld
OBJCOPY := $(BFREE_ELF_BINUTILS_DIR)/x86_64-elf-objcopy
else ifneq ($(BFREE_ELF_GCC_ROOT),)
LD      := $(TOOLBIN)/x86_64-elf-ld
OBJCOPY := $(TOOLBIN)/x86_64-elf-objcopy
else
LD      = x86_64-elf-ld
OBJCOPY = x86_64-elf-objcopy
endif
PYTHON  ?= python3
PROFILE ?= DEBUG
DEBUG ?= 1
RELEASE ?= 0
ifeq ($(PROFILE),RELEASE)
DEBUG := 0
RELEASE := 1
endif

CFLAGS  = -m64 -mcmodel=large -mno-red-zone -fno-stack-protector -fno-pic -ffreestanding -nostdlib -Wall -Wextra -Werror=implicit-function-declaration -mno-sse -mno-mmx -mno-3dnow -I../include -Iinclude -Isysmain -Isysdepend -I../../../kernel
ASFLAGS = --64
LDFLAGS = -melf_x86_64 -T sysdepend/x86_64/kernel.lnk --no-undefined -z noexecstack
ENABLE_RUNTIME_NET ?= 0
CFLAGS += -DENABLE_RUNTIME_NET=$(ENABLE_RUNTIME_NET)

BFREE_STRICT_BOOT_VERIFY ?= 0
BFREE_ENFORCE_WX ?= 0
BFREE_ENABLE_ASLR ?= 0
BFREE_BOOT_GUI_FIRST ?= 0
BFREE_WAYLAND_INPUT_STRICT ?= 0
BFREE_PERSIST_FAT_PROBE ?= 0

ifeq ($(RELEASE),1)
BFREE_ENFORCE_WX ?= 1
BFREE_ENABLE_ASLR ?= 1
BFREE_WAYLAND_INPUT_STRICT ?= 1
endif

CFLAGS += -DBFREE_STRICT_BOOT_VERIFY=$(BFREE_STRICT_BOOT_VERIFY)
CFLAGS += -DBFREE_ENFORCE_WX=$(BFREE_ENFORCE_WX)
CFLAGS += -DBFREE_ENABLE_ASLR=$(BFREE_ENABLE_ASLR)
CFLAGS += -DBFREE_BOOT_GUI_FIRST=$(BFREE_BOOT_GUI_FIRST)
CFLAGS += -DBFREE_WAYLAND_INPUT_STRICT=$(BFREE_WAYLAND_INPUT_STRICT)
CFLAGS += -DBFREE_PERSIST_FAT_PROBE=$(BFREE_PERSIST_FAT_PROBE)
CFLAGS += -DDEBUG=$(DEBUG) -DRELEASE=$(RELEASE)

USER_HELLO_DIR := ../userland/embedded
USER_HELLO_ELF := $(USER_HELLO_DIR)/user_hello.elf
USER_HELLO_OBJ := $(USER_HELLO_DIR)/user_hello.o
USER_HELLO_SRC := $(USER_HELLO_DIR)/user_hello.S
USER_HELLO_LD := $(USER_HELLO_DIR)/user_hello.ld
USER_HELLO_EMBED := sysmain/user_hello_elf.c
USER_HELLO_EMBED_TOOL := ../tools/embed_binary.py

ASMS := ../multiboot2_header.S \
	$(wildcard sysmain/*.S) \
	$(wildcard sysdepend/cpu/x86_64/*.S) \
	sysmain/sysdepend/x86_64/reset.S

SRCS := $(wildcard sysmain/*.c) \
	$(wildcard sysdepend/cpu/x86_64/*.c) \
	$(wildcard sysmain/sysdepend/x86_64/*.c) \
	string.c device.c fbdev.c vbe_gop.c gpu_backend.c virtio_gpu.c cache.c devfs.c mouse.c fb_splash.c fb_splash_brand_data.c time_x86_64.c pci.c

ifeq ($(ENABLE_RUNTIME_NET),1)
# Guest LAN stack lives one tree above Program/ (B-Free-master/kernel).
BFREE_NET_ROOT := $(abspath $(CURDIR)/../../../kernel)
SRCS += netdrv_runtime.c devnet0_runtime.c net_runtime.c net_tcp_min.c
SRCS += $(BFREE_NET_ROOT)/net_udp.c $(BFREE_NET_ROOT)/net_ipv4.c \
        $(BFREE_NET_ROOT)/net_arp.c $(BFREE_NET_ROOT)/net_icmp.c
CFLAGS += -I$(BFREE_NET_ROOT) -I$(abspath $(CURDIR)/../../x86_64/kernel)
endif

OBJS := $(sort $(SRCS:.c=.o) $(ASMS:.S=.o))

all: $(USER_HELLO_EMBED) kernel.elf kernel.bin

$(USER_HELLO_OBJ): $(USER_HELLO_SRC)
>$(AS) $(CFLAGS) -c $< -o $@

$(USER_HELLO_ELF): $(USER_HELLO_OBJ) $(USER_HELLO_LD)
>$(LD) -melf_x86_64 -T $(USER_HELLO_LD) -o $@ $(USER_HELLO_OBJ)

$(USER_HELLO_EMBED): $(USER_HELLO_ELF) $(USER_HELLO_EMBED_TOOL)
>$(PYTHON) $(USER_HELLO_EMBED_TOOL) $(USER_HELLO_ELF) $@ user_hello_elf

sysmain/syscall_handler.o: sysmain/syscall_handler.c
>$(CC) $(CFLAGS) -mno-sse -mno-mmx -mno-3dnow -mno-80387 -c $< -o $@

kernel.elf: $(OBJS)
>$(LD) $(LDFLAGS) -o $@ sysmain/sysdepend/x86_64/reset.o $(filter-out sysmain/sysdepend/x86_64/reset.o,$(OBJS))

kernel.bin: kernel.elf
>$(OBJCOPY) -O binary $< $@

install: kernel.elf kernel.bin
>mkdir -p ../iso_root/boot
>cp kernel.elf ../iso_root/boot/kernel.elf
>cp kernel.bin ../iso_root/boot/kernel.bin

%.o: %.c
>$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
>$(AS) $(CFLAGS) -c $< -o $@

clean:
>rm -f $(OBJS) kernel.elf kernel.bin
