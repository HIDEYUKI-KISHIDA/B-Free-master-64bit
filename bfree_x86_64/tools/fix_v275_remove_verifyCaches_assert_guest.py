#!/usr/bin/env python3
"""Guest: remove erroneous Q_ASSERT(verifyCaches) from v273 patch (Qt 6.8 done() has no verifyCaches)."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

old = """    if (!m_document.isNull()) {
        Q_ASSERT(verifyCaches);
#if defined(BFREE_GUEST_FIXED_STACK)"""

new = """    if (!m_document.isNull()) {
#if defined(BFREE_GUEST_FIXED_STACK)"""

if old not in tt:
    if "Q_ASSERT(verifyCaches)" not in tt:
        print("[v275] verifyCaches assert not present; skip")
        raise SystemExit(0)
    raise SystemExit("[v275] verifyCaches assert anchor missing")

td.write_text(tt.replace(old, new, 1))
print("[v275] removed Q_ASSERT(verifyCaches) from done()")
