# M6 — Boot / syscall trap

M6 bridges the M1–M5 `sys_*()` handlers to a trap entry path suitable for future in-kernel syscall dispatch.

## Scope (host-testable)

| Component | Path | Marker |
|-----------|------|--------|
| Runtime invoke | `syscall.c` → `bfree_invoke_syscall()` | `P6_SYSCALL_INVOKE` |
| Trap entry stub | `kernel/arch/x86_64/syscall_entry.S` | `P6_TRAP_ENTRY` |
| QEMU boot smoke | `kernel/arch/x86_64/boot_smoke.S` | `P6_BOOT_SMOKE` |

## Gates

```bash
cd bfree_x86_64
./tools/phase3_guest_auto.sh          # includes P6_* + full M1–M5
./tools/qemu_boot_smoke.sh           # optional if qemu-system-x86_64 present
```

## Next (M7+)

- Load IDT / program `syscall` MSR on real boot path
- Link full `kernel.elf` with paging + initramfs
- Route musl exec through trap (replace `elf_host_run` fork shim)
- Full LTP subset on booted guest
