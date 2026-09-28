from pathlib import Path
import struct

data = Path(r"D:\Projects\IdeaProjects\Spider-Man 3 - The Game\Game.exe").read_bytes()
IMAGE = 0x400000


def dump(va, n=256):
    fo = va - IMAGE
    b = data[fo : fo + n]
    print(f"\n=== {va:#x} ({n} bytes) ===")
    for i in range(0, len(b), 16):
        print(f"  {va+i:#x}: {b[i:i+16].hex()}")


# LookaroundTick @ 0x648920
dump(0x648920, 0x200)

# Find function that refreshes dist from chase — was mentioned 0x648F10
dump(0x648F00, 0x80)

# Search within LookaroundTick for float loads from +0x0C, +0x28, +0x2C, +0x34
# and calls
fo = 0x648920 - IMAGE
chunk = data[fo : fo + 0x400]
print("\n--- calls / important mem in LookaroundTick+0x400 ---")
i = 0
while i < len(chunk) - 5:
    if chunk[i] == 0xE8:
        rel = struct.unpack_from("<i", chunk, i + 1)[0]
        tgt = 0x648920 + i + 5 + rel
        print(f"  call {0x648920+i:#x} -> {tgt:#x}")
        i += 5
        continue
    i += 1
