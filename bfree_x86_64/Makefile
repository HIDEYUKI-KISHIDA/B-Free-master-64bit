# Host-side unit tests for bfree_x86_64 guest FS/OFD layer.

CC ?= gcc
CFLAGS ?= -Wall -Wextra -std=c11 -O2 -g
BUILD_DIR := build/host-tests

HOST_TEST_SRCS := tools/test_p4_dirent_ofd.c
HOST_TEST_OBJS := $(addprefix $(BUILD_DIR)/,$(notdir $(HOST_TEST_SRCS:.c=.o)))
FS_OFD_OBJ := $(BUILD_DIR)/fs_ofd.o

.PHONY: all host-tests clean

all: host-tests

host-tests: $(BUILD_DIR)/test_p4_dirent_ofd

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/fs_ofd.o: kernel/sysmain/fs_ofd.c kernel/sysmain/fs_ofd.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/test_p4_dirent_ofd.o: tools/test_p4_dirent_ofd.c kernel/sysmain/fs_ofd.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Ikernel/sysmain -c $< -o $@

$(BUILD_DIR)/test_p4_dirent_ofd: $(BUILD_DIR)/test_p4_dirent_ofd.o $(FS_OFD_OBJ)
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -rf $(BUILD_DIR)
