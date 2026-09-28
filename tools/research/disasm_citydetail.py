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


def dump_code(va, size=64):
    fo = va_to_file(va)
    b = data[fo : fo + size]
    print(f"--- code @ {hex(va)} ---")
    print(b.hex())
    # simple decode of pushes/calls/movs involving imm32
    i = 0
    while i < len(b) - 5:
        op = b[i]
        if op == 0x68:  # push imm32
            imm = struct.unpack_from("<I", b, i + 1)[0]
            print(f"  +{i:02x}: push 0x{imm:08X}")
            i += 5
            continue
        if op == 0xE8:  # call rel32
            rel = struct.unpack_from("<i", b, i + 1)[0]
            tgt = va + i + 5 + rel
            print(f"  +{i:02x}: call 0x{tgt:08X}")
            i += 5
            continue
        if op in (0xA1,):  # mov eax, [imm]
            imm = struct.unpack_from("<I", b, i + 1)[0]
            print(f"  +{i:02x}: mov eax,[0x{imm:08X}]")
            i += 5
            continue
        if op == 0xC7 and i + 6 < len(b) and b[i + 1] == 0x05:  # mov [imm], imm
            addr = struct.unpack_from("<I", b, i + 2)[0]
            val = struct.unpack_from("<I", b, i + 6)[0]
            print(f"  +{i:02x}: mov dword [0x{addr:08X}], 0x{val:08X} ({struct.unpack('<f', struct.pack('<I', val))[0]})")
            i += 10
            continue
        i += 1


# CityDetail use sites — dump larger window before the string push
for site in [0x412829, 0x414139]:
    dump_code(site - 0x20, 96)

print("\n=== alt lod floats @ 0xD16AF0 ===")
fo = va_to_file(0xD16AF0)
print([struct.unpack_from("<f", data, fo + i)[0] for i in range(0, 32, 4)])
# xref this address
imm = struct.pack("<I", 0xD16AF0)
text = data[sections[0][2] : sections[0][2] + sections[0][3]]
hits = []
start = 0
while len(hits) < 15:
    i = text.find(imm, start)
    if i < 0:
        break
    hits.append(0x401000 + i)  # text VA base = image+0x1000 = 0x401000, file raw=0x1000
    # more accurately:
    hits[-1] = image_base + sections[0][0] + i
    start = i + 1
print("xrefs to D16AF0", [hex(h) for h in hits])

# Also xref A4A900 rdata defaults
imm = struct.pack("<I", 0xA4A900)
hits = []
start = 0
while len(hits) < 15:
    i = text.find(imm, start)
    if i < 0:
        break
    hits.append(image_base + sections[0][0] + i)
    start = i + 1
print("xrefs to A4A900", [hex(h) for h in hits])

# sm_buildinglod call site
dump_code(0x4E6480, 80)
