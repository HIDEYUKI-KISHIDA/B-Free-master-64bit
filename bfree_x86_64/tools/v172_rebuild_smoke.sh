#!/usr/bin/env bash
set -eu
pkill -f 'qemu-system-x86_64.*bfree.iso' 2>/dev/null || true
exec bash tools/guest_qml_fast_iterate.sh all
