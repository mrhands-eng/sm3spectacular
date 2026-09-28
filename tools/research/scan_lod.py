from pathlib import Path

data = Path(r"D:\Projects\IdeaProjects\Spider-Man 3 - The Game\Game.exe").read_bytes()

keys = [
    b"GRAPHOPTS_BUILDINGS_HIGH_LOD_DISTANCE",
    b"GRAPHOPTS_BUILDINGS_HIGH_LOD_ENABLED",
    b"GRAPHOPTS_BUILDINGS_MEDIUM_LOD_DISTANCE",
    b"SMBUILDINGLODPARAM",
    b"RENDER_LOWLODS",
    b"DISTRICT_LOD_CULLING",
    b"DISTRICT_LOD_CULLING_HEIGHT_SCALE",
    b"CITYLODS",
    b"InitLods",
    b"sm_buildinglod",
    b"sm_citylod",
    b"SMROADLODPARAM",
]

for k in keys:
    i = data.find(k)
    mark = hex(i) if i >= 0 else "-"
    print(f"{k.decode():45} {mark}")
    if i < 0:
        continue
    region = data[max(0, i - 100) : i + len(k) + 160]
    s = "".join(chr(c) if 32 <= c < 127 else "." for c in region)
    print(" ", s[:220])
    print()
