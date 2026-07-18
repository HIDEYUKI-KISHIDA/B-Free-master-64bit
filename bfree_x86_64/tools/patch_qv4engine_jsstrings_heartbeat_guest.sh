#!/usr/bin/env bash
# Heartbeats after jsStrings / jsSymbols blocks in ExecutionEngine ctor.
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
t = p.read_text()

a_old = '    jsStrings[String_flags] = newIdentifier(QStringLiteral("flags"));\n\n    jsSymbols[Symbol_hasInstance]'
a_new = '    jsStrings[String_flags] = newIdentifier(QStringLiteral("flags"));\n    bfree_guest_qv4_heartbeat("post_jsstrings");\n\n    jsSymbols[Symbol_hasInstance]'
if 'bfree_guest_qv4_heartbeat("post_jsstrings")' not in t:
    if a_old in t:
        t = t.replace(a_old, a_new, 1)
        print("[hb_jsstrings] ok")
    else:
        raise SystemExit("[hb_jsstrings] anchor missing")
else:
    print("[hb_jsstrings] already applied")

b_old = '    jsSymbols[Symbol_revokableProxy] = Symbol::create(this, QStringLiteral("@Proxy.revokableProxy"));\n\n    ic = newInternalClass(ArrayPrototype::'
b_new = '    jsSymbols[Symbol_revokableProxy] = Symbol::create(this, QStringLiteral("@Proxy.revokableProxy"));\n    bfree_guest_qv4_heartbeat("post_jssymbols");\n\n    ic = newInternalClass(ArrayPrototype::'
if 'bfree_guest_qv4_heartbeat("post_jssymbols")' not in t:
    if b_old in t:
        t = t.replace(b_old, b_new, 1)
        print("[hb_jssymbols] ok")
    else:
        raise SystemExit("[hb_jssymbols] anchor missing")
else:
    print("[hb_jssymbols] already applied")

p.write_text(t)
PY
