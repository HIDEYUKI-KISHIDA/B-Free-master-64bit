#!/usr/bin/env bash
set -eu
python3 - <<'PY'
from pathlib import Path
p = Path("/root/src/qt6/qtdeclarative/src/qml/jsruntime/qv4internalclass.cpp")
t = p.read_text()
changed = False

decl_old = 'extern "C" void *bfree_guest_qv4_active_engine(void);'
decl_new = decl_old + '\nextern "C" void bfree_guest_qv4_heartbeat(const char *);'
if 'bfree_guest_qv4_heartbeat(const char *)' not in t:
    if decl_old in t:
        t = t.replace(decl_old, decl_new, 1)
        changed = True
        print("[addmember_hb] ok (extern)")
    else:
        raise SystemExit("[addmember_hb] extern anchor missing")
else:
    print("[addmember_hb] extern already present")

old = """    PropertyHash::Entry e = { identifier, newClass->size, data.isAccessor() ? newClass->size + 1 : UINT_MAX };
    newClass->propertyTable.addEntry(e, newClass->size);
    newClass->nameMap.add(newClass->size, identifier);
    newClass->propertyData.add(newClass->size, data);"""
new = """    PropertyHash::Entry e = { identifier, newClass->size, data.isAccessor() ? newClass->size + 1 : UINT_MAX };
    bfree_guest_qv4_heartbeat("add_impl_pre_entry");
    newClass->propertyTable.addEntry(e, newClass->size);
    bfree_guest_qv4_heartbeat("add_impl_post_entry");
    newClass->nameMap.add(newClass->size, identifier);
    bfree_guest_qv4_heartbeat("add_impl_post_namemap");
    newClass->propertyData.add(newClass->size, data);
    bfree_guest_qv4_heartbeat("add_impl_post_pdata");"""
if "add_impl_pre_entry" in t:
    print("[addmember_hb] heartbeats already applied")
elif old in t:
    t = t.replace(old, new, 1)
    changed = True
    print("[addmember_hb] ok (heartbeats)")
else:
    print("[addmember_hb] heartbeat anchor missing (may be ok)")

if changed:
    p.write_text(t)
PY
