#!/usr/bin/env bash
# Guest: guard QBindingStorage against null/corrupt d (#GP in bindingData_helper).
set -eu
python3 - <<'PY'
from pathlib import Path

path = Path("/root/src/qt6/qtbase/src/corelib/kernel/qproperty.cpp")
text = path.read_text()

helper = """
namespace {
static inline bool bfree_binding_d_ok(const QBindingStorageData *pd)
{
    if (!pd)
        return false;
    const quintptr u = quintptr(pd);
    return u >= 0x1000 && u <= ((quintptr(1) << 63) - 1);
}
} // namespace

"""

if "bfree_binding_d_ok" not in text:
    anchor = "struct QBindingStoragePrivate\n"
    if anchor not in text:
        raise SystemExit("[patch_qbindingstorage_guest] struct anchor missing")
    text = text.replace(anchor, helper + anchor, 1)
    print("[patch_qbindingstorage_guest] helper: ok")
else:
    print("[patch_qbindingstorage_guest] helper: already applied")

patches = [
    (
        "reallocate malloc",
        """        void *nd = malloc(allocSize);
        memset(nd, 0, allocSize);""",
        """        void *nd = malloc(allocSize);
        if (!nd)
            return;
        memset(nd, 0, allocSize);""",
    ),
    (
        "get const",
        """    QPropertyBindingData *get(const QUntypedPropertyData *data)
    {
        if (!d || !d->size || (d->size & (d->size - 1)))
            return nullptr;
        size_t index = qHash(data) & (d->size - 1);""",
        """    QPropertyBindingData *get(const QUntypedPropertyData *data)
    {
        if (!d || !bfree_binding_d_ok(d) || !d->size || d->size > 65536
            || (d->size & (d->size - 1)))
            return nullptr;
        size_t index = qHash(data) & (d->size - 1);""",
    ),
    (
        "get const legacy",
        """    QPropertyBindingData *get(const QUntypedPropertyData *data)
    {
        Q_ASSERT(d);
        Q_ASSERT(d->size && (d->size & (d->size - 1)) == 0); // size is a power of two
        size_t index = qHash(data) & (d->size - 1);""",
        """    QPropertyBindingData *get(const QUntypedPropertyData *data)
    {
        if (!d || !bfree_binding_d_ok(d) || !d->size || d->size > 65536
            || (d->size & (d->size - 1)))
            return nullptr;
        size_t index = qHash(data) & (d->size - 1);""",
    ),
    (
        "get non-const corrupt d",
        """    QPropertyBindingData *get(QUntypedPropertyData *data, bool create)
    {
        if (!d) {""",
        """    QPropertyBindingData *get(QUntypedPropertyData *data, bool create)
    {
        if (d && !bfree_binding_d_ok(d))
            d = nullptr;
        if (!d) {""",
    ),
    (
        "get non-const size guard",
        """        else if (d->used*2 >= d->size)
            reallocate(d->size*2);
        Q_ASSERT(d->size && (d->size & (d->size - 1)) == 0); // size is a power of two
        size_t index = qHash(data) & (d->size - 1);""",
        """        else if (d->used*2 >= d->size)
            reallocate(d->size*2);
        if (!d || !bfree_binding_d_ok(d) || !d->size || d->size > 65536
            || (d->size & (d->size - 1)))
            return nullptr;
        size_t index = qHash(data) & (d->size - 1);""",
    ),
    (
        "bindingData_helper const",
        """QPropertyBindingData *QBindingStorage::bindingData_helper(const QUntypedPropertyData *data) const
{
    if (!d)
        return nullptr;
    return QBindingStoragePrivate(d).get(data);
}""",
        """QPropertyBindingData *QBindingStorage::bindingData_helper(const QUntypedPropertyData *data) const
{
    if (!d || !bfree_binding_d_ok(d))
        return nullptr;
    return QBindingStoragePrivate(d).get(data);
}""",
    ),
    (
        "bindingData_helper const legacy",
        """QPropertyBindingData *QBindingStorage::bindingData_helper(const QUntypedPropertyData *data) const
{
    return QBindingStoragePrivate(d).get(data);
}""",
        """QPropertyBindingData *QBindingStorage::bindingData_helper(const QUntypedPropertyData *data) const
{
    if (!d || !bfree_binding_d_ok(d))
        return nullptr;
    return QBindingStoragePrivate(d).get(data);
}""",
    ),
    (
        "registerDependency_helper",
        """    if (!currentBinding)
        return;
    if (!d)
        return;
    auto storage = QBindingStoragePrivate(d).get(dd, true);""",
        """    if (!currentBinding)
        return;
    if (!d || !bfree_binding_d_ok(d))
        return;
    auto storage = QBindingStoragePrivate(d).get(dd, true);""",
    ),
    (
        "bindingData_helper const guest stub v335",
        """QPropertyBindingData *QBindingStorage::bindingData_helper(const QUntypedPropertyData *data) const
{
    if (!d || !bfree_binding_d_ok(d))
        return nullptr;
    return QBindingStoragePrivate(d).get(data);
}""",
        """QPropertyBindingData *QBindingStorage::bindingData_helper(const QUntypedPropertyData *data) const
{
    Q_UNUSED(data);
    return nullptr;
}""",
    ),
    (
        "registerDependency_helper legacy",
        """    if (!currentBinding)
        return;
    auto storage = QBindingStoragePrivate(d).get(dd, true);""",
        """    if (!currentBinding)
        return;
    if (!d || !bfree_binding_d_ok(d))
        return;
    auto storage = QBindingStoragePrivate(d).get(dd, true);""",
    ),
]

changed = 0
for tag, old, new in patches:
    if new in text:
        print(f"[patch_qbindingstorage_guest] {tag}: already applied")
    elif old in text:
        text = text.replace(old, new, 1)
        changed += 1
        print(f"[patch_qbindingstorage_guest] {tag}: ok")
    else:
        print(f"[patch_qbindingstorage_guest] {tag}: skip (no anchor)")

if changed:
    path.write_text(text)
print(f"[patch_qbindingstorage_guest] done ({changed} updated)")
PY
