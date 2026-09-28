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


idx = 0
print("=== GRAPHOPTS *SHADOW* ===")
while True:
    i = data.find(b"GRAPHOPTS_", idx)
    if i < 0:
        break
    end = data.find(b"\x00", i)
    s = data[i:end].decode(errors="replace")
    if "SHADOW" in s.upper():
        va = file_to_va(i)
        print(hex(va), s)
        ptr = struct.pack("<I", va)
        start = 0
        while True:
            j = data.find(ptr, start)
            if j < 0:
                break
            name_p, val_p, typ = struct.unpack_from("<III", data, j)
            if name_p == va and 0x400000 < val_p < 0x1200000:
                vfo = va_to_file(val_p)
                if typ == 1:
                    val = "byte %d" % data[vfo]
                elif typ == 2:
                    val = "int %d" % struct.unpack_from("<I", data, vfo)[0]
                elif typ == 3:
                    val = "float %g" % struct.unpack_from("<f", data, vfo)[0]
                else:
                    val = "?"
                print("  rec", hex(file_to_va(j)), "type", typ, "val@", hex(val_p), val)
            start = j + 1
    idx = i + 1

print("\n=== other shadow strings ===")
for n in [
    b"RENDER_SHADOWS\x00",
    b"ALLOW_SHADOWS\x00",
    b"NO_SHADOWS\x00",
    b"SHADOW_MAP\x00",
    b"ShadowMap\x00",
    b"shadow_bias\x00",
    b"ShadowBias\x00",
    b"SHADOW_DISTANCE\x00",
    b"SHADOW_RANGE\x00",
    b"SHADOW_RES\x00",
    b"SHADOW_SIZE\x00",
    b"CASCADE\x00",
    b"building_shadow\x00",
    b"BUILDINGS_SHADOW\x00",
    b"GRAPHOPTS_BUILDINGS_SHADOW_SCALE\x00",
]:
    fo = data.find(n)
    if fo < 0:
        continue
    va = file_to_va(fo)
    print(hex(va), n.decode().strip("\x00"))

# RTTI-ish
print("\n=== rtti/class-like with shadow ===")
for m in __import__("re").finditer(rb"[\x20-\x7e]*[Ss]hadow[\x20-\x7e]{0,40}", data):
    s = m.group().decode("ascii", "replace")
    if len(s) < 6 or "Microsoft" in s:
        continue
    if s.startswith(".?AV") or "SHADOW" in s or "shadow" in s.lower():
        if any(k in s for k in ("SHADOW", "Shadow", "shadow")):
            if "credit" in s.lower():
                continue
            print(hex(file_to_va(m.start())), s[:100])
