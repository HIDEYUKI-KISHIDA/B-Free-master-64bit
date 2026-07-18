#!/usr/bin/env python3
"""Guest: accept MVP qmlcache units even when host qmlcachegen Qt version differs."""
from pathlib import Path

mt = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmlmetatype.cpp")
tt = mt.read_text()

if "cached_unit_skip_verify_header" in tt:
    print("[v274] cached unit verifyHeader skip already patched")
    raise SystemExit(0)

old = """        if (const QQmlPrivate::CachedQmlUnit *unit = lookup(uri)) {
            QString error;
            if (!unit->qmlData->verifyHeader(QDateTime(), &error)) {
                qCDebug(DBG_DISK_CACHE) << "Error loading pre-compiled file " << uri << ":" << error;
                if (status)
                    *status = CachedUnitLookupError::VersionMismatch;
                return nullptr;
            }"""

new = """        if (const QQmlPrivate::CachedQmlUnit *unit = lookup(uri)) {
            QString error;
#if defined(BFREE_GUEST_FIXED_STACK)
            (void)error;
            /* cached_unit_skip_verify_header: host qmlcachegen may be newer than guest Qt */
#else
            if (!unit->qmlData->verifyHeader(QDateTime(), &error)) {
                qCDebug(DBG_DISK_CACHE) << "Error loading pre-compiled file " << uri << ":" << error;
                if (status)
                    *status = CachedUnitLookupError::VersionMismatch;
                return nullptr;
            }
#endif"""

if old not in tt:
    raise SystemExit("[v274] findCachedCompilationUnit anchor missing")

mt.write_text(tt.replace(old, new, 1))
print("[v274] skip verifyHeader for guest cached QML units")
