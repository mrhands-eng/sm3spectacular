#!/usr/bin/env python3
"""
Build a searchable dump of Spider-Man 3 PC for reverse-engineering.

Output root (default):
  D:\\Projects\\IdeaProjects\\sm3-game-dump\\

Layout:
  00_README.md
  exe/                 Game.exe string + graphopt + shader indexes
  toc/                 amalga.toc parse
  packs_extracted/     sm3ext output (one folder per PCPACK)
  packs_index/         flat indexes (all asset names, fog/lod hits, …)
  pcinterface/         copy of UI/bnx etc.
  sound/               movies/sound listing (not full copy — too large)
"""
from __future__ import annotations

import json
import re
import shutil
import struct
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

GAME = Path(r"D:\Projects\IdeaProjects\Spider-Man 3 - The Game")
DUMP = Path(r"D:\Projects\IdeaProjects\sm3-game-dump")
SM3EXT = Path(r"D:\Projects\IdeaProjects\sm3ext\build32\sm3ext.exe")
IMAGE_BASE = 0x400000


def pe_sections(data: bytes):
    e = struct.unpack_from("<I", data, 0x3C)[0]
    coff = e + 4
    nsec = struct.unpack_from("<H", data, coff + 2)[0]
    opt = coff + 20
    sec_off = opt + struct.unpack_from("<H", data, coff + 16)[0]
    sections = []
    for i in range(nsec):
        off = sec_off + i * 40
        name = data[off : off + 8].split(b"\0")[0].decode("ascii", "replace")
        vsz, va, rsz, raw = struct.unpack_from("<IIII", data, off + 8)
        sections.append((name, va, vsz, raw, rsz))
    return sections


def file_to_va(sections, fo: int) -> int | None:
    for name, va, vsz, raw, rsz in sections:
        if raw <= fo < raw + max(rsz, vsz):
            return IMAGE_BASE + va + (fo - raw)
    return None


def va_to_file(sections, va: int) -> int | None:
    rva = va - IMAGE_BASE
    for name, sva, vsz, raw, rsz in sections:
        if sva <= rva < sva + max(rsz, vsz):
            return raw + (rva - sva)
    return None


def dump_exe(out: Path):
    exe = GAME / "Game.exe"
    data = exe.read_bytes()
    sections = pe_sections(data)
    out.mkdir(parents=True, exist_ok=True)
    (out / "Game.exe.size.txt").write_text(f"{len(data)} bytes\n", encoding="utf-8")

    # All printable strings >= 5
    strings = []
    for m in re.finditer(rb"[\x20-\x7e]{5,200}", data):
        s = m.group().decode("ascii")
        va = file_to_va(sections, m.start())
        strings.append((va or 0, s))
    strings.sort(key=lambda x: x[0])
    with (out / "strings_all.txt").open("w", encoding="utf-8", errors="replace") as f:
        for va, s in strings:
            f.write(f"{va:08X}\t{s}\n")
    print(f"  strings: {len(strings)}")

    # Keyword indexes
    keywords = (
        "fog",
        "Fog",
        "FOG",
        "smoke",
        "Smoke",
        "blur",
        "Blur",
        "BLOOM",
        "bloom",
        "LOD",
        "lod",
        "mip",
        "Mip",
        "particle",
        "Particle",
        "cloud",
        "Cloud",
        "depthfog",
        "CityDetail",
        "BUILDING",
        "building",
        "GRAPHOPTS",
        "haze",
        "dust",
        "smog",
        "atmosphere",
        "volumetric",
        "downsample",
        "gauss",
        "DoF",
        "dof",
    )
    by_kw = defaultdict(list)
    for va, s in strings:
        for kw in keywords:
            if kw in s:
                by_kw[kw].append(f"{va:08X}\t{s}")
                break
    idx_dir = out / "strings_by_keyword"
    idx_dir.mkdir(exist_ok=True)
    for kw, lines in sorted(by_kw.items()):
        (idx_dir / f"{kw}.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")

    # GRAPHOPTS full table
    graph = []
    for va, s in strings:
        if s.startswith("GRAPHOPTS_") or s in (
            "DISABLE_FULLSCREEN_BLUR",
            "RENDER_BLUR_TRAIL",
            "NO_PARTICLES",
            "RENDER_LOWLODS",
            "MOTION_BLUR_ADD",
            "MOTION_BLUR_SUB",
            "CityDetail",
        ):
            name_va = va
            ptr = struct.pack("<I", name_va)
            start = 0
            while True:
                i = data.find(ptr, start)
                if i < 0:
                    break
                # graphopt record: name, value_ptr, type
                name_p, val_p, typ = struct.unpack_from("<III", data, i)
                if name_p == name_va and 0x400000 < val_p < 0x1200000:
                    vfo = va_to_file(sections, val_p)
                    if vfo is not None:
                        if typ == 1:
                            val = f"byte={data[vfo]}"
                        elif typ == 2:
                            val = f"int={struct.unpack_from('<I', data, vfo)[0]}"
                        elif typ == 3:
                            val = f"float={struct.unpack_from('<f', data, vfo)[0]}"
                        else:
                            val = f"raw={data[vfo:vfo+4].hex()} type={typ}"
                    else:
                        val = "?"
                    rec_va = file_to_va(sections, i)
                    graph.append(
                        {
                            "name": s,
                            "name_va": hex(name_va),
                            "rec_va": hex(rec_va or 0),
                            "val_va": hex(val_p),
                            "type": typ,
                            "value": val,
                        }
                    )
                start = i + 1
    (out / "graphopts.json").write_text(json.dumps(graph, indent=2), encoding="utf-8")
    with (out / "graphopts.txt").open("w", encoding="utf-8") as f:
        for g in sorted(graph, key=lambda x: x["name"]):
            f.write(
                f"{g['name']:55} type={g['type']} val@{g['val_va']:10} {g['value']}\n"
            )
    print(f"  graphopts: {len(graph)}")

    # Embedded D3DX shader CTAB names
    shader_names = sorted(
        {
            s
            for va, s in strings
            if any(
                k in s
                for k in (
                    "bloom",
                    "Blur",
                    "blur",
                    "gauss",
                    "down",
                    "depth",
                    "fog",
                    "Fog",
                    "Sampler",
                    "ps_3_0",
                    "sm_",
                )
            )
            and len(s) < 80
        }
    )
    (out / "shader_related_strings.txt").write_text(
        "\n".join(shader_names) + "\n", encoding="utf-8"
    )

    # Absolute address cheat-sheet for sharpness work
    cheat = """# Known runtime addresses (Game.exe image base 0x400000, no ASLR)

## CityDetail / building distances
DE30E4  CityDetail int (1..3)
CF502C  farA float
CF5030  near float
CF5034  farB float
CF5038  budgetA int
CF503C  budgetB int
CF5040  scale float
CF5044  budgetC int
CF550C  altNear
CF5510  altFar

## GRAPHOPTS LOD
D0C858  HIGH_LOD_ENABLED byte
D0C85C  HIGH_LOD_DISTANCE float (vanilla 10)
D0C860  MEDIUM_LOD_DISTANCE float (vanilla 30)
D0C864  INTERIOR_DISTANCE
D0C820  DISTRICT_LOD_CULL
D0C680  RENDER_LOWLODS byte

## Fog / particles / bloom
D0C874  FOG_DEPTH byte
D0C875  FOG_VOLUMETRIC byte
D0C88C  PARTICLE_SCALE float (2400)
D0C848  BLOOM_BLUR_ITERATIONS int
D0C844  BLOOM_BRIGHTNESS float
D0C814  MOTION_BLUR_ADD
D0C818  MOTION_BLUR_SUB
DEAC92  DISABLE_FULLSCREEN_BLUR
DEAC85  RENDER_BLUR_TRAIL
DEAC86  NO_PARTICLES

## Resource type names (NGL)
SMBUILDINGLODPARAM, CITYLODS, CLOUDTWEAK, SMROADLODPARAM, GRAPHICSOPTIONS,
ENVIRONMENT, DAYSKYCOLOR, NIGHTSKYCOLOR, JSON_RENDERLOD, PARTICLE, EFFECTCONTAINER
"""
    (out / "CHEATSHEET_addresses.md").write_text(cheat, encoding="utf-8")


def dump_toc(out: Path):
    toc = GAME / "amalga.toc"
    data = toc.read_bytes()
    out.mkdir(parents=True, exist_ok=True)
    (out / "amalga.toc").write_bytes(data)
    # Dump as hex header + all ASCII strings (pack names live here)
    strings = []
    for m in re.finditer(rb"[\x20-\x7e]{3,120}", data):
        strings.append((m.start(), m.group().decode("ascii")))
    with (out / "amalga_strings.txt").open("w", encoding="utf-8") as f:
        for off, s in strings:
            f.write(f"{off:06X}\t{s}\n")
    # Likely pack name list
    packs = [s for off, s in strings if s.upper().endswith(".PCPACK") or (s.isupper() and "_" in s)]
    (out / "amalga_packish_names.txt").write_text("\n".join(packs) + "\n", encoding="utf-8")
    print(f"  toc strings: {len(strings)}")


def copy_pcinterface(out: Path):
    src = GAME / "pcinterface"
    if not src.exists():
        return
    dst = out / "pcinterface"
    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst)
    print(f"  pcinterface copied")


def list_media(out: Path):
    out.mkdir(parents=True, exist_ok=True)
    lines = []
    for sub in ("movies", "sound", "textures", "packs"):
        p = GAME / sub
        if not p.exists():
            continue
        for f in sorted(p.rglob("*")):
            if f.is_file():
                lines.append(f"{f.relative_to(GAME)}\t{f.stat().st_size}")
    (out / "file_inventory.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"  inventory: {len(lines)} files")


def extract_packs(out: Path, limit: int | None = None):
    if not SM3EXT.exists():
        raise SystemExit(f"sm3ext missing: {SM3EXT}")
    # MinGW-built sm3ext needs libstdc++/libgcc on PATH
    mingw = Path(r"C:\msys64\mingw32\bin")
    env = dict(**{k: v for k, v in __import__("os").environ.items()})
    if mingw.exists():
        env["PATH"] = str(mingw) + ";" + env.get("PATH", "")

    packs_dir = GAME / "packs"
    dest = out / "packs_extracted"
    dest.mkdir(parents=True, exist_ok=True)
    # Windows is case-insensitive — don't glob *.PCPACK and *.pcpack twice.
    seen = set()
    packs = []
    for pack in sorted(packs_dir.glob("*.PCPACK")):
        key = pack.name.lower()
        if key in seen:
            continue
        seen.add(key)
        packs.append(pack)
    if limit:
        packs = packs[:limit]
    print(f"  extracting {len(packs)} packs -> {dest}")
    done = 0
    errors = []
    for pack in packs:
        target = dest / pack.stem
        marker = target / "_EXTRACT_OK"
        if marker.exists():
            done += 1
            continue
        cmd = [str(SM3EXT), "-o", str(dest), "-q", str(pack)]
        try:
            r = subprocess.run(
                cmd, capture_output=True, text=True, timeout=600, env=env
            )
            if r.returncode != 0:
                errors.append((pack.name, (r.stderr or r.stdout or "")[-500:]))
                (dest / f"_ERROR_{pack.stem}.txt").write_text(
                    (r.stdout or "") + "\n" + (r.stderr or ""), encoding="utf-8"
                )
            else:
                target.mkdir(parents=True, exist_ok=True)
                marker.write_text("ok\n", encoding="utf-8")
                done += 1
        except Exception as ex:
            errors.append((pack.name, str(ex)))
        if (done + len(errors)) % 25 == 0:
            print(f"    ... {done}/{len(packs)} (errors={len(errors)})")
    (out / "packs_extract_errors.json").write_text(
        json.dumps(errors, indent=2), encoding="utf-8"
    )
    print(f"  packs done={done} errors={len(errors)}")


def build_pack_indexes(out: Path):
    root = out / "packs_extracted"
    if not root.exists():
        return
    names = []
    fog_hits = []
    lod_hits = []
    for p in root.rglob("*"):
        if not p.is_file():
            continue
        rel = str(p.relative_to(root)).replace("\\", "/")
        names.append(rel)
        low = rel.lower()
        if any(k in low for k in ("fog", "smoke", "cloud", "haze", "dust", "particle", "smog")):
            fog_hits.append(rel)
        if any(k in low for k in ("lod", "building", "citylod", "mip")):
            lod_hits.append(rel)
    idx = out / "packs_index"
    idx.mkdir(parents=True, exist_ok=True)
    (idx / "all_extracted_files.txt").write_text("\n".join(sorted(names)) + "\n", encoding="utf-8")
    (idx / "fog_smoke_cloud_hits.txt").write_text("\n".join(sorted(fog_hits)) + "\n", encoding="utf-8")
    (idx / "lod_building_hits.txt").write_text("\n".join(sorted(lod_hits)) + "\n", encoding="utf-8")
    # Count by extension
    by_ext = defaultdict(int)
    for n in names:
        ext = Path(n).suffix.lower() or "(none)"
        by_ext[ext] += 1
    (idx / "by_extension.txt").write_text(
        "\n".join(f"{c:8} {e}" for e, c in sorted(by_ext.items(), key=lambda x: -x[1])) + "\n",
        encoding="utf-8",
    )
    print(f"  indexed files={len(names)} fogish={len(fog_hits)} lodish={len(lod_hits)}")


def write_readme(out: Path):
    text = f"""# Spider-Man 3 PC — full game dump

Generated for remaster / RE work (sm3spectacular).

Game root: `{GAME}`
Dump root: `{out}`

## How to search

```bat
rg -i "fog|smoke|lod|blur" "{out}\\exe"
rg -i "fog|smoke|cloud|lod" "{out}\\packs_index"
rg -i "SMBUILDINGLOD|CITYLOD|CLOUDTWEAK" "{out}"
```

## Folders

| Path | Contents |
|------|----------|
| `exe/` | Game.exe strings, GRAPHOPTS table, address cheatsheet |
| `toc/` | amalga.toc + string dump |
| `packs_extracted/` | Every PCPACK unpacked via sm3ext (APKF assets, DDS, …) |
| `packs_index/` | Flat file lists + fog/LOD filtered hits |
| `pcinterface/` | UI / BNX copy |
| `media/` | Inventory of movies/sound/textures/packs sizes |

## Important for distant blur / haze

Look first at:
- `exe/CHEATSHEET_addresses.md`
- `exe/graphopts.txt` (FOG_*, PARTICLE_*, BLOOM_*, LOD_*)
- `packs_index/fog_smoke_cloud_hits.txt`
- `packs_index/lod_building_hits.txt`
- resource types: `SMBUILDINGLODPARAM`, `CITYLODS`, `CLOUDTWEAK`, `JSON_RENDERLOD`

## Re-run

```bat
python D:\\Projects\\IdeaProjects\\sm3spectacular\\tools\\build_game_dump.py
python D:\\Projects\\IdeaProjects\\sm3spectacular\\tools\\build_game_dump.py --packs-only
python D:\\Projects\\IdeaProjects\\sm3spectacular\\tools\\build_game_dump.py --meta-only
```
"""
    (out / "00_README.md").write_text(text, encoding="utf-8")


def main():
    args = set(sys.argv[1:])
    packs_only = "--packs-only" in args
    meta_only = "--meta-only" in args
    limit = None
    for a in sys.argv[1:]:
        if a.startswith("--limit="):
            limit = int(a.split("=", 1)[1])

    DUMP.mkdir(parents=True, exist_ok=True)
    print(f"Dump -> {DUMP}")

    if not packs_only:
        print("[1/5] exe dump")
        dump_exe(DUMP / "exe")
        print("[2/5] toc")
        dump_toc(DUMP / "toc")
        print("[3/5] pcinterface + inventory")
        copy_pcinterface(DUMP)
        list_media(DUMP / "media")
        write_readme(DUMP)

    if not meta_only:
        print("[4/5] pack extract (long)")
        extract_packs(DUMP, limit=limit)
        print("[5/5] indexes")
        build_pack_indexes(DUMP)
        write_readme(DUMP)

    print("DONE")


if __name__ == "__main__":
    main()
