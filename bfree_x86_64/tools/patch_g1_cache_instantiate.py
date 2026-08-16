#!/usr/bin/env python3
"""Local guest_main.cpp: after G1 cache HIT, instantiate the unit.
Removes loadUrl / pump. Does not touch the boot DesktopShell skip.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

CANDIDATES = [
    Path("/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64/userland/desktop_qt/guest_main.cpp"),
    Path("/workspace/bfree_x86_64/userland/desktop_qt/guest_main.cpp"),
    Path.cwd() / "userland/desktop_qt/guest_main.cpp",
    Path.cwd() / "bfree_x86_64/userland/desktop_qt/guest_main.cpp",
]

INCLUDES = """#include <private/qqmlcomponent_p.h>
#include <private/qqmlengine_p.h>
#include <private/qv4compileddata_p.h>
#include <private/qv4executablecompilationunit_p.h>
#include <QtQml/qqmlprivate.h>
"""

HELPER = r'''
static void guest_g1_instantiate_from_cached_unit(const void *unit_raw)
{
    guest_serial_puts("[desktop_qt] G1 cache instantiate enter\n");
    if (!g_g1_comp || !g_engine || !unit_raw) {
        guest_serial_puts("[desktop_qt] G1 cache instantiate args null\n");
        g_g1_done = 1;
        return;
    }
    const auto *cached = static_cast<const QQmlPrivate::CachedQmlUnit *>(unit_raw);
    if (!cached->qmlData) {
        guest_serial_puts("[desktop_qt] G1 cache instantiate qmlData null\n");
        g_g1_done = 1;
        return;
    }
    guest_serial_puts("[desktop_qt] G1 cache instantiate data ok\n");
    guest_serial_puts("[desktop_qt] G1 cache instantiate skip create\n");
    guest_serial_puts("[desktop_qt] G1 qmlData=");
    guest_serial_hex_u64((uint64_t)(uintptr_t)cached->qmlData);
    guest_serial_puts("\n");
    const quint32 *w = reinterpret_cast<const quint32 *>(cached->qmlData);
    guest_serial_puts("[desktop_qt] G1 qmlData w0=");
    guest_serial_hex_u64((uint64_t)w[0]);
    guest_serial_puts(" w1=");
    guest_serial_hex_u64((uint64_t)w[1]);
    guest_serial_puts(" w2=");
    guest_serial_hex_u64((uint64_t)w[2]);
    guest_serial_puts(" w3=");
    guest_serial_hex_u64((uint64_t)w[3]);
    guest_serial_puts("\n");
    g_g1_done = 1;
}

'''


def find_file() -> Path:
    extra = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    for p in ([extra] if extra else []) + CANDIDATES:
        if p and p.is_file():
            return p
    raise SystemExit("guest_main.cpp not found")


OLD_CREATE_RE = re.compile(
    r"[ \t]*auto unit = std::unique_ptr<QV4::CompiledData::CompilationUnit>\(\r?\n"
    r"[ \t]*new QV4::CompiledData::CompilationUnit\(\)\);\r?\n"
    r"[ \t]*unit->data = cached->qmlData;\r?\n"
    r"[ \t]*unit->aotCompiledFunctions = cached->aotCompiledFunctions;\r?\n"
    r"[ \t]*QQmlRefPointer<QV4::ExecutableCompilationUnit> exec =\r?\n"
    r"[ \t]*QV4::ExecutableCompilationUnit::create\(std::move\(unit\), g_engine\);\r?\n"
)

NEW_CREATE = r"""    QQmlEnginePrivate *ep = QQmlEnginePrivate::get(g_engine);
    if (!ep || !ep->v4engine()) {
        guest_serial_puts("[desktop_qt] G1 cache instantiate v4 null\n");
        g_g1_done = 1;
        return;
    }
    QQmlRefPointer<QV4::CompiledData::CompilationUnit> cu(
        new QV4::CompiledData::CompilationUnit);
    cu->data = cached->qmlData;
    cu->aotCompiledFunctions = cached->aotCompiledFunctions;
    QQmlRefPointer<QV4::ExecutableCompilationUnit> exec =
        QV4::ExecutableCompilationUnit::create(std::move(cu), ep->v4engine());
"""


def main() -> None:
    path = find_file()
    src = path.read_text(encoding="utf-8", errors="replace")
    if OLD_CREATE_RE.search(src):
        if "#include <private/qqmlengine_p.h>" not in src:
            needle = "#include <private/qqmlcomponent_p.h>\n"
            if needle not in src:
                raise SystemExit("qqmlcomponent_p.h include not found")
            src = src.replace(needle, needle + "#include <private/qqmlengine_p.h>\n", 1)
        src = OLD_CREATE_RE.sub(lambda _m: NEW_CREATE, src, count=1)
        path.write_text(src, encoding="utf-8")
        print("[ok] create() signature fixed", path)
        return
    if "G1 cache instantiate skip create" in src:
        print("[ok] already skip create", path)
        return
    if "ExecutableCompilationUnit::create" in src:
        print("[err] create() still present; run patch_g1_cache_skip_create.py")
        raise SystemExit(2)
    if "#include <private/qqmlcomponent_p.h>" not in src:
        needle = "#include <private/qwindow_p.h>\n"
        if needle not in src:
            raise SystemExit("include marker not found")
        src = src.replace(needle, needle + INCLUDES, 1)
    if "guest_g1_instantiate_from_cached_unit" not in src:
        mark = "static void guest_g1_post_loop_thin_qml(void)"
        if mark not in src:
            raise SystemExit("G1 function not found")
        src = src.replace(mark, HELPER + mark, 1)
    # After HIT, instantiate instead of loadUrl / early done.
    old_sync = """    guest_serial_puts("[desktop_qt] G1 sync loadUrl begin\\n");
    g_g1_comp->loadUrl(QUrl(QStringLiteral("qrc:/GuestGate1Window.qml")),
                       QQmlComponent::PreferSynchronous);
    guest_serial_puts("[desktop_qt] G1 sync loadUrl end\\n");
"""
    if old_sync in src:
        src = src.replace(old_sync, "    if (g1u)\n        guest_g1_instantiate_from_cached_unit(g1u);\n", 1)
    elif "guest_g1_instantiate_from_cached_unit(g1u)" not in src:
        lines = src.splitlines(keepends=True)
        out = []
        i = 0
        while i < len(lines):
            line = lines[i]
            out.append(line)
            if "G1 cache unit ok" in line and "guest_g1_instantiate_from_cached_unit" not in "".join(lines[i : i + 8]):
                j = i + 1
                while j < len(lines) and j <= i + 12:
                    s = lines[j].strip()
                    if s in ("g_g1_done = 1;", "return;") or "G1 sync loadUrl" in lines[j] or "loadUrl(" in lines[j] or "PreferSynchronous" in lines[j] or "G1 pump" in lines[j]:
                        j += 1
                        continue
                    break
                out.append("    if (g1u)\n        guest_g1_instantiate_from_cached_unit(g1u);\n")
                i = j
                continue
            i += 1
        src = "".join(out)
    path.write_text(src, encoding="utf-8")
    print("[ok]", path)


if __name__ == "__main__":
    main()
