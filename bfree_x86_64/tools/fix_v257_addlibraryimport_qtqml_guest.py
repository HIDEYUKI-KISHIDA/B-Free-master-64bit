#!/usr/bin/env python3
"""Extend addLibraryImport guest short-circuit to QtQml module."""
from pathlib import Path

tl = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypeloader.cpp")
te = tl.read_text()

old = """    if (import->uri == QLatin1String("QtQuick")
            || import->uri == QLatin1String("QtQuick.Window")) {"""

new = """    if (import->uri == QLatin1String("QtQuick")
            || import->uri == QLatin1String("QtQuick.Window")
            || import->uri == QLatin1String("QtQml")) {"""

if new in te:
    print("[v257] QtQml already in addLibraryImport guest block")
elif old not in te:
    raise SystemExit("[v257] addLibraryImport anchor missing")
else:
    tl.write_text(te.replace(old, new, 1))
    print("[v257] addLibraryImport guest includes QtQml")
