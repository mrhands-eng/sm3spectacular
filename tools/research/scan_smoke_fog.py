from pathlib import Path
import struct
import re

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


# Collect interesting printable strings
keys = (
    "smoke",
    "Smoke",
    "SMOKE",
    "fog",
    "Fog",
    "FOG",
    "haze",
    "dust",
    "Dust",
    "smog",
    "mist",
    "particle",
    "Particle",
    "emitter",
    "cloud",
    "Cloud",
    "atmosphere",
    "volumetric",
    "pollution",
    "vapor",
    "steam",
    "fx_smoke",
    "cityfog",
    "air_fog",
)

found = {}
for m in re.finditer(rb"[\x20-\x7e]{4,80}", data):
    s = m.group().decode("ascii")
    sl = s.lower()
    if not any(k.lower() in sl for k in keys):
        continue
    # skip junk
    if any(x in sl for x in ("fmod", "credit", "soft landing", "software", "microsoft")):
        continue
    if s not in found:
        found[s] = file_to_va(m.start())

for s in sorted(found, key=lambda x: x.lower()):
    print(hex(found[s]), s)

print("\n--- GRAPHOPTS fog/smoke/particle ---")
idx = 0
while True:
    i = data.find(b"GRAPHOPTS_", idx)
    if i < 0:
        break
    end = data.find(b"\x00", i)
    s = data[i:end].decode(errors="replace")
    su = s.upper()
    if any(k in su for k in ("FOG", "SMOKE", "DUST", "PARTICLE", "CLOUD", "HAZE", "ATMOS", "FX_")):
        print(hex(file_to_va(i)), s)
    idx = i + 1
