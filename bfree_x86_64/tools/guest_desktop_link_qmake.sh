#!/usr/bin/env bash
# QMAKE_LINK entry: single path (no spaces) so qmake does not strip quotes from bash -c.
# IMPORTANT: link argv must be passed to "bash -s --", NOT to "bash -c" (see guest_desktop_link bug).
d=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
export BFREE_ROOT="${BFREE_ROOT:-$(cd "$d/.." && pwd)}"
clean=()
for a in "$@"; do
  clean+=("${a//$'\r'/}")
done
exec bash -s -- "${clean[@]}" < <(tr -d '\r' <"$d/guest_desktop_link.sh")
