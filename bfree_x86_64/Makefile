ARCH_INC := -Ikernel/arch/x86_64
SYS_INC := -Ikernel/sysmain $(ARCH_INC)

CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c11 -O2 -g
BUILD_DIR := build/host-tests
LDFLAGS ?=

HOST_TESTS := test_p4_dirent_ofd test_p4_unlink_open test_p4_openat test_p4_block_fs \
	test_p4_vfork_exec test_p4_waitid test_p4_fork test_p4_pipe_signal test_p4_shell_cli \
	test_p4_rw_dup2 test_p4_brk_mmap test_p4_elf_load test_p4_clone_futex test_p4_tty_job \
	test_p4_musl_load test_p4_musl_exec \
	test_p5_devnode test_p5_mount test_p5_cred test_p5_net_unix test_p5_ipc_shm test_p5_syscall_gate \
	test_p6_syscall_invoke test_p6_trap_entry \
	test_p7_kernel_main test_p7_trap_setup test_p7_trap_payload test_p7_musl_trap \
	test_p8_paging test_p8_initramfs test_p8_trap_hw test_p8_kernel_boot \
	test_p9_gdt test_p9_user_boot test_p10_dispatch_wiring test_p10_kernel_guest \
	test_p11_musl_guest test_p12_busybox_guest test_p13_guest_regress

PAYLOAD_ELF := $(BUILD_DIR)/payload.elf
MUSL_STATIC_ELF := $(BUILD_DIR)/musl_static.elf
BOOT_SMOKE_ELF := build/boot_smoke.elf
KERNEL_ELF := build/kernel.elf
INITRAMFS_CPIO := build/initramfs.cpio
USER_PAYLOAD_ELF := build/user_payload.elf
USER_PAYLOAD_BIN := guest/initramfs/user_payload.bin
MUSL_INITRAMFS_ELF := guest/initramfs/musl_static.elf
BUSYBOX_ROOTFS := guest/rootfs/bin/busybox
BUSYBOX_INITRAMFS := guest/initramfs/bin/busybox
LTP_OPEN_GUEST_ELF := $(BUILD_DIR)/ltp_open_guest.elf
POSIX_IO_GUEST_ELF := $(BUILD_DIR)/posix_io_guest.elf
LTP_OPEN_INITRAMFS := guest/initramfs/ltp_open_guest.elf
POSIX_IO_INITRAMFS := guest/initramfs/posix_io_guest.elf
TRAP_PAYLOAD_ELF := build/trap_payload.elf
KERNEL_DIR := build/kernel
KERNEL_BOOT_CFLAGS ?=

FREESTANDING_CFLAGS := -Wall -Wextra -std=c11 -O2 -ffreestanding -fno-builtin \
	-fno-pie -fno-stack-protector -mno-red-zone -mcmodel=kernel
KERNEL_INC := -Ikernel/arch/x86_64 -Ikernel/sysmain -Ikernel/freestanding

FS_OBJS := $(BUILD_DIR)/fs_ofd.o $(BUILD_DIR)/blk_vol.o $(BUILD_DIR)/blk_persist.o \
	$(BUILD_DIR)/devnode.o $(BUILD_DIR)/mount.o
PROC_OBJS := $(BUILD_DIR)/process.o $(BUILD_DIR)/vmm.o $(BUILD_DIR)/shell_cli.o \
	$(BUILD_DIR)/guest_io.o $(BUILD_DIR)/elf_load.o $(BUILD_DIR)/elf_host_run.o \
	$(BUILD_DIR)/elf_trap_exec.o $(BUILD_DIR)/thread.o $(BUILD_DIR)/tty.o
M5_OBJS := $(BUILD_DIR)/cred.o $(BUILD_DIR)/net_unix.o $(BUILD_DIR)/ipc_shm.o \
	$(BUILD_DIR)/syscall_dispatch.o
M6_OBJS := $(BUILD_DIR)/syscall.o $(BUILD_DIR)/syscall_entry.o
M7_OBJS := $(BUILD_DIR)/trap_setup.o $(BUILD_DIR)/kernel_main.o
M8_OBJS := $(BUILD_DIR)/paging.o $(BUILD_DIR)/initramfs.o $(BUILD_DIR)/trap_hw.o
COMMON_OBJS := $(FS_OBJS) $(PROC_OBJS) $(M5_OBJS)
P6_OBJS := $(COMMON_OBJS) $(M6_OBJS)
P7_OBJS := $(P6_OBJS) $(M7_OBJS)
P8_OBJS := $(P7_OBJS) $(M8_OBJS)
TRAP_PAYLOAD_OBJS := $(P6_OBJS) $(BUILD_DIR)/trap_setup.o

KERNEL_FREESTANDING_INC := -nostdinc -Ikernel/freestanding/include -DBFREE_KERNEL_GUEST
KERNEL_SYSMAIN_OBJS := $(addprefix $(KERNEL_DIR)/,fs_ofd.o blk_vol.o blk_persist.o devnode.o \
	mount.o cred.o net_unix.o ipc_shm.o syscall_dispatch.o syscall.o process.o vmm.o \
	guest_io.o elf_load.o thread.o tty.o)
KERNEL_FREESTANDING_OBJS := $(KERNEL_DIR)/libc_stubs.o $(KERNEL_DIR)/exec_stubs.o \
	$(KERNEL_DIR)/guest_kernel.o $(KERNEL_DIR)/elf_user_load.o $(KERNEL_DIR)/ash_guest_boot.o

KERNEL_OBJS := $(KERNEL_DIR)/boot_kernel.o $(KERNEL_DIR)/initramfs_embed.o \
	$(KERNEL_DIR)/kernel_boot.o $(KERNEL_DIR)/paging.o $(KERNEL_DIR)/trap_setup.o \
	$(KERNEL_DIR)/trap_hw.o $(KERNEL_DIR)/debugcon.o $(KERNEL_DIR)/initramfs.o \
	$(KERNEL_DIR)/string.o $(KERNEL_SYSMAIN_OBJS) $(KERNEL_FREESTANDING_OBJS) \
	$(KERNEL_DIR)/syscall_entry.o $(KERNEL_DIR)/gdt.o $(KERNEL_DIR)/user_boot.o \
	$(KERNEL_DIR)/user_boot_ring3.o $(KERNEL_DIR)/ash_guest_tramp.o \
	$(KERNEL_DIR)/syscall_insn_entry.o

.PHONY: all host-tests clean musl-static busybox-guest guest-regress boot-smoke kernel boot-trap-payload initramfs

all: host-tests boot-smoke initramfs kernel boot-trap-payload

host-tests: $(PAYLOAD_ELF) $(MUSL_STATIC_ELF) $(addprefix $(BUILD_DIR)/,$(HOST_TESTS))

boot-smoke: $(BOOT_SMOKE_ELF)

kernel: $(KERNEL_ELF)

boot-trap-payload: $(TRAP_PAYLOAD_ELF)

initramfs: $(INITRAMFS_CPIO)

musl-static: $(MUSL_STATIC_ELF)

busybox-guest: $(BUSYBOX_ROOTFS)

guest-regress: $(LTP_OPEN_GUEST_ELF) $(POSIX_IO_GUEST_ELF)

$(INITRAMFS_CPIO): tools/build_initramfs.sh tools/build_initramfs.py \
		guest/initramfs/hello.txt $(USER_PAYLOAD_BIN) $(MUSL_INITRAMFS_ELF) \
		$(BUSYBOX_INITRAMFS) $(LTP_OPEN_INITRAMFS) $(POSIX_IO_INITRAMFS)
	mkdir -p build
	bash tools/build_initramfs.sh

$(MUSL_INITRAMFS_ELF): $(MUSL_STATIC_ELF)
	mkdir -p guest/initramfs
	cp "$<" "$@"

$(USER_PAYLOAD_BIN): $(USER_PAYLOAD_ELF)
	mkdir -p guest/initramfs
	objcopy -O binary $< "$@"

$(BUSYBOX_ROOTFS): tools/build_guest_busybox.sh configs/busybox_m3.config \
		third_party/BUSYBOX_VERSION
	bash tools/build_guest_busybox.sh

$(BUSYBOX_INITRAMFS): $(BUSYBOX_ROOTFS)
	mkdir -p guest/initramfs/bin
	cp "$<" "$@"

$(LTP_OPEN_GUEST_ELF) $(POSIX_IO_GUEST_ELF): tools/build_guest_regress.sh \
		tools/guest_regress/ltp_open_guest.S tools/guest_regress/posix_io_guest.S \
		tools/guest_regress/guest_regress.ld | $(BUILD_DIR)
	bash tools/build_guest_regress.sh

$(LTP_OPEN_INITRAMFS): $(LTP_OPEN_GUEST_ELF)
	mkdir -p guest/initramfs
	cp "$<" "$@"

$(POSIX_IO_INITRAMFS): $(POSIX_IO_GUEST_ELF)
	mkdir -p guest/initramfs
	cp "$<" "$@"

$(USER_PAYLOAD_ELF): tools/user_payload/user_payload.S tools/user_payload/user_payload.ld
	mkdir -p build
	$(CC) $(FREESTANDING_CFLAGS) -nostdlib -static \
		-Wl,-T,tools/user_payload/user_payload.ld -o $@ tools/user_payload/user_payload.S

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(PAYLOAD_ELF): tools/elf_payload.c tools/elf_payload.ld | $(BUILD_DIR)
	$(CC) $(CFLAGS) -nostdlib -static -fno-pie -fno-stack-protector \
		-Wl,-T,tools/elf_payload.ld -Wl,-e,elf_main -o $@ tools/elf_payload.c

$(MUSL_STATIC_ELF): tools/build_musl_static.sh tools/musl_static/hello.S tools/musl_static/hello.ld \
		tools/musl_static/hello.c third_party/MUSL_VERSION | $(BUILD_DIR)
	MUSL_STATIC_ASM_ONLY=1 bash tools/build_musl_static.sh

$(BUILD_DIR)/fs_ofd.o: kernel/sysmain/fs_ofd.c kernel/sysmain/fs_ofd.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/blk_vol.o: kernel/sysmain/blk_vol.c kernel/sysmain/blk_vol.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/blk_persist.o: kernel/sysmain/blk_persist.c kernel/sysmain/blk_persist.h kernel/sysmain/fs_ofd.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/devnode.o: kernel/sysmain/devnode.c kernel/sysmain/devnode.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/mount.o: kernel/sysmain/mount.c kernel/sysmain/mount.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/cred.o: kernel/sysmain/cred.c kernel/sysmain/cred.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/net_unix.o: kernel/sysmain/net_unix.c kernel/sysmain/net_unix.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/ipc_shm.o: kernel/sysmain/ipc_shm.c kernel/sysmain/ipc_shm.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/syscall_dispatch.o: kernel/sysmain/syscall_dispatch.c kernel/sysmain/syscall_dispatch.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/syscall.o: kernel/sysmain/syscall.c kernel/sysmain/syscall.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/syscall_entry.o: kernel/arch/x86_64/syscall_entry.S | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/process.o: kernel/sysmain/process.c kernel/sysmain/process.h kernel/sysmain/vmm.h \
	kernel/sysmain/elf_load.h kernel/sysmain/elf_host_run.h kernel/sysmain/elf_trap_exec.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/vmm.o: kernel/sysmain/vmm.c kernel/sysmain/vmm.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/shell_cli.o: kernel/sysmain/shell_cli.c kernel/sysmain/shell_cli.h kernel/sysmain/process.h kernel/sysmain/vmm.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/guest_io.o: kernel/sysmain/guest_io.c kernel/sysmain/guest_io.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/elf_load.o: kernel/sysmain/elf_load.c kernel/sysmain/elf_load.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/elf_host_run.o: kernel/sysmain/elf_host_run.c kernel/sysmain/elf_host_run.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/thread.o: kernel/sysmain/thread.c kernel/sysmain/thread.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/tty.o: kernel/sysmain/tty.c kernel/sysmain/tty.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/%.o: tools/%.c kernel/sysmain/fs_ofd.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p4_vfork_exec.o: tools/test_p4_vfork_exec.c kernel/sysmain/process.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p4_waitid.o: tools/test_p4_waitid.c kernel/sysmain/process.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p4_fork.o: tools/test_p4_fork.c kernel/sysmain/process.h kernel/sysmain/vmm.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p4_pipe_signal.o: tools/test_p4_pipe_signal.c kernel/sysmain/process.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p4_shell_cli.o: tools/test_p4_shell_cli.c kernel/sysmain/process.h kernel/sysmain/shell_cli.h kernel/sysmain/vmm.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p4_rw_dup2.o: tools/test_p4_rw_dup2.c kernel/sysmain/guest_io.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p4_brk_mmap.o: tools/test_p4_brk_mmap.c kernel/sysmain/vmm.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p4_elf_load.o: tools/test_p4_elf_load.c kernel/sysmain/elf_load.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -DPAYLOAD_ELF_PATH=\"$(PAYLOAD_ELF)\" -c $< -o $@

$(BUILD_DIR)/test_p4_clone_futex.o: tools/test_p4_clone_futex.c kernel/sysmain/thread.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p4_tty_job.o: tools/test_p4_tty_job.c kernel/sysmain/tty.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p4_musl_load.o: tools/test_p4_musl_load.c kernel/sysmain/elf_load.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -DMUSL_STATIC_ELF_PATH=\"$(MUSL_STATIC_ELF)\" -c $< -o $@

$(BUILD_DIR)/test_p4_musl_exec.o: tools/test_p4_musl_exec.c kernel/sysmain/elf_load.h kernel/sysmain/elf_host_run.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -DMUSL_STATIC_ELF_PATH=\"$(MUSL_STATIC_ELF)\" -c $< -o $@

$(BUILD_DIR)/test_p5_devnode.o: tools/test_p5_devnode.c kernel/sysmain/fs_ofd.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p5_mount.o: tools/test_p5_mount.c kernel/sysmain/mount.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p5_cred.o: tools/test_p5_cred.c kernel/sysmain/cred.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p5_net_unix.o: tools/test_p5_net_unix.c kernel/sysmain/net_unix.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p5_ipc_shm.o: tools/test_p5_ipc_shm.c kernel/sysmain/ipc_shm.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p5_syscall_gate.o: tools/test_p5_syscall_gate.c kernel/sysmain/syscall_dispatch.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p6_syscall_invoke.o: tools/test_p6_syscall_invoke.c kernel/sysmain/syscall.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p6_trap_entry.o: tools/test_p6_trap_entry.c kernel/sysmain/syscall.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/trap_setup.o: kernel/arch/x86_64/trap_setup.c kernel/arch/x86_64/trap_setup.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/paging.o: kernel/arch/x86_64/paging.c kernel/arch/x86_64/paging.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/initramfs.o: kernel/sysmain/initramfs.c kernel/sysmain/initramfs.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/trap_hw.o: kernel/arch/x86_64/trap_hw.c kernel/arch/x86_64/trap_hw.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/syscall_invoke_stub.o: tools/syscall_invoke_stub.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel_main.o: kernel/sysmain/kernel_main.c kernel/sysmain/kernel_main.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/elf_trap_exec.o: kernel/sysmain/elf_trap_exec.c kernel/sysmain/elf_trap_exec.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p7_kernel_main.o: tools/test_p7_kernel_main.c kernel/sysmain/kernel_main.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p7_trap_setup.o: tools/test_p7_trap_setup.c kernel/arch/x86_64/trap_setup.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p7_trap_payload.o: tools/test_p7_trap_payload.c kernel/sysmain/syscall.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -DTRAP_PAYLOAD_ELF=\"$(TRAP_PAYLOAD_ELF)\" -c $< -o $@

$(BUILD_DIR)/test_p7_musl_trap.o: tools/test_p7_musl_trap.c kernel/sysmain/elf_trap_exec.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -DMUSL_STATIC_ELF_PATH=\"$(MUSL_STATIC_ELF)\" -c $< -o $@

$(BUILD_DIR)/test_p8_paging.o: tools/test_p8_paging.c kernel/arch/x86_64/paging.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p8_initramfs.o: tools/test_p8_initramfs.c kernel/sysmain/initramfs.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -DINITRAMFS_CPIO_PATH=\"$(INITRAMFS_CPIO)\" -c $< -o $@

$(BUILD_DIR)/test_p8_trap_hw.o: tools/test_p8_trap_hw.c kernel/arch/x86_64/trap_hw.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p8_kernel_boot.o: tools/test_p8_kernel_boot.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -DINITRAMFS_CPIO_PATH=\"$(INITRAMFS_CPIO)\" -c $< -o $@

$(BUILD_DIR)/test_p9_gdt.o: tools/test_p9_gdt.c kernel/arch/x86_64/gdt.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p9_user_boot.o: tools/test_p9_user_boot.c kernel/arch/x86_64/user_boot.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/gdt.o: kernel/arch/x86_64/gdt.c kernel/arch/x86_64/gdt.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/user_boot.o: kernel/arch/x86_64/user_boot.c kernel/arch/x86_64/user_boot.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BOOT_SMOKE_ELF): kernel/arch/x86_64/boot_smoke.S kernel/arch/x86_64/boot_smoke.ld
	mkdir -p build
	$(CC) $(CFLAGS) -nostdlib -static -fno-pie -fno-stack-protector \
		-Wl,-T,kernel/arch/x86_64/boot_smoke.ld -o $@ kernel/arch/x86_64/boot_smoke.S

$(KERNEL_ELF): $(INITRAMFS_CPIO) $(KERNEL_OBJS) kernel/arch/x86_64/kernel.ld
	$(CC) $(FREESTANDING_CFLAGS) $(KERNEL_INC) -nostdlib -static \
		-Wl,-T,kernel/arch/x86_64/kernel.ld -o $@ $(KERNEL_OBJS)

$(KERNEL_DIR):
	mkdir -p $(KERNEL_DIR)

$(KERNEL_DIR)/boot_kernel.o: kernel/arch/x86_64/boot_kernel.S | $(KERNEL_DIR)
	$(CC) $(FREESTANDING_CFLAGS) -c $< -o $@

$(KERNEL_DIR)/initramfs_embed.o: kernel/arch/x86_64/initramfs_embed.S $(INITRAMFS_CPIO) | $(KERNEL_DIR)
	$(CC) $(FREESTANDING_CFLAGS) -c $< -o $@

$(KERNEL_DIR)/kernel_boot.o: kernel/freestanding/kernel_boot.c | $(KERNEL_DIR)
	$(CC) $(FREESTANDING_CFLAGS) $(KERNEL_BOOT_CFLAGS) $(KERNEL_INC) $(KERNEL_FREESTANDING_INC) -c $< -o $@

$(KERNEL_DIR)/elf_user_load.o: kernel/freestanding/elf_user_load.c | $(KERNEL_DIR)
	$(CC) $(FREESTANDING_CFLAGS) $(KERNEL_INC) $(KERNEL_FREESTANDING_INC) -c $< -o $@

$(KERNEL_DIR)/ash_guest_boot.o: kernel/freestanding/ash_guest_boot.c | $(KERNEL_DIR)
	$(CC) $(FREESTANDING_CFLAGS) $(KERNEL_INC) $(KERNEL_FREESTANDING_INC) -c $< -o $@

$(KERNEL_DIR)/libc_stubs.o: kernel/freestanding/libc_stubs.c | $(KERNEL_DIR)
	$(CC) $(FREESTANDING_CFLAGS) $(KERNEL_INC) $(KERNEL_FREESTANDING_INC) -c $< -o $@

$(KERNEL_DIR)/exec_stubs.o: kernel/freestanding/exec_stubs.c | $(KERNEL_DIR)
	$(CC) $(FREESTANDING_CFLAGS) $(KERNEL_INC) $(KERNEL_FREESTANDING_INC) -c $< -o $@

$(KERNEL_DIR)/guest_kernel.o: kernel/freestanding/guest_kernel.c | $(KERNEL_DIR)
	$(CC) $(FREESTANDING_CFLAGS) $(KERNEL_INC) $(KERNEL_FREESTANDING_INC) -c $< -o $@

$(KERNEL_DIR)/%.o: kernel/sysmain/%.c | $(KERNEL_DIR)
	$(CC) $(FREESTANDING_CFLAGS) $(KERNEL_INC) $(KERNEL_FREESTANDING_INC) -c $< -o $@

$(BUILD_DIR)/test_p10_dispatch_wiring.o: tools/test_p10_dispatch_wiring.c kernel/sysmain/syscall.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -c $< -o $@

$(BUILD_DIR)/test_p10_kernel_guest.o: tools/test_p10_kernel_guest.c kernel/freestanding/guest_kernel.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -Ikernel/freestanding -c $< -o $@

$(BUILD_DIR)/guest_kernel_host.o: kernel/freestanding/guest_kernel.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SYS_INC) -Ikernel/freestanding -DBFREE_KERNEL_GUEST -c $< -o $@

$(BUILD_DIR)/debugcon_host_stub.o: tools/debugcon_host_stub.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/arch/x86_64 -c $< -o $@

$(BUILD_DIR)/test_p10_dispatch_wiring: $(BUILD_DIR)/test_p10_dispatch_wiring.o $(P6_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/test_p10_kernel_guest: $(BUILD_DIR)/test_p10_kernel_guest.o $(P6_OBJS) \
	$(BUILD_DIR)/guest_kernel_host.o $(BUILD_DIR)/debugcon_host_stub.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/test_p11_musl_guest.o: tools/test_p11_musl_guest.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -DMUSL_STATIC_ELF_PATH=\"$(MUSL_STATIC_ELF)\" \
		-DMUSL_INITRAMFS_ELF_PATH=\"$(MUSL_INITRAMFS_ELF)\" -c $< -o $@

$(BUILD_DIR)/test_p11_musl_guest: $(BUILD_DIR)/test_p11_musl_guest.o | $(MUSL_INITRAMFS_ELF)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/test_p12_busybox_guest.o: tools/test_p12_busybox_guest.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -DBUSYBOX_ROOTFS_PATH=\"$(BUSYBOX_ROOTFS)\" \
		-DBUSYBOX_INITRAMFS_PATH=\"$(BUSYBOX_INITRAMFS)\" -c $< -o $@

$(BUILD_DIR)/test_p12_busybox_guest: $(BUILD_DIR)/test_p12_busybox_guest.o | $(BUSYBOX_INITRAMFS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/test_p13_guest_regress.o: tools/test_p13_guest_regress.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) \
		-DLTP_OPEN_BUILT_PATH=\"$(LTP_OPEN_GUEST_ELF)\" \
		-DLTP_OPEN_STAGED_PATH=\"$(LTP_OPEN_INITRAMFS)\" \
		-DPOSIX_IO_BUILT_PATH=\"$(POSIX_IO_GUEST_ELF)\" \
		-DPOSIX_IO_STAGED_PATH=\"$(POSIX_IO_INITRAMFS)\" \
		-c $< -o $@

$(BUILD_DIR)/test_p13_guest_regress: $(BUILD_DIR)/test_p13_guest_regress.o \
		| $(LTP_OPEN_INITRAMFS) $(POSIX_IO_INITRAMFS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(KERNEL_DIR)/paging.o: kernel/arch/x86_64/paging.c kernel/arch/x86_64/paging.h | $(KERNEL_DIR)
	$(CC) $(FREESTANDING_CFLAGS) $(KERNEL_INC) -c $< -o $@

$(KERNEL_DIR)/trap_setup.o: kernel/arch/x86_64/trap_setup.c kernel/arch/x86_64/trap_setup.h | $(KERNEL_DIR)
	$(CC) $(FREESTANDING_CFLAGS) $(KERNEL_INC) -c $< -o $@

$(KERNEL_DIR)/trap_hw.o: kernel/arch/x86_64/trap_hw.c kernel/arch/x86_64/trap_hw.h | $(KERNEL_DIR)
	$(CC) $(FREESTANDING_CFLAGS) $(KERNEL_INC) -c $< -o $@

$(KERNEL_DIR)/debugcon.o: kernel/arch/x86_64/debugcon.c kernel/arch/x86_64/debugcon.h | $(KERNEL_DIR)
	$(CC) $(FREESTANDING_CFLAGS) $(KERNEL_INC) -c $< -o $@

$(KERNEL_DIR)/initramfs.o: kernel/sysmain/initramfs.c kernel/sysmain/initramfs.h | $(KERNEL_DIR)
	$(CC) $(FREESTANDING_CFLAGS) $(KERNEL_INC) $(KERNEL_FREESTANDING_INC) -c $< -o $@

$(KERNEL_DIR)/string.o: kernel/freestanding/string.c | $(KERNEL_DIR)
	$(CC) $(FREESTANDING_CFLAGS) $(KERNEL_INC) -c $< -o $@

$(KERNEL_DIR)/syscall_entry.o: kernel/arch/x86_64/syscall_entry.S | $(KERNEL_DIR)
	$(CC) $(FREESTANDING_CFLAGS) -c $< -o $@

$(KERNEL_DIR)/gdt.o: kernel/arch/x86_64/gdt.c kernel/arch/x86_64/gdt.h | $(KERNEL_DIR)
	$(CC) $(FREESTANDING_CFLAGS) $(KERNEL_INC) -c $< -o $@

$(KERNEL_DIR)/user_boot.o: kernel/arch/x86_64/user_boot.c kernel/arch/x86_64/user_boot.h | $(KERNEL_DIR)
	$(CC) $(FREESTANDING_CFLAGS) $(KERNEL_INC) -c $< -o $@

$(KERNEL_DIR)/user_boot_ring3.o: kernel/arch/x86_64/user_boot_ring3.S | $(KERNEL_DIR)
	$(CC) $(FREESTANDING_CFLAGS) -c $< -o $@

$(KERNEL_DIR)/ash_guest_tramp.o: kernel/arch/x86_64/ash_guest_tramp.S | $(KERNEL_DIR)
	$(CC) $(FREESTANDING_CFLAGS) -c $< -o $@

$(KERNEL_DIR)/syscall_insn_entry.o: kernel/arch/x86_64/syscall_insn_entry.S | $(KERNEL_DIR)
	$(CC) $(FREESTANDING_CFLAGS) -c $< -o $@

$(TRAP_PAYLOAD_ELF): tools/trap_payload/trap_payload.c $(TRAP_PAYLOAD_OBJS)
	mkdir -p build
	$(CC) $(CFLAGS) $(SYS_INC) -o $@ tools/trap_payload/trap_payload.c $(TRAP_PAYLOAD_OBJS) $(LDFLAGS)

$(BUILD_DIR)/test_p6_syscall_invoke: $(BUILD_DIR)/test_p6_syscall_invoke.o $(P6_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/test_p6_trap_entry: $(BUILD_DIR)/test_p6_trap_entry.o $(P6_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/test_p7_kernel_main: $(BUILD_DIR)/test_p7_kernel_main.o $(P7_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/test_p7_trap_setup: $(BUILD_DIR)/test_p7_trap_setup.o $(TRAP_PAYLOAD_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/test_p7_trap_payload: $(BUILD_DIR)/test_p7_trap_payload.o $(P7_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/test_p7_musl_trap: $(BUILD_DIR)/test_p7_musl_trap.o $(P7_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/test_p8_paging: $(BUILD_DIR)/test_p8_paging.o $(BUILD_DIR)/paging.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/test_p8_initramfs: $(BUILD_DIR)/test_p8_initramfs.o $(BUILD_DIR)/initramfs.o | $(INITRAMFS_CPIO)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/test_p8_trap_hw: $(BUILD_DIR)/test_p8_trap_hw.o $(BUILD_DIR)/trap_setup.o \
	$(BUILD_DIR)/trap_hw.o $(BUILD_DIR)/syscall_entry.o $(BUILD_DIR)/syscall_invoke_stub.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/test_p8_kernel_boot: $(BUILD_DIR)/test_p8_kernel_boot.o $(BUILD_DIR)/paging.o \
	$(BUILD_DIR)/trap_setup.o $(BUILD_DIR)/trap_hw.o $(BUILD_DIR)/initramfs.o \
	$(BUILD_DIR)/syscall_entry.o $(BUILD_DIR)/syscall_invoke_stub.o | $(INITRAMFS_CPIO)
	$(CC) $(CFLAGS) $(BUILD_DIR)/test_p8_kernel_boot.o $(BUILD_DIR)/paging.o \
		$(BUILD_DIR)/trap_setup.o $(BUILD_DIR)/trap_hw.o $(BUILD_DIR)/initramfs.o \
		$(BUILD_DIR)/syscall_entry.o $(BUILD_DIR)/syscall_invoke_stub.o -o $@ $(LDFLAGS)

$(BUILD_DIR)/test_p9_gdt: $(BUILD_DIR)/test_p9_gdt.o $(BUILD_DIR)/gdt.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/test_p9_user_boot: $(BUILD_DIR)/test_p9_user_boot.o $(BUILD_DIR)/user_boot.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/%: $(BUILD_DIR)/%.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR) $(KERNEL_DIR) build/boot_smoke.elf build/kernel.elf \
		build/trap_payload.elf build/initramfs.cpio
