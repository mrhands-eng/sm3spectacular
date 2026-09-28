#!/usr/bin/env python3
"""Search camera-related strings and swing/web recenter patterns."""
import struct
from pathlib import Path

data = Path(r"D:\Projects\IdeaProjects\Spider-Man 3 - The Game\Game.exe").read_bytes()
img = 0x400000
e = struct.unpack_from("<I", data, 0x3C)[0]
ns = struct.unpack_from("<H", data, e + 6)[0]
osz = struct.unpack_from("<H", data, e + 20)[0]
sec = e + 24 + osz
secs = []
for i in range(ns):
    o = sec + i * 40
    name = data[o : o + 8].rstrip(b"\0")
    vsize, va, rsize, rptr = struct.unpack_from("<IIII", data, o + 8)
    secs.append((name, va, vsize, rptr, rsize))


def f2v(f):
    for n, va, vs, rp, rs in secs:
        if rp <= f < rp + max(rs, 1):
            return img + va + (f - rp)
    return None


# Dump all strings containing CAMERA / LOOK / SWING_CAM / FOLLOW / ORBIT / YAW
import re

text_hits = []
for m in re.finditer(rb"[A-Za-z0-9_]{0,20}(CAMERA|camera|LOOKAT|look_at|FREECAM|freelook|RECENTER|AUTOCORRECT|follow_cam|FOLLOW_CAM|swing_cam|SWING_CAM|cam_yaw|CAM_YAW|ORBIT)[A-Za-z0-9_]{0,40}", data):
    s = m.group().decode("ascii", "replace")
    if len(s) < 6:
        continue
    text_hits.append((f2v(m.start()), s))

# unique keep interesting
seen = set()
for va, s in text_hits:
    key = s.upper()
    if key in seen:
        continue
    seen.add(key)
    if any(
        k in key
        for k in (
            "RECENTER",
            "AUTOCORRECT",
            "FREELOOK",
            "FOLLOW",
            "ORBIT",
            "SWING",
            "YAW",
            "ALIGN",
            "SNAP",
            "BEHIND",
            "LOCK",
            "MANUAL",
            "LOOK",
        )
    ):
        print(f"0x{va:08X} {s}")

print("\n=== GT_OPTIONS_CAMERA* ===")
for m in re.finditer(rb"GT_OPTIONS_CAMERA[A-Z0-9_]*", data):
    print(f"0x{f2v(m.start()):08X} {m.group().decode()}")

print("\n=== spiderman_camera* ===")
for m in re.finditer(rb"spiderman_camera[a-z0-9_]*", data):
    print(f"0x{f2v(m.start()):08X} {m.group().decode()}")
