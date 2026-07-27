# M8 — Real boot path (paging, trap HW, initramfs)

M8 moves from asm-only QEMU smoke to a freestanding C kernel with identity paging, hardware trap install, and embedded initramfs.

## Scope

| Component | Path | Marker |
|-----------|------|--------|
| Identity paging | `paging.c` | `P8_PAGING` |
| newc cpio parser | `initramfs.c` | `P8_INITRAMFS` |
| lidt / wrmsr install | `trap_hw.c` | `P8_TRAP_HW` |
| Boot path harness | host + `kernel_boot.c` | `P8_KERNEL_BOOT` |
| Freestanding kernel | `build/kernel.elf` | `P8_KERNEL_BOOT` (QEMU) |

## Gates

```bash
cd bfree_x86_64
./tools/phase3_guest_auto.sh
./tools/qemu_kernel_smoke.sh   # SKIP without QEMU; expects KERNEL_OK + INITRAMFS_OK
```

## Residual (M9+)

- QEMU `-initrd` load (external initramfs vs embed-only today)
- GDT + ring-3 user launch for musl raw `syscall` insn
- Full `syscall`/`sysret` entry stub (distinct from call-based trap entry)
- musl static under in-kernel trap in QEMU
- Full upstream LTP on booted guest
