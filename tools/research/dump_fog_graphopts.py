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


names = [
    b"GRAPHOPTS_FOG_VOLUMETRIC\x00",
    b"GRAPHOPTS_FOG_DEPTH\x00",
    b"GRAPHOPTS_PARTICLE_SCALE\x00",
    b"NO_PARTICLES\x00",
    b"MULTITHREADED_CLOUDS\x00",
    b"DUMP_PARTICLE_INSTANCES\x00",
    b"PARTICLE_FOG_CTL\x00",
    b"depthfog_control\x00",
    b"depthfog_color\x00",
    b"fogs_per_meter\x00",
    b"fog_color\x00",
    b"FogValues\x00",
    b"FogColor\x00",
]

for s in names:
    fo = data.find(s)
    if fo < 0:
        print(s, "NOT FOUND")
        continue
    va = file_to_va(fo)
    print("===", s.decode().strip("\x00"), hex(va))
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
        print("  rec@%s type=%d val@%s %s" % (hex(r), typ, hex(val_p), val))
        start = i + 1

# Also search RENDER_ / ALLOW_ / ENABLE_ with FOG
idx = 0
print("\n--- other fog/particle flags ---")
while True:
    i = data.find(b"\x00", idx)
    # scan for FOG in name-like strings in rdata
    idx = i + 1
    if i < 0 or idx > len(data):
        break
# simpler:
for m in [
    b"ALLOW_FOG",
    b"ENABLE_FOG",
    b"RENDER_FOG",
    b"FOG_ENABLE",
    b"USE_FOG",
    b"VOLUMETRIC_FOG",
    b"CITY_FOG",
    b"SMOKE",
    b"smoke_",
]:
    fo = data.find(m)
    if fo >= 0:
        end = data.find(b"\x00", fo)
        print(hex(file_to_va(fo)), data[fo:end].decode(errors="replace"))
