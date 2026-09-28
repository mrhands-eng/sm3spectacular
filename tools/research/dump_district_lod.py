from pathlib import Path
import struct

data = Path(r"D:\Projects\IdeaProjects\Spider-Man 3 - The Game\Game.exe").read_bytes()
# DISTRICT_LOD_CULLING name at 0xA56EA8, ptr ref at 0xD0E038 from earlier xref
# dump 16-byte aligned entries searching for name ptr

image_base = 0x400000
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
coff = e_lfanew + 4
nsec = struct.unpack_from("<H", data, coff + 2)[0]
opt = coff + 20
sec_off = opt + struct.unpack_from("<H", data, coff + 16)[0]
sections = []
for i in range(nsec):
    off = sec_off + i * 40
    vsz, va, rsz, raw = struct.unpack_from("<IIII", data, off + 8)
    sections.append((va, vsz, raw, rsz))


def va_to_file(va):
    rva = va - image_base
    for sva, vsz, raw, rsz in sections:
        if sva <= rva < sva + max(rsz, vsz):
            return raw + (rva - sva)
    return None


def read_cstr(va):
    fo = va_to_file(va)
    return data[fo : data.find(b"\0", fo)].decode(errors="replace")


for name_va, label in [
    (0xA56EA8, "DISTRICT_LOD_CULLING"),
    (0xA56ECC, "DISTRICT_LOD_CULLING_HEIGHT_SCALE"),
    (0xA56E90, "DISTRICT_LOD_CULLING_MIN_DISTRICTS"),
]:
    ptr = struct.pack("<I", name_va)
    i = data.find(ptr)
    print(label, "name_ptr_loc file", hex(i) if i >= 0 else None)
    if i < 0:
        continue
    # assume record starts at pointer
    # find VA of this file offset in .data
    # reverse: file i -> va
    for sva, vsz, raw, rsz in sections:
        if raw <= i < raw + rsz:
            rec_va = image_base + sva + (i - raw)
            break
    name, val, typ, pad = struct.unpack_from("<IIII", data, i)
    print(f"  rec@{hex(rec_va)} name={hex(name)} val={hex(val)} type={typ}")
    vfo = va_to_file(val)
    if typ == 1:
        print("  byte", data[vfo])
    elif typ == 3:
        print("  float", struct.unpack_from("<f", data, vfo)[0])
    else:
        print("  u32", struct.unpack_from("<I", data, vfo)[0], "f", struct.unpack_from("<f", data, vfo)[0])
