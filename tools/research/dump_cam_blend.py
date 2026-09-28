from pathlib import Path
import struct

data = Path(r"D:\Projects\IdeaProjects\Spider-Man 3 - The Game\Game.exe").read_bytes()
IMAGE = 0x400000


def dump(va, n=256):
    fo = va - IMAGE
    b = data[fo : fo + n]
    print(f"\n=== {va:#x} ===")
    for i in range(0, len(b), 16):
        print(f"  {va+i:#x}: {b[i:i+16].hex()}")


# Continue LookaroundTick from 0x648b10 — writes into cam manager?
dump(0x648B10, 0x200)

# SetChaseTarget full — what blend/desired it sets
dump(0x64C860, 0x60)

# Search writers to cam+0x404 (blendA): 89 xx 04 04 00 00 or f3 0f 11 xx 04 04 00 00
print("\n--- stores to +0x404 in cam range ---")
for fo in range(0x240000, 0x270000):
    if data[fo : fo + 3] == b"\xf3\x0f\x11" and fo + 8 < len(data):
        modrm = data[fo + 3]
        if ((modrm >> 6) & 3) == 2 and data[fo + 4 : fo + 8] == b"\x04\x04\x00\x00":
            print(f"  movss [r+0x404] @ {IMAGE+fo:#x} {data[fo:fo+8].hex()}")
    if data[fo] == 0xC7 and fo + 10 < len(data):
        modrm = data[fo + 1]
        if ((modrm >> 6) & 3) == 2 and data[fo + 2 : fo + 6] == b"\x04\x04\x00\x00":
            print(f"  mov [r+0x404],imm @ {IMAGE+fo:#x} {data[fo:fo+10].hex()}")

print("\n--- stores to +0x3FC (scale) ---")
for fo in range(0x240000, 0x270000):
    if data[fo : fo + 3] == b"\xf3\x0f\x11" and fo + 8 < len(data):
        modrm = data[fo + 3]
        if ((modrm >> 6) & 3) == 2 and data[fo + 4 : fo + 8] == b"\xfc\x03\x00\x00":
            print(f"  movss [r+0x3FC] @ {IMAGE+fo:#x}")
