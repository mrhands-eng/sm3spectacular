#!/usr/bin/env python3
"""Find camera recenter string xrefs and related code in SM3 game.exe."""
import struct
from pathlib import Path

path = Path(r"D:\Projects\IdeaProjects\Spider-Man 3 - The Game\Game.exe")
data = path.read_bytes()

e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
coff = e_lfanew + 4
num_sections = struct.unpack_from("<H", data, coff + 2)[0]
opt_size = struct.unpack_from("<H", data, coff + 16)[0]
opt = coff + 20
image_base = struct.unpack_from("<I", data, opt + 28)[0]
sec_off = opt + opt_size
sections = []
for i in range(num_sections):
    off = sec_off + i * 40
    name = data[off : off + 8].rstrip(b"\0").decode("ascii", "replace")
    vsize, va, rawsize, rawptr = struct.unpack_from("<IIII", data, off + 8)
    sections.append((name, va, vsize, rawptr, rawsize))
    print(f"  {name:8s} VA=0x{va:08X} VSize=0x{vsize:X} Raw=0x{rawptr:X}")
print(f"image_base=0x{image_base:08X}")


def file_to_va(foff):
    for name, va, vsize, rawptr, rawsize in sections:
        if rawptr <= foff < rawptr + max(rawsize, 1):
            return image_base + va + (foff - rawptr)
    return None


def va_to_file(va):
    rva = va - image_base
    for name, sva, vsize, rawptr, rawsize in sections:
        if sva <= rva < sva + max(vsize, rawsize):
            return rawptr + (rva - sva)
    return None


needles = [
    b"ENABLE_PLR_CMD_RECENTER_CAMERA",
    b"GT_OPTIONS_CAMERA_RECENTER",
    b"ENABLE_PLR_CMD_WEB_SHOT",
    b"ENGAGE_FREELOOK",
    b"FREELOOK",
    b"WEB_SHOT",
    b"spiderman_camera_autocorrect",
    b"RECENTER_CAMERA",
]
vas = {}
for n in needles:
    i = data.find(n)
    if i < 0:
        print("missing", n)
        continue
    va = file_to_va(i)
    vas[n] = va
    print(f"{n.decode():40s} file=0x{i:X} VA=0x{va:08X}")


def find_imm_xrefs(target_va, label):
    imm = struct.pack("<I", target_va)
    hits = []
    start = 0
    while True:
        i = data.find(imm, start)
        if i < 0:
            break
        va = file_to_va(i)
        prev = data[i - 1] if i > 0 else 0
        kind = "?"
        insn_va = va
        if prev == 0x68:
            kind = "push"
            insn_va = va - 1
        elif 0xB8 <= prev <= 0xBF:
            regs = ["eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"]
            kind = f"mov {regs[prev - 0xB8]}"
            insn_va = va - 1
        hits.append((i, va, kind, insn_va, prev))
        start = i + 1
    print(f"--- xrefs to {label} (0x{target_va:08X}): {len(hits)} ---")
    for h in hits[:50]:
        print(
            f"  file=0x{h[0]:X} va=0x{h[1]:08X} kind={h[2]} "
            f"insn_va=0x{h[3]:08X} prev=0x{h[4]:02X}"
        )
    return hits


for key, va in vas.items():
    find_imm_xrefs(va, key.decode())

# Also look near ENABLE_PLR_CMD table — dump surrounding string table entries
print("\n=== nearby ENABLE_PLR_CMD strings ===")
base = data.find(b"ENABLE_PLR_CMD_RECENTER_CAMERA")
chunk = data[base - 0x400 : base + 0x200]
# find null-terminated ASCII strings
i = 0
while i < len(chunk):
    if 32 <= chunk[i] < 127:
        j = i
        while j < len(chunk) and 32 <= chunk[j] < 127:
            j += 1
        s = chunk[i:j]
        if len(s) >= 6 and b"PLR" in s or b"CAMERA" in s or b"WEB" in s or b"LOOK" in s:
            foff = base - 0x400 + i
            print(f"  0x{file_to_va(foff):08X} {s.decode()}")
        i = j + 1
    else:
        i += 1
