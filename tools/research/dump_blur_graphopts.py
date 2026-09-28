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


names = [
    b"DISABLE_FULLSCREEN_BLUR\x00",
    b"GRAPHOPTS_BLOOM_BLUR_ITERATIONS\x00",
    b"RENDER_BLUR_TRAIL\x00",
    b"MOTION_BLUR_ADD\x00",
    b"MOTION_BLUR_SUB\x00",
    b"BLUR_ITERATIONS\x00",
    b"GRAPHOPTS_BLOOM_ENABLED\x00",
    b"GRAPHOPTS_BLOOM\x00",
    b"ENABLE_BLOOM\x00",
    b"BLOOM_ENABLED\x00",
    b"FULLSCREEN_BLUR\x00",
]

# Also list nearby GRAPHOPTS containing BLUR or BLOOM
idx = 0
while True:
    i = data.find(b"GRAPHOPTS_", idx)
    if i < 0:
        break
    end = data.find(b"\x00", i)
    s = data[i:end]
    if b"BLUR" in s or b"BLOOM" in s or b"DOF" in s or b"SOFT" in s:
        print("str", hex(file_to_va(i)), s.decode(errors="replace"))
    idx = i + 1

print("--- refs ---")
for s in names:
    fo = data.find(s)
    if fo < 0:
        print(s, "NOT FOUND")
        continue
    va = file_to_va(fo)
    print("===", s.decode().strip("\x00"), hex(va))
    ptr = struct.pack("<I", va)
    refs = []
    start = 0
    while len(refs) < 12:
        i = data.find(ptr, start)
        if i < 0:
            break
        refs.append(file_to_va(i))
        start = i + 1
    print(" refs", [hex(r) for r in refs])
    for r in refs[:3]:
        fo2 = va_to_file(r)
        for o in (-8, -4, 0, 4, 8, 12, 16, 20, 24, 28, 32):
            u = struct.unpack_from("<I", data, fo2 + o)[0]
            f = struct.unpack_from("<f", data, fo2 + o)[0]
            maybe = hex(u) if 0x400000 < u < 0x1200000 else "-"
            print(f"  {o:+d} u={u} f={f:.6g} ptr?={maybe}")
