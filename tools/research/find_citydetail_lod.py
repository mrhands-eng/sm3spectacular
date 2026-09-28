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
text_va, text_vsz, text_raw, text_rsz = sections[0]


def file_to_va(fo):
    for sva, vsz, raw, rsz in sections:
        if raw <= fo < raw + max(rsz, vsz):
            return image_base + sva + (fo - raw)
    return None


def va_to_file(va):
    rva = va - image_base
    for sva, vsz, raw, rsz in sections:
        if sva <= rva < sva + max(rsz, vsz):
            return raw + (rva - sva)
    return None


def read_cstr(va):
    fo = va_to_file(va)
    if fo is None:
        return None
    return data[fo : data.find(b"\0", fo)].decode(errors="replace")


# CityDetail registry path string
fo = data.find(b"Settings\\Display\\CityDetail")
print("CityDetail path", hex(fo), "va", hex(file_to_va(fo)))
city_va = file_to_va(fo)
ptr = struct.pack("<I", city_va)
refs = []
start = 0
while True:
    i = data.find(ptr, start)
    if i < 0:
        break
    refs.append(file_to_va(i))
    start = i + 1
print("CityDetail ptr refs", [hex(r) for r in refs])

# Search for sm_buildinglod type name usage
fo = data.find(b"sm_buildinglod")
print("\nsm_buildinglod", hex(file_to_va(fo)))
ptr = struct.pack("<I", file_to_va(fo))
refs = []
start = 0
while len(refs) < 15:
    i = data.find(ptr, start)
    if i < 0:
        break
    refs.append(file_to_va(i))
    start = i + 1
print("refs", [hex(r) for r in refs])

# SMBUILDINGLODPARAM - likely a typed param block; find nearby floats in .data after string table
# Disassemble-ish: find push of CityDetail string then nearby call
for r in refs[:5]:
    fo = va_to_file(r)
    print(hex(r), data[fo - 16 : fo + 32].hex())

# Look for float 10.0f and 30.0f pairs in .data close together (high/med lod defaults we found)
# already know addresses. Search for OTHER copies of lod distances - maybe runtime mirrors
f10 = struct.pack("<f", 10.0)
f30 = struct.pack("<f", 30.0)
# find f10 followed within 16 bytes by f30
hits = []
start = 0
while len(hits) < 30:
    i = data.find(f10, start)
    if i < 0:
        break
    window = data[i : i + 32]
    if f30 in window:
        hits.append((file_to_va(i), window.hex()))
    start = i + 1
print("\n10.0 near 30.0 count", len(hits))
for va, hx in hits[:15]:
    print(hex(va), hx)
