#!/usr/bin/env bash
# Minimal desktop PHDR/TLS fix for WSL vfork __copy_tls GP (no full compat recompile).
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DESK="$ROOT/userland/desktop_qt"
cd "$DESK"

[[ -s desktop.elf ]] || { echo "FAIL: need existing desktop.elf" >&2; exit 1; }

echo "[phdrs-relink] sync guest_link_compat.cpp phdrs from desktop.elf (for future full relinks)"
python3 "$ROOT/tools/emit_guest_compat_phdrs.py" "$DESK/desktop.elf" "$ROOT/tools/guest_link_compat.cpp"

echo "[phdrs-relink] patch embedded bfree_guest_phdrs[] in desktop.elf (in-place)"
python3 "$ROOT/tools/patch_desktop_phdrs_embedded.py" "$DESK/desktop.elf"

bash "$ROOT/tools/update_guest_resource_holder_va.sh" desktop.elf "$DESK/guest_resource_holder_va.h"
python3 "$ROOT/tools/check_desktop_phdrs_embedded.py" desktop.elf
bash "$ROOT/tools/check_desktop_holder_embedded.sh" desktop.elf
sha256sum desktop.elf
echo "Next: BFREE_D3=1 bash tools/_d3_compositor_stub_smoke.sh"
