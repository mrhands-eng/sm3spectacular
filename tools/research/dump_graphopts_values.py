from pathlib import Path
import struct

data = Path(r"D:\Projects\IdeaProjects\Spider-Man 3 - The Game\Game.exe").read_bytes()
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


def va_to_file(va: int):
    rva = va - image_base
    for sva, vsz, raw, rsz in sections:
        if sva <= rva < sva + max(rsz, vsz):
            return raw + (rva - sva)
    return None


def read_cstr(va: int):
    fo = va_to_file(va)
    if fo is None:
        return "?"
    return data[fo : data.find(b"\0", fo)].decode(errors="replace")


# Parse a range of graphopts records (name, value*, type, pad)
def dump_table(start_va, count):
    for i in range(count):
        va = start_va + i * 16
        fo = va_to_file(va)
        name_va, val_va, typ, pad = struct.unpack_from("<IIII", data, fo)
        name = read_cstr(name_va)
        vfo = va_to_file(val_va)
        if vfo is None:
            print(f"{name}: bad val ptr {hex(val_va)} type={typ}")
            continue
        if typ == 1:  # bool/byte?
            b = data[vfo]
            print(f"{name:55} type={typ} @{hex(val_va)} = byte {b} / u32 {struct.unpack_from('<I', data, vfo)[0]}")
        elif typ == 3:  # float
            f = struct.unpack_from("<f", data, vfo)[0]
            print(f"{name:55} type={typ} @{hex(val_va)} = float {f}")
        elif typ == 2:
            f = struct.unpack_from("<f", data, vfo)[0]
            u = struct.unpack_from("<I", data, vfo)[0]
            print(f"{name:55} type={typ} @{hex(val_va)} = u32 {u} / f {f}")
        else:
            u = struct.unpack_from("<I", data, vfo)[0]
            f = struct.unpack_from("<f", data, vfo)[0]
            print(f"{name:55} type={typ} @{hex(val_va)} = u32 {u} f={f}")


print("=== Building-related graphopts ===")
dump_table(0xD0E1E8, 12)

print("\n=== RENDER_LOWLODS neighborhood ===")
dump_table(0xD0CF78, 8)

print("\n=== DISTRICT_LOD ===")
# find DISTRICT ptr was 0xd0e038
dump_table(0xD0E020, 6)

# INTERIOR_DISTANCE
print("\n=== more buildings ===")
dump_table(0xD0E250, 4)
