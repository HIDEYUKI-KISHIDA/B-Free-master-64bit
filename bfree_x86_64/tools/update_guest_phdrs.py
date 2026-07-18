#!/usr/bin/env python3
"""Regenerate bfree_guest_phdrs[] in tools/guest_link_compat.cpp from desktop.elf."""
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ELF = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "userland/desktop_qt/desktop.elf"
COMPAT = ROOT / "tools/guest_link_compat.cpp"

PT_LOAD = 1
PT_TLS = 7
PT_GNU_EH_FRAME = 0x6474E551
PF_R, PF_W, PF_X = 4, 2, 1


def read_phdrs(path: Path):
    data = path.read_bytes()
    if data[:4] != b"\x7fELF":
        raise SystemExit(f"not ELF: {path}")
    e_phoff, = struct.unpack_from("<Q", data, 32)
    e_phentsize, e_phnum = struct.unpack_from("<HH", data, 54)
    out = []
    off = e_phoff
    for _ in range(e_phnum):
        ph = data[off : off + e_phentsize]
        ptype, pflags, poff, vaddr, paddr, filesz, memsz, align = struct.unpack_from(
            "<IIQQQQQQ", ph
        )
        out.append((ptype, pflags, poff, vaddr, paddr, filesz, memsz, align))
        off += e_phentsize
    return out


def phdr_c_line(ptype, pflags, poff, vaddr, paddr, filesz, memsz, align):
    if ptype == PT_LOAD:
        name = "PT_LOAD"
        fl = "PF_R | PF_W | PF_X"
    elif ptype == PT_TLS:
        name = "PT_TLS"
        fl = "PF_R"
    elif ptype == PT_GNU_EH_FRAME:
        name = "PT_GNU_EH_FRAME"
        fl = "PF_R"
    else:
        name = f"0x{ptype:x}"
        fl = f"0x{pflags:x}"
    return (
        f"    {{ {name}, {fl}, 0x{poff:x}, 0x{vaddr:x}, 0x{paddr:x}, "
        f"0x{filesz:x}, 0x{memsz:x}, 0x{align:x} }},"
    )


def main():
    if not ELF.is_file():
        raise SystemExit(f"missing: {ELF}")
    phdrs = read_phdrs(ELF)
    lines = ["static const Elf64_Phdr bfree_guest_phdrs[] = {"]
    lines += [phdr_c_line(*ph) for ph in phdrs]
    lines.append("};")
    block = "\n".join(lines)
    text = COMPAT.read_text(encoding="utf-8")
    new_text, n = re.subn(
        r"static const Elf64_Phdr bfree_guest_phdrs\[\] = \{.*?\};",
        block,
        text,
        count=1,
        flags=re.DOTALL,
    )
    if n != 1:
        raise SystemExit("bfree_guest_phdrs[] not found in guest_link_compat.cpp")
    if new_text != text:
        COMPAT.write_text(new_text, encoding="utf-8", newline="\n")
        print(f"updated {COMPAT} from {ELF}")
    else:
        print(f"phdrs unchanged in {COMPAT}")


if __name__ == "__main__":
    main()
