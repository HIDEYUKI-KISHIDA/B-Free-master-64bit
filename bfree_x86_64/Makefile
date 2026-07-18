# Host-side unit tests for bfree_x86_64 guest FS/OFD layer.

CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c11 -O2 -g
BUILD_DIR := build/host-tests

HOST_TESTS := test_p4_dirent_ofd test_p4_unlink_open test_p4_openat test_p4_block_fs \
	test_p4_vfork_exec test_p4_waitid test_p4_fork test_p4_pipe_signal

FS_OBJS := $(BUILD_DIR)/fs_ofd.o $(BUILD_DIR)/blk_vol.o $(BUILD_DIR)/blk_persist.o
PROC_OBJS := $(BUILD_DIR)/process.o $(BUILD_DIR)/vmm.o
COMMON_OBJS := $(FS_OBJS) $(PROC_OBJS)

.PHONY: all host-tests clean

all: host-tests

host-tests: $(addprefix $(BUILD_DIR)/,$(HOST_TESTS))

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/fs_ofd.o: kernel/sysmain/fs_ofd.c kernel/sysmain/fs_ofd.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/blk_vol.o: kernel/sysmain/blk_vol.c kernel/sysmain/blk_vol.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/blk_persist.o: kernel/sysmain/blk_persist.c kernel/sysmain/blk_persist.h kernel/sysmain/fs_ofd.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/process.o: kernel/sysmain/process.c kernel/sysmain/process.h kernel/sysmain/vmm.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/vmm.o: kernel/sysmain/vmm.c kernel/sysmain/vmm.h | $(BUILD_DIR)
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

$(BUILD_DIR)/%: $(BUILD_DIR)/%.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -rf $(BUILD_DIR)
