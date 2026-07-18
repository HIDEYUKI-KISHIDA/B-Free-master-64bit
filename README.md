# B-Free-master-64bit

64-bit open-source OS inheriting BTRON philosophy, with a Linux/POSIX guest compatibility track.

## POSIX compatibility roadmap

See [docs/POSIX_FULL_COMPAT_ROADMAP.ja.md](docs/POSIX_FULL_COMPAT_ROADMAP.ja.md) for milestone tracking (M0–M5).

### Run verification (host tests)

```bash
cd bfree_x86_64
./tools/phase3_guest_auto.sh
```

Expected output: `RESULT: ALL PASS`

### Block volume (M1 persistent FS)

```bash
# format + mount + sync API available via bfree_blk_format / bfree_fs_mount / bfree_fs_sync
cd bfree_x86_64 && ./tools/phase3_guest_auto.sh  # includes P4_BLOCK_FS
```
