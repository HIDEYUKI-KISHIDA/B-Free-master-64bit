# Continue on your home PC

## Get M9 (complete)

```bash
git clone https://github.com/HIDEYUKI-KISHIDA/B-Free-master-64bit.git
cd B-Free-master-64bit
git fetch origin
git checkout cursor/m9-user-boot-695c
cd bfree_x86_64
bash tools/setup_home_dev.sh
```

## Verify M9

```bash
./tools/phase3_guest_auto.sh          # RESULT: ALL PASS
./tools/qemu_user_boot_smoke.sh       # P9_USER_BOOT_QEMU: PASS (needs QEMU)
```

Install QEMU if missing:

- **Linux:** `sudo apt install qemu-system-x86`
- **macOS:** `brew install qemu`
- **Windows:** QEMU for Windows + WSL optional for build

## Continue M10 (complete on branch `cursor/m10-syscall-kernel-695c`)

```bash
git fetch origin
git checkout cursor/m10-syscall-kernel-695c
cd bfree_x86_64
bash tools/setup_home_dev.sh
./tools/phase3_guest_auto.sh
./tools/qemu_user_boot_smoke.sh
```

See [docs/M10_SYSCALL_KERNEL.md](M10_SYSCALL_KERNEL.md).

## Cursor prompt

```
Branch cursor/m10-syscall-kernel-695c — M10 wires sysmain into kernel.elf.
Run setup_home_dev.sh, phase3_guest_auto.sh, qemu_user_boot_smoke.sh.
Continue M11 musl-on-guest per docs/M10_SYSCALL_KERNEL.md.
Arch: x86_64 (bfree_x86_64).
```

## Notes

- Chat history does not sync; this file + `docs/M9_USER_BOOT.md` are the handoff.
- `main` may still be behind; use the branch above.
