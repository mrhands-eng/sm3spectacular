from pathlib import Path
import struct

data = Path(r"D:\Projects\IdeaProjects\Spider-Man 3 - The Game\Game.exe").read_bytes()
e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
opt = e_lfanew + 24
opt_size = struct.unpack_from("<H", data, e_lfanew + 20)[0]
image_base = struct.unpack_from("<I", data, opt + 28)[0]
sec_off = opt + opt_size
numsec = struct.unpack_from("<H", data, e_lfanew + 6)[0]
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


def dump(va, n=64):
    fo = va_to_file(va)
    b = data[fo : fo + n]
    print(f"\n=== {va:#x} ===")
    for i in range(0, len(b), 16):
        print(f"  {va+i:#x}: {b[i:i+16].hex()}")


# Find function starts (CC CC or typical prologues) before each write
writes = [
    0x640C05, 0x64BF8A, 0x64BF9C, 0x653375, 0x65ACFE, 0x65C23B, 0x65C3FF,
    0x660C8B, 0x660F59, 0x660FB8, 0x66101A, 0x661043, 0x6610EC, 0x661181,
    0x661215, 0x66130B, 0x66132E, 0x661417, 0x664D56, 0x666574, 0x6665A0,
    0x66A4DC,
]

def find_func_start(va, back=0x200):
    fo = va_to_file(va)
    for i in range(fo, max(fo - back, 0), -1):
        # int3 padding then common prologue
        if data[i] in (0x55, 0x56, 0x53, 0x51, 0x57) and i > 0 and data[i - 1] == 0xCC:
            return file_to_va(i)
        if data[i : i + 3] == b"\x55\x8B\xEC":
            return file_to_va(i)
        if data[i : i + 3] == b"\x56\x8B\xF1":
            return file_to_va(i)
        if data[i : i + 3] == b"\x53\x8B\xD9":
            return file_to_va(i)
    return None


for w in writes:
    fs = find_func_start(w)
    print(f"write {w:#x} -> func ~ {fs:#x}" if fs else f"write {w:#x} -> ?")

# Disasm-ish: dump around 0x64BF70 (near SetChaseTarget) and 0x660C00 cluster and 0x666550
for va in [0x64BF50, 0x64C860, 0x660C50, 0x660F20, 0x666540, 0x666C00, 0x66A4B0]:
    dump(va, 96)

# Look for calls to a likely SetPending helper: function that only does mov [ecx+270], arg
# Check 0x640BE0 area after ApplyMode - maybe RequestMode sibling
dump(0x640B90, 128)
dump(0x640C00, 64)

# Count callers of each write's function - focus on funcs with many writes (mode request hub)
from collections import Counter
func_counts = Counter()
for w in writes:
    fs = find_func_start(w)
    if fs:
        func_counts[fs] += 1
print("\nfuncs by write count:")
for fs, c in func_counts.most_common():
    print(f"  {fs:#x}: {c} writes")
