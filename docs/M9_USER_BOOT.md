# M9 — Ring-3 user boot + in-kernel syscall path

M9 boots ring-3 userspace under `kernel.elf` with raw `syscall` instructions handled in-kernel.

## Scope

| Component | Path | Marker |
|-----------|------|--------|
| GDT + `lgdt` | `gdt.c` | `P9_GDT` |
| User payload install | `user_boot.c` | `P9_USER_BOOT` |
| `iretq` to ring 3 | `user_boot_ring3.S` | (QEMU) |
| `syscall`/`sysret` entry | `syscall_insn_entry.S` | (QEMU) |
| User payload (raw `syscall`) | `user_payload.S` | `P9_USER_BOOT_QEMU` |

## Home PC quick start

```bash
git fetch origin
git checkout cursor/m9-user-boot-695c
cd bfree_x86_64
bash tools/setup_home_dev.sh
./tools/qemu_user_boot_smoke.sh   # needs qemu-system-x86_64
```

Expected QEMU output includes: `KERNEL_OK`, `INITRAMFS_OK`, `USER_BOOT_OK`

## Gates

```bash
cd bfree_x86_64
./tools/phase3_guest_auto.sh
./tools/qemu_user_boot_smoke.sh
```

## Residual (M10+)

- musl static via in-kernel trap (`P9_MUSL_KERNEL` / M10)
- Wire full `sysmain` syscall table into freestanding kernel
- `stat`/`poll`/`getdents64` and registry gaps
