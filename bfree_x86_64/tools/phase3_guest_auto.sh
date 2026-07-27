#!/usr/bin/env bash
# Guest regression gate (Phase 3 base + per-milestone markers).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT}/build/host-tests"
FAIL=0

run_test() {
  local name="$1"
  echo "== phase3_guest_auto: ${name} =="
  if ! "${BUILD_DIR}/${name}"; then
    FAIL=1
  fi
}

mkdir -p "${BUILD_DIR}"

echo "== phase3_guest_auto: build host tests =="
make -C "${ROOT}" -s host-tests boot-smoke initramfs kernel boot-trap-payload

# M1 filesystem
run_test test_p4_dirent_ofd
run_test test_p4_unlink_open
run_test test_p4_openat
run_test test_p4_block_fs

# M2 process
run_test test_p4_vfork_exec
run_test test_p4_waitid
run_test test_p4_fork
run_test test_p4_pipe_signal

# M3 normal CLI
run_test test_p4_shell_cli

# M4 musl / static binary base
run_test test_p4_rw_dup2
run_test test_p4_brk_mmap
run_test test_p4_elf_load
run_test test_p4_clone_futex
run_test test_p4_tty_job
run_test test_p4_musl_load
run_test test_p4_musl_exec

# M5 POSIX surface
run_test test_p5_devnode
run_test test_p5_mount
run_test test_p5_cred
run_test test_p5_net_unix
run_test test_p5_ipc_shm
run_test test_p5_syscall_gate

# M6 boot / trap
run_test test_p6_syscall_invoke
run_test test_p6_trap_entry

echo "== phase3_guest_auto: QEMU boot smoke =="
if ! "${ROOT}/tools/qemu_boot_smoke.sh"; then
  FAIL=1
fi

# M7 kernel boot / trap
run_test test_p7_kernel_main
run_test test_p7_trap_setup
run_test test_p7_trap_payload
run_test test_p7_musl_trap

# M8 real boot path
run_test test_p8_paging
run_test test_p8_initramfs
run_test test_p8_trap_hw
run_test test_p8_kernel_boot

# M9 user boot (handoff — QEMU USER_BOOT_OK is next goal)
run_test test_p9_gdt
run_test test_p9_user_boot

# M10 syscall kernel integration
run_test test_p10_dispatch_wiring
run_test test_p10_kernel_guest

# M11 musl on booted guest
run_test test_p11_musl_guest

# M12 BusyBox ash on booted guest
run_test test_p12_busybox_guest

# M13 guest LTP/POSIX probes on booted QEMU
run_test test_p13_guest_regress

# M14 stat/poll/fstatat syscalls + guest probes
run_test test_p14_stat_poll
run_test test_p14_guest_regress

# M15 blocking poll + ash regress on booted QEMU
run_test test_p15_poll_blocking
run_test test_p15_ash_regress_staged

# M16 ring-3 fork + full ash regress on booted QEMU
run_test test_p16_pipe_dup2
run_test test_p16_ash_full_staged

# M17 Linux ABI holes categories 0-4
run_test test_p17_abi_holes

# M18 second-map batch (BusyBox 9 ENOSYS + 15 THIN)
run_test test_p18_second_map_24

echo "== phase3_guest_auto: ABI second map (BusyBox evidence) =="
if ! python3 "${ROOT}/tools/gen_abi_second_map.py"; then
  FAIL=1
fi

echo "== phase3_guest_auto: QEMU user boot smoke =="
if ! "${ROOT}/tools/qemu_user_boot_smoke.sh"; then
  FAIL=1
fi

echo "== phase3_guest_auto: QEMU musl guest smoke =="
if ! "${ROOT}/tools/qemu_musl_guest_smoke.sh"; then
  FAIL=1
fi

echo "== phase3_guest_auto: QEMU busybox guest smoke =="
if ! "${ROOT}/tools/qemu_busybox_guest_smoke.sh"; then
  FAIL=1
fi

echo "== phase3_guest_auto: QEMU ltp open guest smoke =="
if ! "${ROOT}/tools/qemu_ltp_open_guest_smoke.sh"; then
  FAIL=1
fi

echo "== phase3_guest_auto: QEMU posix io guest smoke =="
if ! "${ROOT}/tools/qemu_posix_io_guest_smoke.sh"; then
  FAIL=1
fi

echo "== phase3_guest_auto: QEMU stat guest smoke =="
if ! "${ROOT}/tools/qemu_stat_guest_smoke.sh"; then
  FAIL=1
fi

echo "== phase3_guest_auto: QEMU poll guest smoke =="
if ! "${ROOT}/tools/qemu_poll_guest_smoke.sh"; then
  FAIL=1
fi

echo "== phase3_guest_auto: QEMU ash regress guest smoke =="
if ! "${ROOT}/tools/qemu_ash_regress_guest_smoke.sh"; then
  FAIL=1
fi

echo "== phase3_guest_auto: QEMU kernel boot =="
if ! "${ROOT}/tools/qemu_kernel_smoke.sh"; then
  FAIL=1
fi

echo "== phase3_guest_auto: LTP regress gate =="
if ! "${ROOT}/tools/phase3_guest_ltp.sh"; then
  FAIL=1
fi

echo "== phase3_guest_auto: POSIX regress gate =="
if ! "${ROOT}/tools/phase3_guest_posix_regress.sh"; then
  FAIL=1
fi

echo "== phase3_guest_auto: build guest BusyBox rootfs =="
"${ROOT}/tools/build_guest_busybox.sh"

echo "== phase3_guest_auto: guest ash regression =="
if ! "${ROOT}/tools/phase3_guest_ash.sh"; then
  FAIL=1
fi

if [[ "${FAIL}" -ne 0 ]]; then
  echo "RESULT: FAIL"
  exit 1
fi

echo "RESULT: ALL PASS"
