#!/usr/bin/env bash
# Guest nostdinc++: qsharedmemory.cpp uses PATH_MAX without including limits.h.
if grep -q $'\r' "$0" 2>/dev/null; then
  exec env BFREE_FIX_CRLF_DONE=1 bash -c "$(tr -d '\r' <"$0")" bash "$@"
fi
set -euo pipefail
QT_SRC="${BFREE_QT_SRC:-$HOME/src/qt6}"
f="$QT_SRC/qtbase/src/corelib/ipc/qsharedmemory.cpp"
marker="$QT_SRC/qtbase/.bfree_guest_qsharedmemory_path_max_patched"

[[ -f "$f" ]] || { echo "[patch] missing: $f" >&2; exit 1; }

if [[ -f "$marker" ]] && grep -q '#include <limits.h>' "$f"; then
  echo "[patch] ok: qsharedmemory PATH_MAX already patched"
  exit 0
fi

old_block=$'#include <errno.h>\n\n#ifndef MAX_PATH\n# define MAX_PATH PATH_MAX\n#endif\n'
new_block=$'#include <errno.h>\n#include <limits.h>\n\n#ifndef MAX_PATH\n# ifdef PATH_MAX\n#  define MAX_PATH PATH_MAX\n# else\n#  define MAX_PATH 1024\n# endif\n#endif\n'

if grep -qF '# define MAX_PATH PATH_MAX' "$f" && ! grep -q '#include <limits.h>' "$f"; then
  perl -0pi -e '
    s/#include <errno.h>\n\n#ifndef MAX_PATH\n# define MAX_PATH PATH_MAX\n#endif\n/
#include <errno.h>\n#include <limits.h>\n\n#ifndef MAX_PATH\n# ifdef PATH_MAX\n#  define MAX_PATH PATH_MAX\n# else\n#  define MAX_PATH 1024\n# endif\n#endif\n/s;
  ' "$f"
elif ! grep -q '#include <limits.h>' "$f"; then
  perl -pi -e 's/(#include <errno.h>)/$1\n#include <limits.h>/' "$f"
fi

touch "$marker"
echo "[patch] ok: qsharedmemory PATH_MAX (limits.h + safe fallback)"
