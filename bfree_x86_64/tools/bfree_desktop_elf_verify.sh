#!/usr/bin/env bash
# Shared desktop.elf verification helpers (source only).

bfree_find_elf_nm() {
  command -v x86_64-elf-nm 2>/dev/null || command -v nm 2>/dev/null || true
}

# True when Q_IMPORT_PLUGIN(QPlatformIntegrationPluginBFree) is linked.
# Prefer nm: mangled _Z48qt_static_plugin_* is in the symbol table but often absent from strings(1).
bfree_elf_has_static_qpa_plugin() {
  local elf="$1"
  [[ -f "$elf" ]] || return 1
  local nm_bin nm_out
  nm_bin="$(bfree_find_elf_nm)"
  if [[ -n "$nm_bin" ]]; then
    # Capture nm output: grep -q in a pipe breaks under set -o pipefail (SIGPIPE from nm).
    nm_out="$("$nm_bin" "$elf" 2>/dev/null || true)"
    if [[ "$nm_out" == *qt_static_plugin_QPlatformIntegrationPluginBFree* ]]; then
      return 0
    fi
  fi
  if command -v strings >/dev/null 2>&1; then
    if strings "$elf" 2>/dev/null | grep -Fq 'qt_static_plugin_QPlatformIntegrationPluginBFree'; then
      return 0
    fi
  fi
  return 1
}
