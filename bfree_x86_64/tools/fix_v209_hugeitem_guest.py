from pathlib import Path

cp = Path("/root/src/qt6/qtdeclarative/src/qml/memory/qv4mm.cpp")
t = cp.read_text()

huge_old = """        if (totalSize > Chunk::DataSize) {
            o = static_cast<Heap::Object *>(allocData(size));
#if defined(BFREE_GUEST_FIXED_STACK)
            if (!o)
                o = static_cast<Heap::Object *>(bfree_guest_alloc_data_fallback(size));
#endif
            m = hugeItemAllocator.allocate(memberSize)->as<Heap::MemberData>();
        } else {"""

huge_new = """        if (totalSize > Chunk::DataSize) {
#if defined(BFREE_GUEST_FIXED_STACK)
            o = static_cast<Heap::Object *>(allocData(size));
            if (!o)
                o = static_cast<Heap::Object *>(bfree_guest_alloc_data_fallback(size));
            m = bfree_guest_mm_member_fallback(memberSize, nMembers);
#else
            o = static_cast<Heap::Object *>(allocData(size));
            m = hugeItemAllocator.allocate(memberSize)->as<Heap::MemberData>();
#endif
        } else {"""

alloc_fn_old = """Heap::Object *MemoryManager::allocObjectWithMemberData(const QV4::VTable *vtable, uint nMembers)
{
    uint size = (vtable->nInlineProperties + vtable->inlinePropertyOffset)*sizeof(Value);"""

alloc_fn_new = """Heap::Object *MemoryManager::allocObjectWithMemberData(const QV4::VTable *vtable, uint nMembers)
{
#if defined(BFREE_GUEST_FIXED_STACK)
    if (nMembers > 4096)
        nMembers = 4096;
#endif
    uint size = (vtable->nInlineProperties + vtable->inlinePropertyOffset)*sizeof(Value);"""

huge_alloc_old = """HeapItem *HugeItemAllocator::allocate(size_t size) {
    MemorySegment *m = nullptr;
    Chunk *c = nullptr;
    if (size >= MemorySegment::SegmentSize/2) {"""

huge_alloc_new = """HeapItem *HugeItemAllocator::allocate(size_t size) {
#if defined(BFREE_GUEST_FIXED_STACK)
    if (size > Chunk::DataSize)
        return nullptr;
#endif
    MemorySegment *m = nullptr;
    Chunk *c = nullptr;
    if (size >= MemorySegment::SegmentSize/2) {"""

if huge_new.split("bfree_guest_mm_member_fallback")[0] in t:
    print("[v209] huge member guest fallback already patched")
elif huge_old not in t:
    raise SystemExit("[v209] allocObjectWithMemberData huge anchor missing")
else:
    t = t.replace(huge_old, huge_new, 1)
    print("[v209] allocObjectWithMemberData: skip HugeItemAllocator on guest")

if alloc_fn_new.split("nMembers > 4096")[0] in t:
    print("[v209] nMembers cap already patched")
elif alloc_fn_old not in t:
    raise SystemExit("[v209] allocObjectWithMemberData fn anchor missing")
else:
    t = t.replace(alloc_fn_old, alloc_fn_new, 1)
    print("[v209] cap nMembers on guest")

if huge_alloc_new.split("return nullptr")[0] in t:
    print("[v209] HugeItemAllocator guest guard already patched")
elif huge_alloc_old not in t:
    print("[v209] HugeItemAllocator anchor missing (skipped)")
else:
    t = t.replace(huge_alloc_old, huge_alloc_new, 1)
    print("[v209] HugeItemAllocator guest null on oversized")

cp.write_text(t)
print("[v209] done")
