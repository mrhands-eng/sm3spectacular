from pathlib import Path
import struct

data = Path(r"D:\Projects\IdeaProjects\Spider-Man 3 - The Game\Game.exe").read_bytes()
image_base = 0x400000
e = struct.unpack_from("<I", data, 0x3C)[0]
coff = e + 4
nsec = struct.unpack_from("<H", data, coff + 2)[0]
opt = coff + 20
sec_off = opt + struct.unpack_from("<H", data, coff + 16)[0]
sections = []
for i in range(nsec):
    off = sec_off + i * 40
    vsz, va, rsz, raw = struct.unpack_from("<IIII", data, off + 8)
    sections.append((va, vsz, raw, rsz))
text_va, _, text_raw, text_rsz = sections[0]


def file_to_va(fo):
    for va, vsz, raw, rsz in sections:
        if raw <= fo < raw + max(rsz, vsz):
            return image_base + va + (fo - raw)
    return None


def va_to_file(va):
    rva = va - image_base
    for sva, vsz, raw, rsz in sections:
        if sva <= rva < sva + max(rsz, vsz):
            return raw + (rva - sva)
    return None


def dump_opt(name_bytes):
    fo = data.find(name_bytes)
    if fo < 0:
        print(name_bytes, "NOT FOUND")
        return
    va = file_to_va(fo)
    print("===", name_bytes.decode().strip("\x00"), hex(va))
    ptr = struct.pack("<I", va)
    start = 0
    while True:
        j = data.find(ptr, start)
        if j < 0:
            break
        name_p, val_p, typ = struct.unpack_from("<III", data, j)
        if name_p == va:
            vfo = va_to_file(val_p) if 0x400000 < val_p < 0x1200000 else None
            val = "?"
            if vfo is not None:
                if typ == 1:
                    val = "byte %d" % data[vfo]
                elif typ == 2:
                    val = "int %d" % struct.unpack_from("<I", data, vfo)[0]
                elif typ == 3:
                    val = "float %g" % struct.unpack_from("<f", data, vfo)[0]
            print("  rec", hex(file_to_va(j)), "type", typ, "val@", hex(val_p), val)
        start = j + 1


for n in [
    b"RENDER_SHADOWS\x00",
    b"Settings\\Display\\Shadow\x00",
    b"GRAPHOPTS_SHADOWS_SMALLEST_DIM\x00",
    b"GRAPHOPTS_SHADOWS_STEP_FACTOR\x00",
    b"GRAPHOPTS_SHADOWS_DEPTH_FACTOR\x00",
    b"GRAPHOPTS_BUILDINGS_SHADOW_SCALE\x00",
]:
    dump_opt(n)

# neighborhood around D0C84C-D0C870
print("\n=== floats around shadow opts ===")
fo = va_to_file(0xD0C840)
for o in range(0, 0x40, 4):
    f = struct.unpack_from("<f", data, fo + o)[0]
    u = struct.unpack_from("<I", data, fo + o)[0]
    print(hex(0xD0C840 + o), "f=%g" % f, "u=%u" % u)

# xrefs to D0C84C, D0C850, D0C854, D0C870
print("\n=== code xrefs ===")
text = data[text_raw : text_raw + text_rsz]
for addr in [0xD0C84C, 0xD0C850, 0xD0C854, 0xD0C870, 0xD0C832]:
    imm = struct.pack("<I", addr)
    hits = []
    start = 0
    while len(hits) < 8:
        i = text.find(imm, start)
        if i < 0:
            break
        hits.append(image_base + text_va + i)
        start = i + 1
    print(hex(addr), [hex(h) for h in hits])

# Shadow display setting variable
fo = data.find(b"Settings\\Display\\Shadow\x00")
print("\nShadow setting path", hex(file_to_va(fo)))
# CityDetail-like: find DE30xx nearby for Shadow int
for va in range(0xDE30D0, 0xDE3120, 4):
    fo = va_to_file(va)
    print(hex(va), struct.unpack_from("<I", data, fo)[0])
