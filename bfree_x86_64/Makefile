# Host-side unit tests for bfree_x86_64 guest FS/OFD layer.

CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c11 -O2 -g
BUILD_DIR := build/host-tests

HOST_TESTS := test_p4_dirent_ofd test_p4_unlink_open test_p4_openat
FS_OFD_OBJ := $(BUILD_DIR)/fs_ofd.o

.PHONY: all host-tests clean

all: host-tests

host-tests: $(addprefix $(BUILD_DIR)/,$(HOST_TESTS))

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/fs_ofd.o: kernel/sysmain/fs_ofd.c kernel/sysmain/fs_ofd.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/%.o: tools/%.c kernel/sysmain/fs_ofd.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/%: $(BUILD_DIR)/%.o $(FS_OFD_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -rf $(BUILD_DIR)
