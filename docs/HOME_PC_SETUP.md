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

## Continue M13 (on branch `cursor/m13-guest-ltp-posix-695c`)

```bash
git fetch origin
git checkout cursor/m13-guest-ltp-posix-695c
cd bfree_x86_64
bash tools/setup_home_dev.sh
./tools/phase3_guest_auto.sh
./tools/qemu_ltp_open_guest_smoke.sh
./tools/qemu_posix_io_guest_smoke.sh
```

See [docs/M13_GUEST_LTP_POSIX.md](M13_GUEST_LTP_POSIX.md).

## Cursor prompt

```
Branch cursor/m13-guest-ltp-posix-695c — M13 guest LTP/POSIX probes are implemented.
Run setup_home_dev.sh, phase3_guest_auto.sh, qemu_ltp_open_guest_smoke.sh,
qemu_posix_io_guest_smoke.sh.
Continue M14 stat/poll/fstatat per docs/M13_GUEST_LTP_POSIX.md.
Arch: x86_64 (bfree_x86_64).
```

## Notes

- Chat history does not sync; this file + `docs/M9_USER_BOOT.md` are the handoff.
- `main` may still be behind; use the branch above.
