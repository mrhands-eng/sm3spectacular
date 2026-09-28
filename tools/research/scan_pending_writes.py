from pathlib import Path
import struct

data = Path(r"D:\Projects\IdeaProjects\Spider-Man 3 - The Game\Game.exe").read_bytes()
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
numsec = struct.unpack_from("<H", data, e_lfanew + 6)[0]
opt = e_lfanew + 24
opt_size = struct.unpack_from("<H", data, e_lfanew + 20)[0]
image_base = struct.unpack_from("<I", data, opt + 28)[0]
sec_off = opt + opt_size
sections = []
for i in range(numsec):
    off = sec_off + i * 40
    name = data[off : off + 8].split(b"\0", 1)[0].decode()
    vsz, va, rsz, raw = struct.unpack_from("<IIII", data, off + 8)
    sections.append((name, va, vsz, raw, rsz))


def va_to_file(va):
    rva = va - image_base
    for name, sva, vsz, raw, rsz in sections:
        if sva <= rva < sva + max(vsz, rsz):
            return raw + (rva - sva)
    return None


def file_to_va(foff):
    for name, sva, vsz, raw, rsz in sections:
        if raw <= foff < raw + max(rsz, 1):
            return image_base + sva + (foff - raw)
    return None


text = next(s for s in sections if s[0] == ".text")
_, va0, vsz, raw0, rsz = text

hits = []
for i in range(raw0, raw0 + rsz - 10):
    # mov [reg+0x270], r32
    if data[i] == 0x89 and data[i + 2 : i + 6] == b"\x70\x02\x00\x00":
        modrm = data[i + 1]
        if (modrm >> 6) & 3 == 2:
            hits.append((file_to_va(i), "mov", data[i : i + 6].hex(), modrm))
    # mov dword [reg+0x270], imm32
    if data[i] == 0xC7 and data[i + 2 : i + 6] == b"\x70\x02\x00\x00":
        modrm = data[i + 1]
        if (modrm >> 6) & 3 == 2:
            imm = struct.unpack_from("<I", data, i + 6)[0]
            hits.append((file_to_va(i), f"imm={imm:#x}", data[i : i + 10].hex(), modrm))

print(f"total +0x270 writes in .text: {len(hits)}")
# Camera-ish range
for va, kind, hx, modrm in sorted(hits):
    if 0x00600000 <= va <= 0x006A0000:
        print(f"  {va:#x} {kind:12s} modrm={modrm:02x} {hx}")

# Disassemble ApplyMode briefly - dump bytes
fo = va_to_file(0x00640B10)
print("\nApplyMode dump:")
print(data[fo : fo + 0x80].hex())

# Find call sites to ApplyMode (E8 rel32)
apply = 0x00640B10
callers = []
for i in range(raw0, raw0 + rsz - 5):
    if data[i] == 0xE8:
        rel = struct.unpack_from("<i", data, i + 1)[0]
        tgt = file_to_va(i) + 5 + rel
        if tgt == apply:
            callers.append(file_to_va(i))
print(f"\ncallers of ApplyMode: {len(callers)}")
for c in callers[:40]:
    print(f"  {c:#x}")
