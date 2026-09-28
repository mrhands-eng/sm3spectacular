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


def xrefs(addr, limit=20):
    imm = struct.pack("<I", addr)
    text = data[text_raw : text_raw + text_rsz]
    hits = []
    start = 0
    while len(hits) < limit:
        i = text.find(imm, start)
        if i < 0:
            break
        hits.append(image_base + text_va + i)
        start = i + 1
    return hits


for name in [
    b"FogValues\x00",
    b"FogColor\x00",
    b"depthfog_control\x00",
    b"depthfog_color\x00",
    b"fogs_per_meter\x00",
    b"fog_color\x00",
    b"hfog\x00",
    b"set_blur\x00",
    b"blur_intensity\x00",
]:
    fo = data.find(name)
    if fo < 0:
        print(name, "NOT FOUND")
        continue
    va = file_to_va(fo)
    print("===", name.decode().strip(chr(0)), hex(va), "xrefs", [hex(x) for x in xrefs(va)])

# Find blur state globals near set_blur RTTI at 0xD08318 area - that's RTTI in .data
# Search for float 1.0 near blur script handlers

# Disassemble set_fog_active related - find string then code
for s in [b"set_fog_active", b"set_blur\x00", b"enable_bloom"]:
    fo = data.find(s)
    print("str", s, hex(file_to_va(fo)) if fo >= 0 else None)

# Parse one CTAB for depthfog_control register
fo = data.find(b"depthfog_control")
print("depthfog_control at", hex(fo), "ctx", data[fo - 80 : fo + 40])
