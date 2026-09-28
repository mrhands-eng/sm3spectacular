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


def file_to_va(foff):
    for name, sva, vsz, raw, rsz in sections:
        if raw <= foff < raw + max(rsz, 1):
            return image_base + sva + (foff - raw)
    return None


text = next(s for s in sections if s[0] == ".text")
raw0, rsz = text[3], text[4]
targets = {
    0x640BF0: "RequestModeBySlot",
    0x64BF60: "SetPendingWeird",
    0x640B10: "ApplyMode",
}
for tgt, name in targets.items():
    callers = []
    for i in range(raw0, raw0 + rsz - 5):
        if data[i] == 0xE8:
            rel = struct.unpack_from("<i", data, i + 1)[0]
            if file_to_va(i) + 5 + rel == tgt:
                callers.append(file_to_va(i))
    print(f"{name} {tgt:#x}: {len(callers)} callers")
    for c in callers[:40]:
        print(f"  {c:#x}")
