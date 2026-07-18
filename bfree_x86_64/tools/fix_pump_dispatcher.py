#!/usr/bin/env python3
from pathlib import Path

p = Path("/root/src/qt6/qtbase/src/corelib/thread/qthread_unix.cpp")
t = p.read_text()
old = """    for (int i = 0; i < 2; ++i) {
        if (ed->processEvents(QEventLoop::AllEvents))
            ++n;
            ++n;
    }"""
new = """    for (int i = 0; i < 2; ++i) {
        if (ed->processEvents(QEventLoop::AllEvents))
            ++n;
    }"""
if old not in t:
    raise SystemExit("pump_dispatcher block not found")
p.write_text(t.replace(old, new, 1))
print("fixed pump_dispatcher")
