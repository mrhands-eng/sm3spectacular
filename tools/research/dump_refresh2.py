from pathlib import Path
import struct

data = Path(r"D:\Projects\IdeaProjects\Spider-Man 3 - The Game\Game.exe").read_bytes()
IMAGE = 0x400000


def dump(va, n=128):
    fo = va - IMAGE
    b = data[fo : fo + n]
    print(f"\n=== {va:#x} ===")
    for i in range(0, len(b), 16):
        print(f"  {va+i:#x}: {b[i:i+16].hex()}")


dump(0x6585C0, 0x80)
dump(0x648F10, 0xA0)

# finish 648F10 - reads chase dist via vfuncs into look+0x28/+0x2C
# Also find FOV-like: stores of values near 0.5-1.2 radians or 40-90 degrees as float
# Common: movss to [cam+disp] of FOV

# Check 0x66669c scale writer context  
dump(0x666680, 0x60)

# Object at chase+0x64 - any reads in lookaround
print("\n--- [r+0x64] movss in 0x648000-0x64A000 ---")
for fo in range(0x248000, 0x24A000):
    if data[fo : fo + 3] == b"\xf3\x0f\x10" and fo + 5 < len(data):  # movss load
        modrm = data[fo + 3]
        if ((modrm >> 6) & 3) == 1 and data[fo + 4] == 0x64:
            print(f"  load [r+0x64] {IMAGE+fo:#x}")
        if ((modrm >> 6) & 3) == 2 and data[fo + 4 : fo + 8] == b"\x64\x00\x00\x00":
            print(f"  load [r+0x64]d {IMAGE+fo:#x}")
    if data[fo : fo + 3] == b"\xf3\x0f\x11" and fo + 5 < len(data):
        modrm = data[fo + 3]
        if ((modrm >> 6) & 3) == 1 and data[fo + 4] == 0x64:
            print(f"  store [r+0x64] {IMAGE+fo:#x}")
