#!/bin/sh
# One-time: convert *.sh under bfree_x86_64 to Unix LF (run from WSL on /mnt/c trees).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
find "$ROOT" -name '*.sh' -type f | while read -r f; do
  sed -i 's/\r$//' "$f"
done
echo "CRLF stripped from shell scripts under $ROOT"
