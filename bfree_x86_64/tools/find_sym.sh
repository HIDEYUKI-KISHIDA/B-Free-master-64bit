#!/usr/bin/env bash
# Usage: find_sym.sh <elf> <hex-addr>
set -eu
elf="$1"
addr="$2"
nm "$elf" | sort | awk -v tgt="$((16#${addr#0x}))" '
{
  a = strtonum("0x" $1)
  if (a <= tgt) { last = $0 }
  else { print last; print $0; exit }
}'
