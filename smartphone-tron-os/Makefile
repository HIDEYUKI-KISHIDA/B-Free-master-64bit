.PHONY: check qemu
check:
	python3 tools/check_bsp.py
	python3 tests/test_bsp_header.py
	$(MAKE) -C kernel BOARD=qemu_virt all
	$(MAKE) -C kernel BOARD=lena all
	tools/run_qemu_bringup.sh

qemu:
	$(MAKE) -C kernel BOARD=qemu_virt qemu
