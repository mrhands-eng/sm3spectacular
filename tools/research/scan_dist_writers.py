from pathlib import Path
import struct

data = Path(r"D:\Projects\IdeaProjects\Spider-Man 3 - The Game\Game.exe").read_bytes()
IMAGE = 0x400000

# Find movss/mov to [r+0x0C] in camera range that might be look dist writers
print("movss store [r+0x0C] in 0x640000-0x670000:")
for fo in range(0x240000, 0x270000):
    if data[fo : fo + 3] == b"\xf3\x0f\x11":
        modrm = data[fo + 3]
        if ((modrm >> 6) & 3) == 1 and fo + 5 <= len(data) and data[fo + 4] == 0x0C:
            print(f"  {IMAGE+fo:#x} {data[fo:fo+5].hex()}")
        if ((modrm >> 6) & 3) == 2 and data[fo + 4 : fo + 8] == b"\x0c\x00\x00\x00":
            print(f"  {IMAGE+fo:#x} {data[fo:fo+8].hex()}")

print("\nmovss store [r+0x28] distMin:")
for fo in range(0x240000, 0x270000):
    if data[fo : fo + 3] == b"\xf3\x0f\x11":
        modrm = data[fo + 3]
        if ((modrm >> 6) & 3) == 1 and fo + 5 <= len(data) and data[fo + 4] == 0x28:
            print(f"  {IMAGE+fo:#x}")
        if ((modrm >> 6) & 3) == 2 and data[fo + 4 : fo + 8] == b"\x28\x00\x00\x00":
            print(f"  {IMAGE+fo:#x}")

# fstp dword [esi+0x28] = D9 5E 28
print("\nfstp [r+0x28]:")
for fo in range(0x240000, 0x270000):
    if data[fo : fo + 3] == b"\xd9\x5e\x28" or data[fo : fo + 3] == b"\xd9\x5f\x28":
        print(f"  {IMAGE+fo:#x} {data[fo:fo+3].hex()}")
    if data[fo : fo + 6] == b"\xd9\x9e\x28\x00\x00\x00":
        print(f"  {IMAGE+fo:#x}")
