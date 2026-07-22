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

## Continue M12 (on branch `cursor/m12-busybox-guest-695c`)

```bash
git fetch origin
git checkout cursor/m12-busybox-guest-695c
cd bfree_x86_64
bash tools/setup_home_dev.sh
./tools/phase3_guest_auto.sh
./tools/qemu_musl_guest_smoke.sh
./tools/qemu_busybox_guest_smoke.sh
```

See [docs/M12_BUSYBOX_GUEST.md](M12_BUSYBOX_GUEST.md).

## Cursor prompt

```
Branch cursor/m12-busybox-guest-695c — M12 BusyBox-on-guest is implemented.
Run setup_home_dev.sh, phase3_guest_auto.sh, qemu_busybox_guest_smoke.sh.
Continue M13 guest LTP/POSIX per docs/M12_BUSYBOX_GUEST.md.
Arch: x86_64 (bfree_x86_64).
```

## Notes

- Chat history does not sync; this file + `docs/M9_USER_BOOT.md` are the handoff.
- `main` may still be behind; use the branch above.
