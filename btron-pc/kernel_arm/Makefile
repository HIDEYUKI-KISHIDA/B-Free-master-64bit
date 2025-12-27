# ARMカーネル用Makefile雛形
CROSS_COMPILE ?= arm-none-eabi-
CC := $(CROSS_COMPILE)gcc
AS := $(CROSS_COMPILE)as
LD := $(CROSS_COMPILE)ld
OBJCOPY := $(CROSS_COMPILE)objcopy
CFLAGS := -O2 -nostdlib -ffreestanding -Wall -I../include_arm
LDFLAGS := -T linker_arm.ld

OBJS = boot/start_arm.o arch/cpu_arm.o mm/mm_arm.o drivers/timer_arm.o drivers/gpio_arm.o

all: kernel_arm.img

boot/start_arm.o: boot/start_arm.S
	$(AS) -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

kernel_arm.elf: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

kernel_arm.img: kernel_arm.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -f $(OBJS) kernel_arm.elf kernel_arm.img
