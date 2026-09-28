from pathlib import Path
import struct

data = Path(r"D:\Projects\IdeaProjects\Spider-Man 3 - The Game\Game.exe").read_bytes()
# PE parse
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
coff = e_lfanew + 4
nsec = struct.unpack_from("<H", data, coff + 2)[0]
opt = coff + 20
magic = struct.unpack_from("<H", data, opt)[0]
assert magic == 0x10B  # PE32
image_base = struct.unpack_from("<I", data, opt + 28)[0]
sec_off = opt + struct.unpack_from("<H", data, coff + 16)[0]
print(f"image_base=0x{image_base:X} sections={nsec}")

sections = []
for i in range(nsec):
    off = sec_off + i * 40
    name = data[off : off + 8].split(b"\0", 1)[0].decode()
    vsz, va, rsz, raw = struct.unpack_from("<IIII", data, off + 8)
    sections.append((name, va, vsz, raw, rsz))
    print(f"  {name:8} VA=0x{va:08X} VSZ=0x{vsz:X} RAW=0x{raw:X}")


def file_to_va(file_off: int):
    for name, va, vsz, raw, rsz in sections:
        if raw <= file_off < raw + max(rsz, vsz):
            return image_base + va + (file_off - raw)
    return None


def va_to_file(va: int):
    rva = va - image_base
    for name, sva, vsz, raw, rsz in sections:
        if sva <= rva < sva + max(rsz, vsz):
            return raw + (rva - sva)
    return None


keys = [
    b"GRAPHOPTS_BUILDINGS_HIGH_LOD_DISTANCE",
    b"GRAPHOPTS_BUILDINGS_HIGH_LOD_ENABLED",
    b"GRAPHOPTS_BUILDINGS_MEDIUM_LOD_DISTANCE",
    b"RENDER_LOWLODS",
    b"DISTRICT_LOD_CULLING",
]
for k in keys:
    fo = data.find(k)
    va = file_to_va(fo)
    print(f"\n{k.decode()} file=0x{fo:X} va=0x{va:X}")
    # find absolute pointer refs (LE)
    ptr = struct.pack("<I", va)
    refs = []
    start = 0
    while len(refs) < 12:
        i = data.find(ptr, start)
        if i < 0:
            break
        refs.append((i, file_to_va(i)))
        start = i + 1
    print("  ptr refs:", [(hex(a), hex(b) if b else None) for a, b in refs[:8]])
