#!/usr/bin/env python3
"""Rewrite qmlcache helper shell scripts with Unix LF (fixes WSL CRLF breakage)."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
scripts = [
    ROOT / "tools/write_build_host_qmlcachegen_sh.py",
    ROOT / "tools/write_gen_guest_mvp_qmlcache_sh.py",
]

for script in scripts:
    print(f"[write_qmlcache_shell_scripts] running {script.name}")
    subprocess.run([sys.executable, str(script)], check=True)

print("[write_qmlcache_shell_scripts] done")
