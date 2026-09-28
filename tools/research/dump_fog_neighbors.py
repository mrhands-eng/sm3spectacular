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


def va_to_file(va):
    rva = va - image_base
    for sva, vsz, raw, rsz in sections:
        if sva <= rva < sva + max(rsz, vsz):
            return raw + (rva - sva)
    return None


def file_to_va(fo):
    for va, vsz, raw, rsz in sections:
        if raw <= fo < raw + max(rsz, vsz):
            return image_base + va + (fo - raw)
    return None


# Find graphopt records whose value ptr falls in D0C860..D0C8A0
for val in range(0xD0C860, 0xD0C8A0, 1):
    ptr = struct.pack("<I", val)
    start = 0
    while True:
        i = data.find(ptr, start)
        if i < 0:
            break
        # check if looks like graphopt (name ptr before)
        if i >= 4:
            name_p = struct.unpack_from("<I", data, i - 4)[0]
            if 0xA40000 < name_p < 0xC00000:
                nfo = va_to_file(name_p)
                name = data[nfo : data.find(b"\x00", nfo)].decode(errors="replace")
                typ = struct.unpack_from("<I", data, i + 4)[0]
                print(hex(val), "type", typ, name, "rec", hex(file_to_va(i - 4)))
        start = i + 1
