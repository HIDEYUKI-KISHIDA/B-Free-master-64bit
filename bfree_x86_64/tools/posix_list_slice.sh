#!/usr/bin/env bash
# Usage: posix_list_slice.sh START END  — print filtered posix2017_functions_only.txt lines START..END
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LIST="$ROOT/posix2017_functions_only.txt"
START="${1:?}"
END="${2:?}"
awk '
    /^[[:space:]]*#/ { next }
    {
        gsub(/\r/, "", $0)
        gsub(/^[[:space:]]+|[[:space:]]+$/, "", $0)
        if ($0 ~ /^[A-Za-z_][A-Za-z0-9_]*$/) {
            if ($0 == "Requirements" || $0 == "Threads" || $0 == "Flags") next
            print $0
        }
    }
' "$LIST" | sed -n "${START},${END}p"
