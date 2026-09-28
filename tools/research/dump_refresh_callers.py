from pathlib import Path
import struct

data = Path(r"D:\Projects\IdeaProjects\Spider-Man 3 - The Game\Game.exe").read_bytes()
IMAGE = 0x400000
text = (0x1000, 0x642000)


def callers(tgt):
    out = []
    raw0, rsz = text
    for i in range(raw0, raw0 + rsz - 5):
        if data[i] == 0xE8:
            rel = struct.unpack_from("<i", data, i + 1)[0]
            if IMAGE + i + 5 + rel == tgt:
                out.append(IMAGE + i)
    return out


for tgt, name in [
    (0x648F10, "refreshDistFromChase"),
    (0x648A60, "lookaroundEye?"),
    (0x648920, "LookaroundTick"),
]:
    cs = callers(tgt)
    print(f"{name} {tgt:#x}: {len(cs)} callers")
    for c in cs[:20]:
        print(f"  {c:#x}")

# dump floats at known consts used in tick
for va in [0x00A6A3FC, 0x00A6A510, 0x00A5C0B4, 0x00A4B408, 0x00A6A48C, 0x00A53768]:
    fo = va - IMAGE
    if 0 <= fo < len(data) - 4:
        f = struct.unpack_from("<f", data, fo)[0]
        print(f"const {va:#x} = {f}")

# Disasm 0x65c5ea context - blend writer during gameplay
print("\n=== around 0x65c5d0 ===")
fo = 0x65C5D0 - IMAGE
b = data[fo : fo + 96]
for i in range(0, len(b), 16):
    print(f"  {0x65C5D0+i:#x}: {b[i:i+16].hex()}")

# 0x647890 blend writers
print("\n=== around 0x647890 ===")
fo = 0x647890 - IMAGE
b = data[fo : fo + 96]
for i in range(0, len(b), 16):
    print(f"  {0x647890+i:#x}: {b[i:i+16].hex()}")
