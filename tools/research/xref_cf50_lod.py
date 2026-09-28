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


def va_to_file(va):
    rva = va - image_base
    for sva, vsz, raw, rsz in sections:
        if sva <= rva < sva + max(rsz, vsz):
            return raw + (rva - sva)
    return None


def floats(va, n):
    fo = va_to_file(va)
    return [struct.unpack_from("<f", data, fo + 4 * i)[0] for i in range(n)]


# Full CityDetail switch starting at sub eax,1
fo = va_to_file(0x412861)
chunk = data[fo : fo + 0x280]
print("A43B00 table:", floats(0xA43B00, 12))
print("CF5020 block:", floats(0xCF5020, 12))

# Xrefs to CF502C - who else writes/reads
text_va, _, text_raw, text_rsz = sections[0]
text = data[text_raw : text_raw + text_rsz]
for addr in [0xCF502C, 0xCF5030, 0xCF5034, 0xCF5040, 0xCF550C, 0xCF5510]:
    imm = struct.pack("<I", addr)
    hits = []
    start = 0
    while len(hits) < 25:
        i = text.find(imm, start)
        if i < 0:
            break
        hits.append(image_base + text_va + i)
        start = i + 1
    print(hex(addr), "hits", len(hits), [hex(h) for h in hits[:10]])
