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


def floats(va, n=8):
    fo = va_to_file(va)
    return [struct.unpack_from("<f", data, fo + 4 * i)[0] for i in range(n)]


# From CityDetail switch at 0x412861
print("A43B20..", floats(0xA43B20, 4))
print("A43B24..", floats(0xA43B24, 4))
print("A43778..", floats(0xA43778, 4))
print("CF502C..", floats(0xCF502C, 8))
print("CF5510..", floats(0xCF5510, 8))

# Dump more of the CityDetail switch function to get all level float tables
fo = va_to_file(0x412861)
chunk = data[fo : fo + 0x200]
print("\nCityDetail switch function (partial):")
# find all movss from [imm] F3 0F 10 05 xx xx xx xx
i = 0
while i < len(chunk) - 8:
    if chunk[i : i + 4] == b"\xf3\x0f\x10\x05":
        addr = struct.unpack_from("<I", chunk, i + 4)[0]
        print(f"  +{i:03X}: movss xmm0, [{hex(addr)}] = {floats(addr,1)[0]}")
        i += 8
        continue
    if chunk[i : i + 4] == b"\xf3\x0f\x10\x0d":
        addr = struct.unpack_from("<I", chunk, i + 4)[0]
        print(f"  +{i:03X}: movss xmm1, [{hex(addr)}] = {floats(addr,1)[0]}")
        i += 8
        continue
    if chunk[i : i + 4] == b"\xf3\x0f\x11\x05":
        addr = struct.unpack_from("<I", chunk, i + 4)[0]
        print(f"  +{i:03X}: movss [{hex(addr)}], xmm0")
        i += 8
        continue
    if chunk[i : i + 4] == b"\xf3\x0f\x11\x0d":
        addr = struct.unpack_from("<I", chunk, i + 4)[0]
        print(f"  +{i:03X}: movss [{hex(addr)}], xmm1")
        i += 8
        continue
    i += 1

# Also dump 0x4147b2 area for detail==3 checks
print("\n=== 0x4147A0 area ===")
fo = va_to_file(0x4147A0)
chunk = data[fo : fo + 0x80]
i = 0
while i < len(chunk) - 8:
    if chunk[i : i + 4] in (b"\xf3\x0f\x10\x05", b"\xf3\x0f\x10\x0d", b"\xf3\x0f\x11\x05", b"\xf3\x0f\x11\x0d"):
        addr = struct.unpack_from("<I", chunk, i + 4)[0]
        op = chunk[i : i + 4].hex()
        print(f"  {hex(0x4147A0+i)}: {op} [{hex(addr)}] val={floats(addr,1)[0] if va_to_file(addr) else '?'}")
        i += 8
        continue
    i += 1
