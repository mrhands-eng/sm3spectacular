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


# hunt haze / weather / sky / atmosphere graphopts and strings
hits = []
idx = 0
while True:
    i = data.find(b"GRAPHOPTS_", idx)
    if i < 0:
        break
    end = data.find(b"\x00", i)
    s = data[i:end].decode(errors="replace")
    su = s.upper()
    if any(k in su for k in ("FOG", "HAZE", "SMOKE", "WEATHER", "SKY", "AIR", "ATMOS", "CLOUD", "DUST", "SMOG", "MIST", "BLOOM", "EXPOSURE", "SUN")):
        hits.append((file_to_va(i), s))
    idx = i + 1

for va, s in hits:
    print(hex(va), s)

print("--- weather/haze strings ---")
for needle in [b"haze", b"Haze", b"weather", b"Weather", b"sky_fog", b"air_density", b"smog", b"pollution", b"atmosphere", b"volumetric", b"sun_glare", b"lens_flare", b"godray", b"skybox"]:
    fo = data.find(needle)
    if fo < 0:
        continue
    end = data.find(b"\x00", fo)
    print(hex(file_to_va(fo)), data[max(0,fo-20):end+1])
