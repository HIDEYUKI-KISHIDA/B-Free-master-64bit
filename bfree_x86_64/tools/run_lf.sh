#!/usr/bin/env bash
# Run a script from /mnt/c with CRLF stripped (bash parses the file before in-script fixes).
# Usage: bash tools/run_lf.sh tools/build_x86_64_elf_libstdcxx.sh
set -e
script="${1:?usage: bash tools/run_lf.sh path/to/script.sh [args...]}"
shift
exec bash -c "$(tr -d '\r' <"$script")" bash "$@"
