# Continue M9 on your home PC

This repo’s POSIX work lives on **feature branches**, not `main`.

## 1. Get the code

```bash
git fetch origin
git checkout cursor/m9-user-boot-695c
cd bfree_x86_64
```

## 2. Verify environment

```bash
bash tools/setup_home_dev.sh
```

Needs: `gcc`, `make`, `python3`, `git`. Optional but recommended: `qemu-system-x86_64`.

## 3. Open in Cursor

Open the repository root in Cursor. Start a new Agent chat with:

```
Continue M9 user boot on cursor/m9-user-boot-695c.
Read docs/M9_USER_BOOT.md and implement USER_BOOT_OK in QEMU.
Arch is x86_64 (bfree_x86_64), not aarch64.
```

## 4. What M9 means

- **Done (M0–M8):** host-tested POSIX stubs, trap, paging, initramfs embed
- **M9 goal:** ring-3 userspace under `kernel.elf` with in-kernel `syscall` path
- **Not synced:** cloud agent chat history — only git branches and `docs/M9_USER_BOOT.md`

## Branches

| Branch | Milestone |
|--------|-----------|
| `cursor/m8-real-boot-695c` | M8 paging + trap HW |
| `cursor/m9-user-boot-695c` | **M9 handoff (start here)** |

Draft PRs #3–#10 cover M1–M8; open a new PR from your M9 branch when ready.
