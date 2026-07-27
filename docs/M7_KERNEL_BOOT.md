# M7 — Kernel boot / in-kernel trap

M7 moves from host invoke stubs to kernel entry, trap table setup, and trap-based static payload execution.

## Scope

| Component | Path | Marker |
|-----------|------|--------|
| Trap setup (IDT / MSR state) | `trap_setup.c` | `P7_TRAP_SETUP` |
| Kernel main | `kernel_main.c` | `P7_KERNEL_MAIN` |
| Trap static payload | `trap_payload.S` | `P7_TRAP_PAYLOAD` |
| musl via trap-init exec | `elf_trap_exec.c` | `P7_MUSL_TRAP` |
| QEMU kernel boot | `boot_kernel.S` + `kernel.elf` | `P7_KERNEL_BOOT` |
| LTP-style gate | `ltp_regress/` | `LTP_REGRESS` |

## Gates

```bash
cd bfree_x86_64
./tools/phase3_guest_auto.sh
./tools/phase3_guest_ltp.sh
./tools/qemu_kernel_smoke.sh   # SKIP without QEMU
```

## Residual (M9+)

- QEMU `-initrd` external load
- GDT + ring-3 user launch for musl raw `syscall`
- Full upstream LTP on booted guest
