#!/usr/bin/env python3
"""Guest: remove incomplete guest hacks from qv4engine.cpp that cause build errors."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
tt = td.read_text()

if "GUEST_SET_JSSYMBOL" not in tt:
    print("[v272] guest hacks already removed")
    raise SystemExit(0)

# Remove GUEST_SET_JSSYMBOL line
tt = tt.replace("    GUEST_SET_JSSYMBOL(Symbol_hasInstance, QStringLiteral(\"@Symbol.hasInstance\"));\n", "")
print("[v272] removed GUEST_SET_JSSYMBOL")

# Replace guestBuiltinStringKey with builtinStringKey
tt = tt.replace("guestBuiltinStringKey(this, QStringLiteral(\"length\"))", "builtinStringKey(this, QStringLiteral(\"length\"))")
tt = tt.replace("guestBuiltinStringKey(this, QStringLiteral(\"toString\"))", "builtinStringKey(this, QStringLiteral(\"toString\"))")
print("[v272] replaced guestBuiltinStringKey with builtinStringKey")

# Replace guestBuiltinSymbolKey with builtinSymbolKey
tt = tt.replace("guestBuiltinSymbolKey(this, Symbol_hasInstance)", "builtinSymbolKey(this, Symbol_hasInstance)")
print("[v272] replaced guestBuiltinSymbolKey with builtinSymbolKey")

# Remove duplicate numberObject declaration (line 1274)
lines = tt.split('\n')
new_lines = []
skip_next = False
for i, line in enumerate(lines):
    if skip_next:
        skip_next = False
        continue
    if 'FunctionObject *numberObject = numberCtor();' in line and i > 0:
        # Check if previous line is also numberObject declaration
        if i > 0 and 'FunctionObject *numberObject = numberCtor();' in lines[i-1]:
            print(f"[v272] removed duplicate numberObject declaration at line {i+1}")
            continue
    new_lines.append(line)

tt = '\n'.join(new_lines)

td.write_text(tt)
print("[v272] guest hacks removal done")
