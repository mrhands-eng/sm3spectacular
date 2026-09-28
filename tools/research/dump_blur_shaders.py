from pathlib import Path

data = Path(r"D:\Projects\IdeaProjects\Spider-Man 3 - The Game\Game.exe").read_bytes()

def strings_near(key: bytes, radius=500):
    i = data.find(key)
    if i < 0:
        print(key, "missing")
        return
    ctab = data.rfind(b"CTAB", max(0, i - 200), i + 1)
    start = ctab if ctab >= 0 else max(0, i - 64)
    # find end token after key
    end = data.find((0xFF).to_bytes(1, "little") + (0xFF).to_bytes(1, "little"), i)
    # better: scan DWORD end 0x0000FFFF from CTAB
    region = data[start:start + 1200]
    out = []
    s = ""
    for c in region:
        if 32 <= c < 127:
            s += chr(c)
        else:
            if len(s) >= 4:
                out.append(s)
            s = ""
    if len(s) >= 4:
        out.append(s)
    print("===", key.decode(), "at", hex(i), "ctab", hex(ctab) if ctab >= 0 else None)
    print(out)

for k in [
    b"bloomDepthControl",
    b"blurlevel1offset",
    b"blurlevel2offset",
    b"gauss5x5_Y4pixelOffset",
    b"BlurKernel",
    b"lerpval",
]:
    strings_near(k)
