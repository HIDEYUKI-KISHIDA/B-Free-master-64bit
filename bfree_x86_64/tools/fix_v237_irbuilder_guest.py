#!/usr/bin/env python3
"""Guest: null-safe resolveQualifiedId in QmlIR IRBuilder."""
from pathlib import Path

ib = Path("/root/src/qt6/qtdeclarative/src/qml/compiler/qqmlirbuilder.cpp")
te = ib.read_text()

if "bfree_guest_irbuilder_rqid" in te:
    print("[v237] irbuilder resolveQualifiedId already patched")
    raise SystemExit(0)

old = """bool IRBuilder::resolveQualifiedId(QQmlJS::AST::UiQualifiedId **nameToResolve, Object **object, bool onAssignment)
{
    QQmlJS::AST::UiQualifiedId *qualifiedIdElement = *nameToResolve;

    if (qualifiedIdElement->name == QLatin1String("id") && qualifiedIdElement->next)"""

new = """bool IRBuilder::resolveQualifiedId(QQmlJS::AST::UiQualifiedId **nameToResolve, Object **object, bool onAssignment)
{
#if defined(BFREE_GUEST_FIXED_STACK)
    if (!nameToResolve || !*nameToResolve || !object || !_object)
        return false;
#endif
    QQmlJS::AST::UiQualifiedId *qualifiedIdElement = *nameToResolve;

    if (qualifiedIdElement->name == QLatin1String("id") && qualifiedIdElement->next)"""

if old not in te:
    raise SystemExit("[v237] resolveQualifiedId anchor missing")
te = te.replace(old, new, 1)

old2 = """    // If it's a namespace, prepend the qualifier and we'll resolve it later to the correct type.
    QString currentName = qualifiedIdElement->name.toString();"""

new2 = """    // If it's a namespace, prepend the qualifier and we'll resolve it later to the correct type.
#if defined(BFREE_GUEST_FIXED_STACK)
    QString currentName = QString::fromUtf8(qualifiedIdElement->name.toUtf8());
#else
    QString currentName = qualifiedIdElement->name.toString();
#endif"""

if old2 not in te:
    raise SystemExit("[v237] currentName anchor missing")
te = te.replace(old2, new2, 1)

ib.write_text(te)
print("[v237] irbuilder resolveQualifiedId guest guards")
