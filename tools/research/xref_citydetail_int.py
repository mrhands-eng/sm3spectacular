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
text_va, _, text_raw, text_rsz = sections[0]
text = data[text_raw : text_raw + text_rsz]


def dump(va, n=48):
    rva = va - image_base
    fo = text_raw + (rva - text_va)
    b = data[fo : fo + n]
    print(hex(va), b.hex())


addr = 0xDE30E4
imm = struct.pack("<I", addr)
hits = []
start = 0
while len(hits) < 40:
    i = text.find(imm, start)
    if i < 0:
        break
    hits.append(image_base + text_va + i)
    start = i + 1
print(f"xrefs to CityDetail int @ {hex(addr)}: {len(hits)}")
for h in hits:
    # show 12 bytes before as likely instruction start
    print(" @", hex(h))
    dump(h - 6, 24)

# Also search A3 mov eax,[DE30E4] pattern specifically already in hits
# Look for cmp with CityDetail and float loads nearby in a larger window
print("\n=== nearby float loads around CityDetail comparisons ===")
for h in hits[:20]:
    rva = h - image_base
    fo = text_raw + (rva - text_va)
    window = data[fo - 40 : fo + 80]
    # find float imm in window (not addresses)
    for off in range(len(window) - 4):
        if window[off : off + 1] in (b"\x68",) :  # push
            pass
    # print if we see fld/movss patterns - opcode F3 0F 10 = movss
    if b"\xf3\x0f\x10" in window or b"\xd9\x05" in window:
        print(hex(h), "has float load nearby", window.hex())
