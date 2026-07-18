#!/usr/bin/env bash
# Guest jsStrings[]: assign via Value::fromHeapObject so id_* handles stay valid.
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4engine.cpp")
t = p.read_text()

if "GUEST_SET_JSSTRING" in t:
    print("[jsstrings_value] already applied")
    raise SystemExit(0)

# Wrap the jsStrings assignment block (guest-only) with a macro after pre_jsstrings heartbeat.
old = '    bfree_guest_qv4_heartbeat("pre_jsstrings");\n    Scope scope(this);'
new = """    bfree_guest_qv4_heartbeat("pre_jsstrings");
#define GUEST_SET_JSSTRING(slot, text) do { \\
        Heap::String *_gs = newIdentifier(text); \\
        if (_gs) jsStrings[slot] = Value::fromHeapObject(_gs); \\
    } while (0)
    Scope scope(this);"""
if old not in t:
    raise SystemExit("[jsstrings_value] scope anchor missing")
t = t.replace(old, new, 1)

pairs = [
    ("jsStrings[String_Empty] = newIdentifier(QString());", 'GUEST_SET_JSSTRING(String_Empty, QString());'),
    ('jsStrings[String_undefined] = newIdentifier(QStringLiteral("undefined"));', 'GUEST_SET_JSSTRING(String_undefined, QStringLiteral("undefined"));'),
    ('jsStrings[String_null] = newIdentifier(QStringLiteral("null"));', 'GUEST_SET_JSSTRING(String_null, QStringLiteral("null"));'),
    ('jsStrings[String_true] = newIdentifier(QStringLiteral("true"));', 'GUEST_SET_JSSTRING(String_true, QStringLiteral("true"));'),
    ('jsStrings[String_false] = newIdentifier(QStringLiteral("false"));', 'GUEST_SET_JSSTRING(String_false, QStringLiteral("false"));'),
    ('jsStrings[String_boolean] = newIdentifier(QStringLiteral("boolean"));', 'GUEST_SET_JSSTRING(String_boolean, QStringLiteral("boolean"));'),
    ('jsStrings[String_number] = newIdentifier(QStringLiteral("number"));', 'GUEST_SET_JSSTRING(String_number, QStringLiteral("number"));'),
    ('jsStrings[String_string] = newIdentifier(QStringLiteral("string"));', 'GUEST_SET_JSSTRING(String_string, QStringLiteral("string"));'),
    ('jsStrings[String_default] = newIdentifier(QStringLiteral("default"));', 'GUEST_SET_JSSTRING(String_default, QStringLiteral("default"));'),
    ('jsStrings[String_symbol] = newIdentifier(QStringLiteral("symbol"));', 'GUEST_SET_JSSTRING(String_symbol, QStringLiteral("symbol"));'),
    ('jsStrings[String_object] = newIdentifier(QStringLiteral("object"));', 'GUEST_SET_JSSTRING(String_object, QStringLiteral("object"));'),
    ('jsStrings[String_function] = newIdentifier(QStringLiteral("function"));', 'GUEST_SET_JSSTRING(String_function, QStringLiteral("function"));'),
    ('jsStrings[String_length] = newIdentifier(QStringLiteral("length"));', 'GUEST_SET_JSSTRING(String_length, QStringLiteral("length"));'),
    ('jsStrings[String_prototype] = newIdentifier(QStringLiteral("prototype"));', 'GUEST_SET_JSSTRING(String_prototype, QStringLiteral("prototype"));'),
    ('jsStrings[String_constructor] = newIdentifier(QStringLiteral("constructor"));', 'GUEST_SET_JSSTRING(String_constructor, QStringLiteral("constructor"));'),
    ('jsStrings[String_arguments] = newIdentifier(QStringLiteral("arguments"));', 'GUEST_SET_JSSTRING(String_arguments, QStringLiteral("arguments"));'),
    ('jsStrings[String_caller] = newIdentifier(QStringLiteral("caller"));', 'GUEST_SET_JSSTRING(String_caller, QStringLiteral("caller"));'),
    ('jsStrings[String_callee] = newIdentifier(QStringLiteral("callee"));', 'GUEST_SET_JSSTRING(String_callee, QStringLiteral("callee"));'),
    ('jsStrings[String_this] = newIdentifier(QStringLiteral("this"));', 'GUEST_SET_JSSTRING(String_this, QStringLiteral("this"));'),
    ('jsStrings[String___proto__] = newIdentifier(QStringLiteral("__proto__"));', 'GUEST_SET_JSSTRING(String___proto__, QStringLiteral("__proto__"));'),
    ('jsStrings[String_enumerable] = newIdentifier(QStringLiteral("enumerable"));', 'GUEST_SET_JSSTRING(String_enumerable, QStringLiteral("enumerable"));'),
    ('jsStrings[String_configurable] = newIdentifier(QStringLiteral("configurable"));', 'GUEST_SET_JSSTRING(String_configurable, QStringLiteral("configurable"));'),
    ('jsStrings[String_writable] = newIdentifier(QStringLiteral("writable"));', 'GUEST_SET_JSSTRING(String_writable, QStringLiteral("writable"));'),
    ('jsStrings[String_value] = newIdentifier(QStringLiteral("value"));', 'GUEST_SET_JSSTRING(String_value, QStringLiteral("value"));'),
    ('jsStrings[String_get] = newIdentifier(QStringLiteral("get"));', 'GUEST_SET_JSSTRING(String_get, QStringLiteral("get"));'),
    ('jsStrings[String_set] = newIdentifier(QStringLiteral("set"));', 'GUEST_SET_JSSTRING(String_set, QStringLiteral("set"));'),
    ('jsStrings[String_eval] = newIdentifier(QStringLiteral("eval"));', 'GUEST_SET_JSSTRING(String_eval, QStringLiteral("eval"));'),
    ('jsStrings[String_uintMax] = newIdentifier(QStringLiteral("4294967295"));', 'GUEST_SET_JSSTRING(String_uintMax, QStringLiteral("4294967295"));'),
    ('jsStrings[String_name] = newIdentifier(QStringLiteral("name"));', 'GUEST_SET_JSSTRING(String_name, QStringLiteral("name"));'),
    ('jsStrings[String_index] = newIdentifier(QStringLiteral("index"));', 'GUEST_SET_JSSTRING(String_index, QStringLiteral("index"));'),
    ('jsStrings[String_input] = newIdentifier(QStringLiteral("input"));', 'GUEST_SET_JSSTRING(String_input, QStringLiteral("input"));'),
    ('jsStrings[String_toString] = newIdentifier(QStringLiteral("toString"));', 'GUEST_SET_JSSTRING(String_toString, QStringLiteral("toString"));'),
    ('jsStrings[String_toLocaleString] = newIdentifier(QStringLiteral("toLocaleString"));', 'GUEST_SET_JSSTRING(String_toLocaleString, QStringLiteral("toLocaleString"));'),
    ('jsStrings[String_destroy] = newIdentifier(QStringLiteral("destroy"));', 'GUEST_SET_JSSTRING(String_destroy, QStringLiteral("destroy"));'),
    ('jsStrings[String_valueOf] = newIdentifier(QStringLiteral("valueOf"));', 'GUEST_SET_JSSTRING(String_valueOf, QStringLiteral("valueOf"));'),
    ('jsStrings[String_byteLength] = newIdentifier(QStringLiteral("byteLength"));', 'GUEST_SET_JSSTRING(String_byteLength, QStringLiteral("byteLength"));'),
    ('jsStrings[String_byteOffset] = newIdentifier(QStringLiteral("byteOffset"));', 'GUEST_SET_JSSTRING(String_byteOffset, QStringLiteral("byteOffset"));'),
    ('jsStrings[String_buffer] = newIdentifier(QStringLiteral("buffer"));', 'GUEST_SET_JSSTRING(String_buffer, QStringLiteral("buffer"));'),
    ('jsStrings[String_lastIndex] = newIdentifier(QStringLiteral("lastIndex"));', 'GUEST_SET_JSSTRING(String_lastIndex, QStringLiteral("lastIndex"));'),
    ('jsStrings[String_next] = newIdentifier(QStringLiteral("next"));', 'GUEST_SET_JSSTRING(String_next, QStringLiteral("next"));'),
    ('jsStrings[String_done] = newIdentifier(QStringLiteral("done"));', 'GUEST_SET_JSSTRING(String_done, QStringLiteral("done"));'),
    ('jsStrings[String_return] = newIdentifier(QStringLiteral("return"));', 'GUEST_SET_JSSTRING(String_return, QStringLiteral("return"));'),
    ('jsStrings[String_throw] = newIdentifier(QStringLiteral("throw"));', 'GUEST_SET_JSSTRING(String_throw, QStringLiteral("throw"));'),
    ('jsStrings[String_global] = newIdentifier(QStringLiteral("global"));', 'GUEST_SET_JSSTRING(String_global, QStringLiteral("global"));'),
    ('jsStrings[String_ignoreCase] = newIdentifier(QStringLiteral("ignoreCase"));', 'GUEST_SET_JSSTRING(String_ignoreCase, QStringLiteral("ignoreCase"));'),
    ('jsStrings[String_multiline] = newIdentifier(QStringLiteral("multiline"));', 'GUEST_SET_JSSTRING(String_multiline, QStringLiteral("multiline"));'),
    ('jsStrings[String_unicode] = newIdentifier(QStringLiteral("unicode"));', 'GUEST_SET_JSSTRING(String_unicode, QStringLiteral("unicode"));'),
    ('jsStrings[String_sticky] = newIdentifier(QStringLiteral("sticky"));', 'GUEST_SET_JSSTRING(String_sticky, QStringLiteral("sticky"));'),
    ('jsStrings[String_source] = newIdentifier(QStringLiteral("source"));', 'GUEST_SET_JSSTRING(String_source, QStringLiteral("source"));'),
    ('jsStrings[String_flags] = newIdentifier(QStringLiteral("flags"));', 'GUEST_SET_JSSTRING(String_flags, QStringLiteral("flags"));'),
]

n = 0
for old, new in pairs:
    if old in t:
        t = t.replace(old, new, 1)
        n += 1

# Only apply GUEST_SET_JSSTRING inside guest build - wrap block with #if
# The macro is only under BFREE_GUEST path where Scope was added; use #if for assignments
guest_wrap_start = '#define GUEST_SET_JSSTRING'
if guest_wrap_start in t:
    t = t.replace(
        '#define GUEST_SET_JSSTRING',
        '#if defined(BFREE_GUEST_FIXED_STACK)\n#define GUEST_SET_JSSTRING',
        1,
    )
    t = t.replace(
        '    bfree_guest_qv4_heartbeat("post_jsstrings");',
        '#else\n#define GUEST_SET_JSSTRING(slot, text) jsStrings[slot] = newIdentifier(text)\n#endif\n    bfree_guest_qv4_heartbeat("post_jsstrings");',
        1,
    )

p.write_text(t)
print(f"[jsstrings_value] ok ({n} slots)")
PY
