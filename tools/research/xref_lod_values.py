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


def file_to_va(fo: int):
    for sva, vsz, raw, rsz in sections:
        if raw <= fo < raw + max(rsz, vsz):
            return image_base + sva + (fo - raw)
    return None


# Find code xrefs: who displaces/loads from value addresses
# High LOD distance value lives at VA 0xD0C85C
targets = {
    "highLodDist": 0xD0C85C,
    "medLodDist": 0xD0C860,
    "highLodEn": 0xD0C858,
    "renderLowLods": 0xD0C680,
    "districtCull": 0xD0C820,
}

# Search .text for immediate displacements matching low 32-bit abs address
# Also rip-relative unlikely on x86.
text_va, text_vsz, text_raw, text_rsz = sections[0]  # .text
text = data[text_raw : text_raw + text_rsz]

for name, va in targets.items():
    imm = struct.pack("<I", va)
    hits = []
    start = 0
    while len(hits) < 20:
        i = text.find(imm, start)
        if i < 0:
            break
        hits.append(image_base + text_va + i)
        start = i + 1
    print(f"{name} @{hex(va)} code imm hits ({len(hits)}):", [hex(h) for h in hits[:12]])
    # for first few, dump surrounding bytes as instructions-ish
    for h in hits[:3]:
        fo = va_to_file(h)
        ctx = data[fo - 8 : fo + 12]
        print(" ", hex(h), ctx.hex())
