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


def va_to_file(va):
    rva = va - image_base
    for sva, vsz, raw, rsz in sections:
        if sva <= rva < sva + max(rsz, vsz):
            return raw + (rva - sva)
    return None


def file_to_va(fo):
    for va, vsz, raw, rsz in sections:
        if raw <= fo < raw + max(rsz, vsz):
            return image_base + va + (fo - raw)
    return None


needles = []
idx = 0
while True:
    i = data.find(b"GRAPHOPTS_", idx)
    if i < 0:
        break
    end = data.find(b"\x00", i)
    s = data[i:end]
    if any(k in s for k in (b"BLUR", b"BLOOM", b"DOF", b"GAUSS")):
        needles.append((file_to_va(i), s.decode()))
    idx = i + 1

for raws in [
    b"DISABLE_FULLSCREEN_BLUR\x00",
    b"RENDER_BLUR_TRAIL\x00",
    b"FULLSCREEN_BLUR\x00",
    b"MOTION_BLUR_ADD\x00",
    b"MOTION_BLUR_SUB\x00",
]:
    fo = data.find(raws)
    if fo >= 0:
        needles.append((file_to_va(fo), raws.decode().strip("\x00")))

seen = set()
for va, name in needles:
    if va in seen:
        continue
    seen.add(va)
    ptr = struct.pack("<I", va)
    start = 0
    while True:
        i = data.find(ptr, start)
        if i < 0:
            break
        r = file_to_va(i)
        name_p, val_p, typ = struct.unpack_from("<III", data, i)
        vfo = va_to_file(val_p) if 0x400000 < val_p < 0x1200000 else None
        val = "?"
        if vfo is not None:
            if typ == 1:
                val = "byte %d" % data[vfo]
            else:
                ui = struct.unpack_from("<I", data, vfo)[0]
                fl = struct.unpack_from("<f", data, vfo)[0]
                val = "i=%d f=%g" % (ui, fl)
        print("%-45s rec@%s type=%d val@%s %s" % (name, hex(r), typ, hex(val_p), val))
        start = i + 1
