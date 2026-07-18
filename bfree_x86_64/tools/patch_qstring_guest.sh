#!/usr/bin/env bash
# Revert guest QString patch — guest_link_compat memcpy guard handles bad src.
set -eu
python3 - <<'PY'
from pathlib import Path

path = Path("/root/src/qt6/qtbase/src/corelib/text/qstring.cpp")
text = path.read_text()

raw = """        if (dd.size > 0)
            ::memcpy(dd.data(), d.data(), dd.size * sizeof(QChar));"""

patches = [
"""        if (dd.size > 0) {
            const void *oldData = d.data();
            const auto oldAddr = reinterpret_cast<quintptr>(oldData);
            if (qEnvironmentVariableIsSet("BFREE_GUEST_FIXED_LOCALE")
                && (!oldData || oldAddr < 0x10000u
                    || oldAddr == Q_UINT64_C(0x0101010101010101))) {
                dd.size = 0;
            } else {
                ::memcpy(dd.data(), oldData, dd.size * sizeof(QChar));
            }
        }""",
"""        if (dd.size > 0) {
            const void *oldData = d.data();
            const auto oldAddr = reinterpret_cast<quintptr>(oldData);
            if (!qEnvironmentVariableIsSet("BFREE_GUEST_FIXED_LOCALE")
                || (oldData && oldAddr >= 0x10000u
                    && oldAddr != Q_UINT64_C(0x0101010101010101)))
                ::memcpy(dd.data(), oldData, dd.size * sizeof(QChar));
        }""",
]

if raw in text:
    print("[patch_qstring_guest] already reverted")
else:
    for p in patches:
        if p in text:
            text = text.replace(p, raw, 1)
            path.write_text(text)
            print("[patch_qstring_guest] reverted to stock memcpy")
            break
    else:
        raise SystemExit("qstring.cpp: guest patch anchor missing")
PY
