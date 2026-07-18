#!/usr/bin/env python3
"""Guest: skip QUrl::fileName debug log calls in QQmlTypeData::done() to avoid crash."""
from pathlib import Path

td = Path("/root/src/qt6/qtdeclarative/src/qml/qml/qqmltypedata.cpp")
tt = td.read_text()

if "typedata_skip_filename_debuglog" in tt:
    print("[v270] fileName debuglog skip already patched")
    raise SystemExit(0)

# Skip the debug log that calls m_compiledData->fileName()
old_debuglog = """        if (error.isValid()) {
                qCDebug(DBG_DISK_CACHE)
                        << "Failed to create property caches for"
                        << m_compiledData->fileName()
                        << "because" << error.description();
            } else {
                qCDebug(DBG_DISK_CACHE)
                        << "Checksum mismatch for cached version of"
                        << m_compiledData->fileName();
            }"""

new_debuglog = """#if !defined(BFREE_GUEST_FIXED_STACK)
        if (error.isValid()) {
                qCDebug(DBG_DISK_CACHE)
                        << "Failed to create property caches for"
                        << m_compiledData->fileName()
                        << "because" << error.description();
            } else {
                qCDebug(DBG_DISK_CACHE)
                        << "Checksum mismatch for cached version of"
                        << m_compiledData->fileName();
            }
#else
            bfree_guest_qv4_heartbeat("typedata_skip_filename_debuglog");
#endif"""

if old_debuglog not in tt:
    raise SystemExit("[v270] debuglog anchor missing")
tt = tt.replace(old_debuglog, new_debuglog, 1)
print("[v270] ok (skip fileName debuglog)")

# Also skip the other fileName debug log in saveToDiskCache
old_savelog = """            qCDebug(DBG_DISK_CACHE) << "Error saving cached version of"
                                    << compilationUnit->fileName() << "to disk:" << errorString;"""

new_savelog = """#if !defined(BFREE_GUEST_FIXED_STACK)
            qCDebug(DBG_DISK_CACHE) << "Error saving cached version of"
                                    << compilationUnit->fileName() << "to disk:" << errorString;
#else
            bfree_guest_qv4_heartbeat("typedata_skip_save_debuglog");
#endif"""

if old_savelog not in tt:
    raise SystemExit("[v270] savelog anchor missing")
tt = tt.replace(old_savelog, new_savelog, 1)
print("[v270] ok (skip save debuglog)")

td.write_text(tt)
print("[v270] fileName debuglog skip done")
