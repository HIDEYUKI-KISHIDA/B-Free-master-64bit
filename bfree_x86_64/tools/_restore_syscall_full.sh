#!/usr/bin/env bash
set -eu
ROOT=/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64
cd "$ROOT"

# Strip CR from patch/replay helpers (in place)
for s in tools/_patch_*.py tools/_replay_syscall_transcripts.py tools/_unix_coop_block.c; do
  [ -f "$s" ] || continue
  tr -d '\r' < "$s" > "$s.nocr" && mv "$s.nocr" "$s"
done

echo "[1] baseline size"
wc -c kernel/sysmain/syscall.c

echo "[2] apply layered patch scripts (in-tree paths)"
for s in \
  tools/_patch_stage1_b.py \
  tools/_patch_stage1_ash.py \
  tools/_patch_unix_coop.py \
  tools/_patch_pty_stage2.py \
  tools/_patch_posix_phase_a.py \
  tools/_patch_posix_phase_a2.py \
  tools/_patch_sf_todos.py \
  tools/_patch_watch2.py
do
  echo "=== $s ==="
  python3 "$s" || echo "WARN: $s exited $?"
done

echo "[3] size after patches"
wc -c kernel/sysmain/syscall.c
for s in BFREE_UNIX_FD_BASE g_guest_sig_pending /dev/ptmx sys_linux_socket; do
  printf "%5d  %s\n" "$(grep -cF -- "$s" kernel/sysmain/syscall.c || true)" "$s"
done

echo "[4] transcript replay"
python3 tools/_replay_syscall_transcripts.py 2>&1 | tee /tmp/replay_out.txt | tail -60

echo "[5] marker check"
for s in g_guest_fork_was_as_copy bfree_inet_from_fd g_bfree_exec_transfer_rip g_unix_socks g_inet_socks /dev/ptmx bfree_coop_as_switch_to; do
  printf "%5d  %s\n" "$(grep -cF -- "$s" kernel/sysmain/syscall.c || true)" "$s"
done
wc -c kernel/sysmain/syscall.c
