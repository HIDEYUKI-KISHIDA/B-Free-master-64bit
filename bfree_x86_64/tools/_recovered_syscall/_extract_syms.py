import struct
from pathlib import Path

p = Path("kernel/sysmain/syscall.o")
data = p.read_bytes()
print("size", len(data), "magic", data[:4])
assert data[:4] == b"\x7fELF"
e_shoff = struct.unpack_from("<Q", data, 40)[0]
e_shentsize = struct.unpack_from("<H", data, 58)[0]
e_shnum = struct.unpack_from("<H", data, 60)[0]
e_shstrndx = struct.unpack_from("<H", data, 62)[0]


def shdr(i):
    o = e_shoff + i * e_shentsize
    return struct.unpack_from("<IIQQQQIIQQ", data, o)


sn, st, sf, sa, so, ss, sl, si, sal, se = shdr(e_shstrndx)
shstr = data[so : so + ss]
secs = []
for i in range(e_shnum):
    n, t, f, a, o, s, l, inf, al, e = shdr(i)
    name = shstr[n : shstr.find(b"\0", n)].decode()
    secs.append((name, t, o, s, l, inf, e))

keys = [
    "as_copy",
    "inet",
    "parked",
    "exec_transfer",
    "unix_socks",
    "coop_as",
    "timerfd",
    "ptmx",
    "sigframe",
    "futex",
    "CLONE",
    "g_guest",
    "g_bfree",
    "g_inet",
    "g_unix",
    "bfree_guest",
    "bfree_inet",
    "bfree_coop",
]
hits = []
all_defined = []
for name, t, o, s, l, inf, e in secs:
    if name != ".symtab":
        continue
    strtab = secs[l]
    strdata = data[strtab[2] : strtab[2] + strtab[3]]
    entsize = e or 24
    for i in range(s // entsize):
        no, info, other, shndx, val, sz = struct.unpack_from("<IBBHQQ", data, o + i * entsize)
        snm = strdata[no : strdata.find(b"\0", no)].decode()
        if not snm:
            continue
        if shndx != 0:  # defined
            all_defined.append(snm)
        if any(k in snm for k in keys):
            hits.append(snm)

out = Path("tools/_recovered_syscall/_o_interest_syms.txt")
out.write_text("\n".join(sorted(set(hits))) + "\n", encoding="utf-8")
Path("tools/_recovered_syscall/_o_all_defined.txt").write_text(
    "\n".join(sorted(set(all_defined))) + "\n", encoding="utf-8"
)
print("interest", len(set(hits)), "defined", len(set(all_defined)))
for h in sorted(set(hits)):
    print(h)
