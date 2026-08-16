#!/usr/bin/env python3
"""Local guest_main.cpp: after G1 cache HIT, instantiate the unit.
Removes loadUrl / pump. Does not touch the boot DesktopShell skip.
"""
from __future__ import annotations

import sys
from pathlib import Path

CANDIDATES = [
    Path("/mnt/c/Users/h_kis/Desktop/B-Free-master/Program/bfree_x86_64/userland/desktop_qt/guest_main.cpp"),
    Path("/workspace/bfree_x86_64/userland/desktop_qt/guest_main.cpp"),
    Path.cwd() / "userland/desktop_qt/guest_main.cpp",
    Path.cwd() / "bfree_x86_64/userland/desktop_qt/guest_main.cpp",
]

INCLUDES = """#include <private/qqmlcomponent_p.h>
#include <private/qv4compileddata_p.h>
#include <private/qv4executablecompilationunit_p.h>
#include <QtQml/qqmlprivate.h>
#include <memory>
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
    auto unit = std::unique_ptr<QV4::CompiledData::CompilationUnit>(
        new QV4::CompiledData::CompilationUnit());
    unit->data = cached->qmlData;
    unit->aotCompiledFunctions = cached->aotCompiledFunctions;
    QQmlRefPointer<QV4::ExecutableCompilationUnit> exec =
        QV4::ExecutableCompilationUnit::create(std::move(unit), g_engine);
    if (!exec) {
        guest_serial_puts("[desktop_qt] G1 cache instantiate exec null\n");
        g_g1_done = 1;
        return;
    }
    guest_serial_puts("[desktop_qt] G1 cache instantiate exec ok\n");
    QQmlComponentPrivate *priv = QQmlComponentPrivate::get(g_g1_comp);
    if (!priv) {
        guest_serial_puts("[desktop_qt] G1 cache instantiate priv null\n");
        g_g1_done = 1;
        return;
    }
    priv->compilationUnit = exec;
    guest_serial_puts("[desktop_qt] G1 cache instantiate ok\n");
    guest_serial_puts("[desktop_qt] G1 IR status=");
    guest_serial_hex_u64((uint64_t)(unsigned)g_g1_comp->status());
    guest_serial_puts("\n");
    if (g_g1_comp->isReady())
        guest_serial_puts("[desktop_qt] G1 thin QML Ready\n");
    else if (g_g1_comp->isError())
        guest_serial_puts("[desktop_qt] G1 thin QML error\n");
    else
        guest_serial_puts("[desktop_qt] G1 thin QML not ready\n");
    g_g1_done = 1;
}

'''


def find_file() -> Path:
    extra = Path(sys.argv[1]) if len(sys.argv) > 1 else None
    for p in ([extra] if extra else []) + CANDIDATES:
        if p and p.is_file():
            return p
    raise SystemExit("guest_main.cpp not found")


def main() -> None:
    path = find_file()
    src = path.read_text(encoding="utf-8", errors="replace")
    if "G1 cache instantiate enter" in src and "loadUrl" not in src[src.find("guest_g1_post_loop_thin_qml") : src.find("guest_g1_post_loop_thin_qml") + 2500]:
        print("[ok] already instantiated path", path)
        return
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
