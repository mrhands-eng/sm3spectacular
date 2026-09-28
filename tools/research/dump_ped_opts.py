from pathlib import Path
import struct

data = Path(r"D:\Projects\IdeaProjects\Spider-Man 3 - The Game\Game.exe").read_bytes()
IB = 0x400000
e = struct.unpack_from("<I", data, 0x3C)[0]
coff = e + 4
nsec = struct.unpack_from("<H", data, coff + 2)[0]
opt = coff + 20
sec_off = opt + struct.unpack_from("<H", data, coff + 16)[0]
secs = []
for i in range(nsec):
    off = sec_off + i * 40
    vsz, va, rsz, raw = struct.unpack_from("<IIII", data, off + 8)
    secs.append((va, vsz, raw, rsz))


def va2fo(va):
    rva = va - IB
    for sva, vsz, raw, rsz in secs:
        if sva <= rva < sva + max(rsz, vsz):
            return raw + (rva - sva)
    return None


def read_cstr(va):
    fo = va2fo(va)
    if fo is None:
        return "?"
    end = data.find(b"\0", fo)
    if end < 0 or end - fo > 80:
        return "?"
    try:
        return data[fo:end].decode("ascii")
    except Exception:
        return "?"


for base in [0xD0D780, 0xD0D7F8, 0xD0D928, 0xD0D968]:
    print("===", hex(base), "===")
    fo = va2fo(base)
    for i in range(16):
        name_va, val_va, typ, pad = struct.unpack_from("<IIII", data, fo + i * 16)
        name = read_cstr(name_va)
        vfo = va2fo(val_va)
        if vfo is None:
            print(" ", name)
            continue
        u = struct.unpack_from("<I", data, vfo)[0]
        f = struct.unpack_from("<f", data, vfo)[0]
        print(f"  {name:45} val@{val_va:08X} t={typ} u={u} f={f}")

# Heuristic: search .data for int defaults that look like max_peds near known ped strings usage
# Find absolute refs to value 0xD0C720 / 0xD0C724 in code (.text)
print("\n=== code refs to D0C720/D0C724/D0C770 ===")
for target in [0xD0C720, 0xD0C724, 0xD0C770, 0xD0C778]:
    pat = struct.pack("<I", target)
    count = 0
    start = 0
    while count < 5:
        j = data.find(pat, start)
        if j < 0:
            break
        va = None
        rva_guess = None
        for sva, vsz, raw, rsz in secs:
            if raw <= j < raw + rsz:
                va = IB + sva + (j - raw)
                break
        print(f"  {hex(target)} ref file@{hex(j)} va@{hex(va) if va else 0}")
        count += 1
        start = j + 4
