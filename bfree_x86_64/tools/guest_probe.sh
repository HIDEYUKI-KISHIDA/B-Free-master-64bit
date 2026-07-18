#!/usr/bin/env bash
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
qlog=/tmp/bfree-probe-qemu.log
: > "$qlog"
pkill -9 -f 'qemu-system-x86_64.*bfree' 2>/dev/null || true
sleep 1
(
  sleep 88
  printf 'id && echo ID_OK\n'
  sleep 2
  printf 'echo B2TARDAT""A > /tmp/b2f\n'
  sleep 2
  printf 'tar -cf /tmp/b2.tar -C /tmp b2f; echo CRC=$?\n'
  sleep 3
  printf 'cd /\n'
  sleep 2
  printf 'wc -c /tmp/b2.tar\n'
  sleep 2
  printf 'tar -tf /tmp/b2.tar; echo TRC=$?\n'
  sleep 3
  printf 'cd /\n'
  sleep 2
  printf 'rm /tmp/b2f\n'
  sleep 2
  printf 'tar -xf /tmp/b2.tar -C /tmp; echo XRC=$?\n'
  sleep 3
  printf 'cd /\n'
  sleep 2
  printf 'cat /tmp/b2f; echo\n'
  sleep 2
  printf 'echo P4FD > /tmp/p4fd\n'
  sleep 2
  printf 'exec 3</tmp/p4fd; exec 4</tmp/p4fd\n'
  sleep 2
  printf 'read P4A <&3; read P4B <&4; echo P4=[$P4A:$P4B]\n'
  sleep 2
  printf 'exec 3<&-; exec 4<&-; rm /tmp/p4fd\n'
  sleep 2
  printf 'echo P_END\n'
  sleep 2
) | timeout 180 qemu-system-x86_64 -m 512M -no-reboot -cdrom "$ROOT/bfree.iso" \
    -display none -serial mon:stdio >>"$qlog" 2>&1 || true
sed -n '/B2TARDAT/,$p' "$qlog" | tail -40
