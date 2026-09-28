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


for s in [
    b"set_blur\x00",
    b"blur_on\x00",
    b"blur_off\x00",
    b"blur_intensity\x00",
    b"fullscreen_blur\x00",
    b"FullscreenBlur\x00",
    b"m_blur",
    b"post::render",
]:
    fo = data.find(s)
    print(s, hex(file_to_va(fo)) if fo >= 0 else None)

# RTTI / string for set_blur - find code refs to blur intensity storage
# Search float common blur amounts near DEAC92
print("bytes around DEAC80", data[va_to_file(0xDEAC80) : va_to_file(0xDEAC80) + 64].hex())

# Find xrefs to DEAC92
imm = struct.pack("<I", 0xDEAC92)
text = data[text_raw : text_raw + text_rsz]
start = 0
hits = []
while len(hits) < 20:
    i = text.find(imm, start)
    if i < 0:
        break
    hits.append(image_base + text_va + i)
    start = i + 1
print("xrefs DEAC92", [hex(h) for h in hits])

imm = struct.pack("<I", 0xDEAC90)
hits = []
start = 0
while len(hits) < 20:
    i = text.find(imm, start)
    if i < 0:
        break
    hits.append(image_base + text_va + i)
    start = i + 1
print("xrefs DEAC90", [hex(h) for h in hits])

# blur_on__num string
fo = data.find(b"blur_on__num__t")
print("blur_on va", hex(file_to_va(fo)))
# nearby floats in .data that look like blur intensity (0-1)
