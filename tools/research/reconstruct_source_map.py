#!/usr/bin/env python3
"""
Reconstruct a browsable 'source map' of Spider-Man 3 PC from Game.exe.

We do NOT have original C++ sources. This recovers the closest usable stand-in:
  - MSVC RTTI class tree (2769+ types)
  - script/SLF method names
  - GRAPHOPTS / systems
  - embedded shader CTAB symbols
  - FMOD path leftovers (only third-party paths survived in the binary)

Output: <dump>/source_map/
"""
from __future__ import annotations

import json
import re
import struct
from collections import defaultdict
from pathlib import Path

GAME_EXE = Path(r"D:\Projects\IdeaProjects\Spider-Man 3 - The Game\Game.exe")
OUT = Path(r"D:\Projects\IdeaProjects\sm3-game-dump\source_map")
IMAGE_BASE = 0x400000


def pe_sections(data: bytes):
    e = struct.unpack_from("<I", data, 0x3C)[0]
    coff = e + 4
    nsec = struct.unpack_from("<H", data, coff + 2)[0]
    opt = coff + 20
    sec_off = opt + struct.unpack_from("<H", data, coff + 16)[0]
    secs = []
    for i in range(nsec):
        off = sec_off + i * 40
        name = data[off : off + 8].split(b"\0")[0].decode("ascii", "replace")
        vsz, va, rsz, raw = struct.unpack_from("<IIII", data, off + 8)
        secs.append((name, va, vsz, raw, rsz))
    return secs


def file_to_va(secs, fo: int) -> int | None:
    for name, va, vsz, raw, rsz in secs:
        if raw <= fo < raw + max(rsz, vsz):
            return IMAGE_BASE + va + (fo - raw)
    return None


def demangle_msvc_rtti(raw: str) -> str:
    """Best-effort readable name from .?AV...@@ / .?AU...@@ strings."""
    s = raw
    if s.startswith(".?AV"):
        s = s[4:]
    elif s.startswith(".?AU"):
        s = s[4:]
    elif s.startswith(".?AV?"):
        s = s[4:]
    # strip trailing @@
    while s.endswith("@"):
        s = s[:-1]
    # very rough template / nested class cleanup
    s = s.replace("@@", "::")
    s = re.sub(r"\?\$", "", s)
    s = s.replace("@", "::")
    s = re.sub(r"::+$", "", s)
    return s


def classify_bucket(name: str) -> str:
    n = name.lower()
    rules = [
        ("render/fog_particles", ("fog", "particle", "aps2", "smoke", "cloud", "bloom", "blur")),
        ("render/shaders", ("shader", "sm3shader", "ps_", "vs_", "material", "ngl")),
        ("render/lod_buildings", ("lod", "building", "citylod", "district", "roadlod")),
        ("render/postfx", ("bloom", "blur", "dof", "exposure", "adjustment", "tonemap")),
        ("render/core", ("render", "mesh", "texture", "scene", "camera", "light", "shadow")),
        ("ai", ("ai_", "base_ai", "state_graph", "path", "nav")),
        ("gameplay/hero", ("spidey", "hero", "web", "swing", "combat")),
        ("gameplay/mission", ("mission", "quest", "objective", "checkpoint")),
        ("ui", ("igo", "ui_", "menu", "hud", "widget", "screen")),
        ("audio", ("fmod", "sound", "audio", "sfx")),
        ("physics", ("phys", "collision", "convex", "rigid")),
        ("script", ("slf", "script", " Mash", "sin_")),
        ("online", ("soap", "online", "profile", "storage")),
        ("system", ("singleton", "allocator", "memory", "stream", "file", "resource")),
    ]
    for bucket, keys in rules:
        if any(k in n for k in keys):
            return bucket
    return "misc"


def main():
    data = GAME_EXE.read_bytes()
    secs = pe_sections(data)
    OUT.mkdir(parents=True, exist_ok=True)

    # --- RTTI classes ---
    rtti_raw = []
    for m in re.finditer(rb"\.\?A[VU][^\x00]{2,200}", data):
        raw = m.group().decode("ascii", "replace")
        va = file_to_va(secs, m.start()) or 0
        nice = demangle_msvc_rtti(raw)
        rtti_raw.append({"va": va, "raw": raw, "name": nice, "bucket": classify_bucket(nice)})

    # unique by name
    by_name = {}
    for e in rtti_raw:
        by_name.setdefault(e["name"], e)
    classes = sorted(by_name.values(), key=lambda x: x["name"].lower())
    (OUT / "rtti_classes.json").write_text(json.dumps(classes, indent=2), encoding="utf-8")
    with (OUT / "rtti_classes.txt").open("w", encoding="utf-8") as f:
        for e in classes:
            f.write(f"{e['va']:08X}\t{e['bucket']:28}\t{e['name']}\n")

    # bucket folders with class lists (pseudo source tree)
    tree = defaultdict(list)
    for e in classes:
        tree[e["bucket"]].append(e["name"])
    tree_root = OUT / "pseudo_src"
    if tree_root.exists():
        import shutil

        shutil.rmtree(tree_root)
    for bucket, names in tree.items():
        d = tree_root / bucket
        d.mkdir(parents=True, exist_ok=True)
        (d / "CLASSES.md").write_text(
            "# " + bucket + "\n\n" + "\n".join(f"- `{n}`" for n in sorted(names)) + "\n",
            encoding="utf-8",
        )
    print(f"RTTI classes: {len(classes)} -> buckets {len(tree)}")

    # --- SLF / script method names ---
    slf = sorted(
        {
            m.group().decode("ascii", "replace")
            for m in re.finditer(rb"slf__[a-z0-9_]+__[a-z0-9_]+", data, re.I)
        }
    )
    (OUT / "slf_methods.txt").write_text("\n".join(slf) + "\n", encoding="utf-8")
    print(f"SLF methods: {len(slf)}")

    # --- resource type registry strings ---
    res_types = []
    for m in re.finditer(
        rb"(?:SMBUILDINGLODPARAM|CITYLODS|CLOUDTWEAK|GRAPHICSOPTIONS|ENVIRONMENT|"
        rb"DAYSKYCOLOR|NIGHTSKYCOLOR|JSON_RENDERLOD|PARTICLE|EFFECTCONTAINER|"
        rb"SMROADLODPARAM|ALLBUILDINGS|OUTDOORS|MESH|TEX|MAT)[A-Z0-9_]*",
        data,
    ):
        s = m.group().decode("ascii", "replace")
        res_types.append((file_to_va(secs, m.start()) or 0, s))
    res_types = sorted(set(res_types), key=lambda x: x[1])
    (OUT / "resource_type_strings.txt").write_text(
        "\n".join(f"{va:08X}\t{s}" for va, s in res_types) + "\n", encoding="utf-8"
    )

    # --- interesting systems for blur/haze/lod ---
    focus_keys = (
        "fog",
        "Fog",
        "particle",
        "Particle",
        "bloom",
        "Bloom",
        "blur",
        "Blur",
        "LOD",
        "Lod",
        "building",
        "Building",
        "cloud",
        "Cloud",
        "depthfog",
        "CityDetail",
        "Exposure",
        "sky",
        "Sky",
    )
    focus = [e for e in classes if any(k in e["name"] for k in focus_keys)]
    (OUT / "FOCUS_fog_lod_blur_classes.txt").write_text(
        "\n".join(f"{e['va']:08X}\t{e['bucket']}\t{e['name']}" for e in focus) + "\n",
        encoding="utf-8",
    )
    print(f"Focus fog/lod/blur classes: {len(focus)}")

    # --- cpp path leftovers ---
    cpp = sorted(
        {
            m.group().decode("ascii", "replace")
            for m in re.finditer(rb"[\w./\\-]{3,160}\.cpp", data)
        }
    )
    (OUT / "embedded_cpp_paths.txt").write_text("\n".join(cpp) + "\n", encoding="utf-8")

    readme = f"""# Source map (reconstructed — NOT original sources)

Spider-Man 3 PC shipped **without** source code. Nobody outside the original studios
has the C++ tree. What we *can* recover from `Game.exe` is still extremely useful:

| Artifact | Count | File |
|----------|------:|------|
| MSVC RTTI classes | {len(classes)} | `rtti_classes.txt` / `pseudo_src/**/CLASSES.md` |
| SLF script methods | {len(slf)} | `slf_methods.txt` |
| Fog/LOD/blur-related classes | {len(focus)} | `FOCUS_fog_lod_blur_classes.txt` |
| Embedded `.cpp` paths | {len(cpp)} | mostly FMOD only |

## How to use this as 'source'

1. Open `pseudo_src/render/fog_particles/CLASSES.md` — particle/fog systems.
2. Open `FOCUS_fog_lod_blur_classes.txt` — shortlist for distant haze/soap.
3. Cross-ref class names with `../exe/graphopts.txt` and `../packs_index/`.
4. For behavior detail, decompile the VA of a class's vtable/methods in Ghidra/IDA
   (image base `0x00400000`, no ASLR).

## Limits

- No function bodies / original comments / headers.
- Template demangling is approximate.
- Game logic for buildings/fog lives in stripped code + resource blobs
  (`SMBUILDINGLODPARAM`, `CITYLODS`, `CLOUDTWEAK`, …) under `../packs_extracted/`.

## Next tooling (optional)

Import `Game.exe` into Ghidra with this map as a bookmark list of class names.
"""
    (OUT / "00_README.md").write_text(readme, encoding="utf-8")
    print(f"Wrote {OUT}")


if __name__ == "__main__":
    main()
