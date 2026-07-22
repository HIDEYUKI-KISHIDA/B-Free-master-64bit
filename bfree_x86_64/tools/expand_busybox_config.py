#!/usr/bin/env python3
"""Expand busybox_m3.config with additional POSIX applets."""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "configs" / "busybox_m3.config"
OUT = ROOT / "configs" / "busybox_posix.config"

# Applets and features safe for static guest + host ash regression.
ENABLE = {
    "CONFIG_LFS",
    "CONFIG_FEATURE_EDITING",
    "CONFIG_FEATURE_EDITING_SAVEHISTORY",
    "CONFIG_BASENAME",
    "CONFIG_DIRNAME",
    "CONFIG_LS",
    "CONFIG_FEATURE_LS_FILETYPES",
    "CONFIG_FEATURE_LS_SORTFILES",
    "CONFIG_FEATURE_LS_TIMESTAMPS",
    "CONFIG_MKDIR",
    "CONFIG_RM",
    "CONFIG_RMDIR",
    "CONFIG_CP",
    "CONFIG_MV",
    "CONFIG_LN",
    "CONFIG_PWD",
    "CONFIG_WC",
    "CONFIG_HEAD",
    "CONFIG_TAIL",
    "CONFIG_SORT",
    "CONFIG_UNIQ",
    "CONFIG_CUT",
    "CONFIG_TR",
    "CONFIG_TEE",
    "CONFIG_READLINK",
    "CONFIG_FEATURE_READLINK_FOLLOW",
    "CONFIG_TOUCH",
    "CONFIG_FEATURE_TOUCH_SUSV3",
    "CONFIG_CHMOD",
    "CONFIG_CHOWN",
    "CONFIG_ENV",
    "CONFIG_WHICH",
    "CONFIG_KILL",
    "CONFIG_GREP",
    "CONFIG_EGREP",
    "CONFIG_FGREP",
    "CONFIG_FEATURE_GREP_CONTEXT",
    "CONFIG_SED",
    "CONFIG_AWK",
    "CONFIG_FEATURE_AWK_LIBM",
    "CONFIG_FIND",
    "CONFIG_FEATURE_FIND_PRINT0",
    "CONFIG_FEATURE_FIND_MTIME",
    "CONFIG_FEATURE_FIND_TYPE",
    "CONFIG_FEATURE_FIND_PERM",
    "CONFIG_FEATURE_FIND_EXEC",
    "CONFIG_XARGS",
    "CONFIG_MD5SUM",
    "CONFIG_GZIP",
    "CONFIG_GUNZIP",
    "CONFIG_ZCAT",
    "CONFIG_TAR",
    "CONFIG_FEATURE_TAR_CREATE",
    "CONFIG_FEATURE_TAR_AUTODETECT",
    "CONFIG_PS",
    "CONFIG_FEATURE_PS_WIDE",
    "CONFIG_MKTEMP",
    "CONFIG_SEQ",
    "CONFIG_YES",
    "CONFIG_NOHUP",
    "CONFIG_STAT",
    "CONFIG_FEATURE_STAT_FORMAT",
    "CONFIG_DU",
    "CONFIG_DF",
    "CONFIG_MORE",
    "CONFIG_LESS",
    "CONFIG_FEATURE_LESS_MAXLINES",
    "CONFIG_FEATURE_LESS_FLAGS",
    "CONFIG_ECHO",
    "CONFIG_FEATURE_FANCY_ECHO",
    "CONFIG_TEST",
    "CONFIG_FEATURE_TEST_64",
}

VALUE_OVERRIDES = {
    "CONFIG_FEATURE_EDITING_MAX_LEN": "1024",
    "CONFIG_FEATURE_EDITING_HISTORY": "256",
    "CONFIG_FEATURE_LESS_MAXLINES": "9999999",
    "CONFIG_GZIP_FAST": "2",
}


def main() -> int:
    if not SRC.is_file():
        print(f"ERROR: missing {SRC}", file=sys.stderr)
        return 1

    lines = SRC.read_text().splitlines()
    out: list[str] = []
    enabled_count = 0

    for line in lines:
        m_off = re.match(r"^# (CONFIG_[A-Z0-9_]+) is not set$", line)
        if m_off and m_off.group(1) in ENABLE:
            out.append(f"{m_off.group(1)}=y")
            enabled_count += 1
            continue

        m_val = re.match(r"^(CONFIG_[A-Z0-9_]+)=(.*)$", line)
        if m_val and m_val.group(1) in VALUE_OVERRIDES:
            out.append(f"{m_val.group(1)}={VALUE_OVERRIDES[m_val.group(1)]}")
            continue

        out.append(line)

    header = [
        "# Expanded guest BusyBox config (generated from busybox_m3.config)",
        f"# Enabled {enabled_count} additional options",
        "",
    ]
    OUT.write_text("\n".join(header + out) + "\n")
    print(f"OK: {OUT} (+{enabled_count} options)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
