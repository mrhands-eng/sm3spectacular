from pathlib import Path
import struct

data = Path(r"D:\Projects\IdeaProjects\Spider-Man 3 - The Game\Game.exe").read_bytes()
image_base = 0x400000
sections = []
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
coff = e_lfanew + 4
nsec = struct.unpack_from("<H", data, coff + 2)[0]
opt = coff + 20
sec_off = opt + struct.unpack_from("<H", data, coff + 16)[0]
for i in range(nsec):
    off = sec_off + i * 40
    name = data[off : off + 8].split(b"\0", 1)[0].decode()
    vsz, va, rsz, raw = struct.unpack_from("<IIII", data, off + 8)
    sections.append((va, vsz, raw, rsz))


def va_to_file(va: int):
    rva = va - image_base
    for sva, vsz, raw, rsz in sections:
        if sva <= rva < sva + max(rsz, vsz):
            return raw + (rva - sva)
    return None


def read_cstr(va: int):
    fo = va_to_file(va)
    if fo is None:
        return None
    end = data.find(b"\0", fo)
    return data[fo:end].decode(errors="replace")


# Dump graphopts entries around building LOD (ptr table at ~0xD0E200)
base = 0xD0E1E0
print("Scanning graphopts-like records near building LOD...")
for va in range(base, base + 0x80, 4):
    fo = va_to_file(va)
    val = struct.unpack_from("<I", data, fo)[0]
    s = read_cstr(val) if 0x400000 < val < 0xE00000 else None
    mark = f' -> "{s}"' if s and s.startswith("GRAPH") or (s and "LOD" in s) else ""
    if s and ("GRAPH" in s or "LOD" in s or "BUILDING" in s or "DISTRICT" in s or "RENDER" in s):
        print(f"  [{hex(va)}] {hex(val)}{mark}")

# Heuristic: many engines store {const char* name; void* value; int type}
# Look at 16-byte or 12-byte stride records containing the name ptr
name_va = 0xA56B3C  # HIGH_LOD_DISTANCE
name_ptr_loc = 0xD0E218
print("\nBytes around HIGH_LOD_DISTANCE name ptr @", hex(name_ptr_loc))
fo = va_to_file(name_ptr_loc - 32)
chunk = data[fo : fo + 96]
for i in range(0, 96, 16):
    row = chunk[i : i + 16]
    ints = struct.unpack("<IIII", row)
    floats = struct.unpack("<ffff", row)
    print(f"  {hex(name_ptr_loc - 32 + i)}: {[hex(x) for x in ints]}  f={floats}")

# Also dump RENDER_LOWLODS neighborhood
print("\nRENDER_LOWLODS ptr @ 0xD0CF98")
fo = va_to_file(0xD0CF98 - 32)
chunk = data[fo : fo + 96]
for i in range(0, 96, 16):
    row = chunk[i : i + 16]
    ints = struct.unpack("<IIII", row)
    print(f"  {hex(0xD0CF98 - 32 + i)}: {[hex(x) for x in ints]}")
