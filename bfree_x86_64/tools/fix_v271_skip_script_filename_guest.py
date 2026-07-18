#!/usr/bin/env python3
"""Guest: skip script.fileName in buildTypeResolutionCaches to avoid QUrl::fileName crash."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

if "typedata_skip_script_filename" in tt:
    print("[v271] script.fileName skip already patched")
    raise SystemExit(0)

# Line-based approach: find the script.fileName line and wrap the entire loop
import re

# Find the script.fileName line
lines = tt.split('\n')
script_filename_line = None
for i, line in enumerate(lines):
    if 'script.fileName' in line and 'getScript' in line:
        script_filename_line = i
        break

if script_filename_line is None:
    print("[v271] script.fileName not in guest tree (Qt 6.8 uses getScript(location) only); skip")
    raise SystemExit(0)

# Find the loop start (for statement)
loop_start = None
for i in range(script_filename_line, max(0, script_filename_line - 10), -1):
    if 'for (const QQmlImports::ScriptReference' in lines[i]:
        loop_start = i
        break

if loop_start is None:
    raise SystemExit("[v271] loop start not found")

# Find the loop end (closing brace)
loop_end = None
brace_count = 0
for i in range(loop_start, len(lines)):
    brace_count += lines[i].count('{')
    brace_count -= lines[i].count('}')
    if brace_count == 0 and i > loop_start:
        loop_end = i
        break

if loop_end is None:
    raise SystemExit("[v271] loop end not found")

# Extract the loop
old_loop = '\n'.join(lines[loop_start:loop_end + 1])

new_loop = """#if !defined(BFREE_GUEST_FIXED_STACK)
""" + old_loop + """
#else
    bfree_guest_qv4_heartbeat("typedata_skip_script_filename");
#endif"""

# Replace
tt = tt.replace(old_loop, new_loop, 1)

print(f"[v271] ok (skip script.fileName loop at lines {loop_start+1}-{loop_end+1})")

td.write_text(tt)
print("[v271] script.fileName skip done")
