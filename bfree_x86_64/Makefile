# Host-side unit tests for bfree_x86_64 guest FS/OFD layer.

CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c11 -O2 -g
BUILD_DIR := build/host-tests
LDFLAGS ?=

HOST_TESTS := test_p4_dirent_ofd test_p4_unlink_open test_p4_openat test_p4_block_fs \
	test_p4_vfork_exec test_p4_waitid test_p4_fork test_p4_pipe_signal test_p4_shell_cli \
	test_p4_rw_dup2 test_p4_brk_mmap test_p4_elf_load test_p4_clone_futex test_p4_tty_job \
	test_p5_devnode test_p5_mount test_p5_cred test_p5_net_unix test_p5_ipc_shm test_p5_syscall_gate

PAYLOAD_ELF := $(BUILD_DIR)/payload.elf

FS_OBJS := $(BUILD_DIR)/fs_ofd.o $(BUILD_DIR)/blk_vol.o $(BUILD_DIR)/blk_persist.o \
	$(BUILD_DIR)/devnode.o $(BUILD_DIR)/mount.o
PROC_OBJS := $(BUILD_DIR)/process.o $(BUILD_DIR)/vmm.o $(BUILD_DIR)/shell_cli.o \
	$(BUILD_DIR)/guest_io.o $(BUILD_DIR)/elf_load.o $(BUILD_DIR)/thread.o $(BUILD_DIR)/tty.o
M5_OBJS := $(BUILD_DIR)/cred.o $(BUILD_DIR)/net_unix.o $(BUILD_DIR)/ipc_shm.o \
	$(BUILD_DIR)/syscall_dispatch.o
COMMON_OBJS := $(FS_OBJS) $(PROC_OBJS) $(M5_OBJS)

.PHONY: all host-tests clean

all: host-tests

host-tests: $(PAYLOAD_ELF) $(addprefix $(BUILD_DIR)/,$(HOST_TESTS))

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(PAYLOAD_ELF): tools/elf_payload.c tools/elf_payload.ld | $(BUILD_DIR)
	$(CC) $(CFLAGS) -nostdlib -static -fno-pie -fno-stack-protector \
		-Wl,-T,tools/elf_payload.ld -Wl,-e,elf_main -o $@ tools/elf_payload.c

$(BUILD_DIR)/fs_ofd.o: kernel/sysmain/fs_ofd.c kernel/sysmain/fs_ofd.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/blk_vol.o: kernel/sysmain/blk_vol.c kernel/sysmain/blk_vol.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/blk_persist.o: kernel/sysmain/blk_persist.c kernel/sysmain/blk_persist.h kernel/sysmain/fs_ofd.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/devnode.o: kernel/sysmain/devnode.c kernel/sysmain/devnode.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/mount.o: kernel/sysmain/mount.c kernel/sysmain/mount.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/cred.o: kernel/sysmain/cred.c kernel/sysmain/cred.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/net_unix.o: kernel/sysmain/net_unix.c kernel/sysmain/net_unix.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/ipc_shm.o: kernel/sysmain/ipc_shm.c kernel/sysmain/ipc_shm.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/syscall_dispatch.o: kernel/sysmain/syscall_dispatch.c kernel/sysmain/syscall_dispatch.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/process.o: kernel/sysmain/process.c kernel/sysmain/process.h kernel/sysmain/vmm.h kernel/sysmain/elf_load.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/vmm.o: kernel/sysmain/vmm.c kernel/sysmain/vmm.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/shell_cli.o: kernel/sysmain/shell_cli.c kernel/sysmain/shell_cli.h kernel/sysmain/process.h kernel/sysmain/vmm.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/guest_io.o: kernel/sysmain/guest_io.c kernel/sysmain/guest_io.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/elf_load.o: kernel/sysmain/elf_load.c kernel/sysmain/elf_load.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/thread.o: kernel/sysmain/thread.c kernel/sysmain/thread.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/tty.o: kernel/sysmain/tty.c kernel/sysmain/tty.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/%.o: tools/%.c kernel/sysmain/fs_ofd.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/test_p4_vfork_exec.o: tools/test_p4_vfork_exec.c kernel/sysmain/process.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/test_p4_waitid.o: tools/test_p4_waitid.c kernel/sysmain/process.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/test_p4_fork.o: tools/test_p4_fork.c kernel/sysmain/process.h kernel/sysmain/vmm.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/test_p4_pipe_signal.o: tools/test_p4_pipe_signal.c kernel/sysmain/process.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/test_p4_shell_cli.o: tools/test_p4_shell_cli.c kernel/sysmain/process.h kernel/sysmain/shell_cli.h kernel/sysmain/vmm.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/test_p4_rw_dup2.o: tools/test_p4_rw_dup2.c kernel/sysmain/guest_io.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/test_p4_brk_mmap.o: tools/test_p4_brk_mmap.c kernel/sysmain/vmm.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/test_p4_elf_load.o: tools/test_p4_elf_load.c kernel/sysmain/elf_load.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -DPAYLOAD_ELF_PATH=\"$(PAYLOAD_ELF)\" -c $< -o $@

$(BUILD_DIR)/test_p4_clone_futex.o: tools/test_p4_clone_futex.c kernel/sysmain/thread.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/test_p4_tty_job.o: tools/test_p4_tty_job.c kernel/sysmain/tty.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/test_p5_devnode.o: tools/test_p5_devnode.c kernel/sysmain/fs_ofd.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/test_p5_mount.o: tools/test_p5_mount.c kernel/sysmain/mount.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/test_p5_cred.o: tools/test_p5_cred.c kernel/sysmain/cred.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/test_p5_net_unix.o: tools/test_p5_net_unix.c kernel/sysmain/net_unix.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/test_p5_ipc_shm.o: tools/test_p5_ipc_shm.c kernel/sysmain/ipc_shm.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/test_p5_syscall_gate.o: tools/test_p5_syscall_gate.c kernel/sysmain/syscall_dispatch.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/%: $(BUILD_DIR)/%.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)
