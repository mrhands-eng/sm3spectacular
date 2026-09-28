#!/usr/bin/env python3
"""Fill remaining high-value dump gaps (PE, shaders, resource catalog, TOC)."""
from __future__ import annotations

import json
import re
import struct
from collections import Counter, defaultdict
from pathlib import Path

GAME = Path(r"D:\Projects\IdeaProjects\Spider-Man 3 - The Game")
DUMP = Path(r"D:\Projects\IdeaProjects\sm3-game-dump")
IMAGE_BASE = 0x400000


def pe_sections(data: bytes):
    e = struct.unpack_from("<I", data, 0x3C)[0]
    coff = e + 4
    nsec = struct.unpack_from("<H", data, coff + 2)[0]
    opt = coff + 20
    magic = struct.unpack_from("<H", data, opt)[0]
    if magic != 0x10B:
        raise SystemExit("not PE32")
    dd = opt + 96  # DataDirectory for PE32
    sec_off = opt + struct.unpack_from("<H", data, coff + 16)[0]
    sections = []
    for i in range(nsec):
        off = sec_off + i * 40
        name = data[off : off + 8].split(b"\0")[0].decode("ascii", "replace")
        vsz, va, rsz, raw = struct.unpack_from("<IIII", data, off + 8)
        sections.append((name, va, vsz, raw, rsz))
    return e, coff, opt, dd, sections


def rva_to_file(sections, rva: int) -> int | None:
    for name, va, vsz, raw, rsz in sections:
        if va <= rva < va + max(rsz, vsz):
            return raw + (rva - va)
    return None


def dump_pe_imports(out: Path):
    data = (GAME / "Game.exe").read_bytes()
    e, coff, opt, dd, sections = pe_sections(data)
    import_rva, import_size = struct.unpack_from("<II", data, dd + 8)  # entry 1
    fo = rva_to_file(sections, import_rva)
    lines = []
    if fo is None:
        lines.append("import table not found")
    else:
        i = fo
        while True:
            ilt, td, fwd, name_rva, iat = struct.unpack_from("<IIIII", data, i)
            if ilt == 0 and name_rva == 0:
                break
            nfo = rva_to_file(sections, name_rva)
            dll = data[nfo : data.find(b"\0", nfo)].decode("ascii", "replace") if nfo else "?"
            lines.append(f"DLL {dll}")
            # walk INT
            thunk_rva = ilt or iat
            tfo = rva_to_file(sections, thunk_rva)
            if tfo is not None:
                j = tfo
                while True:
                    thunk = struct.unpack_from("<I", data, j)[0]
                    if thunk == 0:
                        break
                    if thunk & 0x80000000:
                        lines.append(f"  ord {thunk & 0xFFFF}")
                    else:
                        ifo = rva_to_file(sections, thunk)
                        if ifo is not None:
                            hint = struct.unpack_from("<H", data, ifo)[0]
                            nm = data[ifo + 2 : data.find(b"\0", ifo + 2)].decode(
                                "ascii", "replace"
                            )
                            lines.append(f"  {hint:04X} {nm}")
                    j += 4
            i += 20
    out.mkdir(parents=True, exist_ok=True)
    (out / "pe_imports.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"  imports lines={len(lines)}")


def dump_shader_ctabs(out: Path):
    data = (GAME / "Game.exe").read_bytes()
    # Find CTAB chunks near Microsoft D3DX shader compiler banner
    hits = []
    for m in re.finditer(rb"CTAB", data):
        start = max(0, m.start() - 64)
        end = min(len(data), m.start() + 512)
        chunk = data[start:end]
        ascii_bits = re.findall(rb"[\x20-\x7e]{4,64}", chunk)
        names = [b.decode("ascii", "replace") for b in ascii_bits]
        if any("ps_2" in n or "ps_3" in n or "Sampler" in n or "vs_" in n for n in names):
            hits.append({"off": m.start(), "names": names[:40]})
    out.mkdir(parents=True, exist_ok=True)
    (out / "embedded_shader_ctabs.json").write_text(
        json.dumps(hits, indent=2), encoding="utf-8"
    )
    # flat unique constant/sampler names
    uniq = sorted({n for h in hits for n in h["names"] if re.match(r"^[A-Za-z_]", n)})
    (out / "embedded_shader_symbols.txt").write_text("\n".join(uniq) + "\n", encoding="utf-8")
    print(f"  shader CTABs={len(hits)} symbols={len(uniq)}")


def catalog_pack_resources(out: Path):
    root = DUMP / "packs_extracted"
    # Filenames look like name.type.c0_img.bin or name.tex.dds
    type_counter = Counter()
    interesting = defaultdict(list)
    keys = (
        "citylod",
        "buildinglod",
        "cloudtweak",
        "graphicsoptions",
        "renderlod",
        "particle",
        "environment",
        "daysky",
        "nightsky",
        "fog",
        "smoke",
        "blur",
        "bloom",
        "outdoors",
        "allbuildings",
    )
    all_files = DUMP / "packs_index" / "all_extracted_files.txt"
    if not all_files.exists():
        print("  no pack index yet")
        return
    for line in all_files.read_text(encoding="utf-8", errors="replace").splitlines():
        low = line.lower()
        # extension / type token
        m = re.search(r"\.([a-z0-9_]+)\.(?:c\d_|dds|bin)", low)
        if m:
            type_counter[m.group(1)] += 1
        else:
            type_counter[Path(low).suffix or "(none)"] += 1
        for k in keys:
            if k in low:
                interesting[k].append(line)
                break
    out.mkdir(parents=True, exist_ok=True)
    (out / "resource_type_counts.txt").write_text(
        "\n".join(f"{c:8} {t}" for t, c in type_counter.most_common()) + "\n",
        encoding="utf-8",
    )
    for k, files in interesting.items():
        (out / f"resources_{k}.txt").write_text("\n".join(files) + "\n", encoding="utf-8")
    summary = {k: len(v) for k, v in sorted(interesting.items())}
    (out / "resources_interest_summary.json").write_text(
        json.dumps(summary, indent=2), encoding="utf-8"
    )
    print(f"  interest={summary}")


def parse_amalga_better(out: Path):
    data = (GAME / "amalga.toc").read_bytes()
    out.mkdir(parents=True, exist_ok=True)
    # Dump u32 stream stats + pack name table heuristic
    u32s = list(struct.unpack("<" + "I" * (len(data) // 4), data[: len(data) // 4 * 4]))
    (out / "amalga_u32_head.txt").write_text(
        "\n".join(f"{i*4:06X} {v:08X} {v}" for i, v in enumerate(u32s[:64])) + "\n",
        encoding="utf-8",
    )
    # Extract every ASCII token that looks like a pack stem
    names = []
    for m in re.finditer(rb"[A-Z0-9_]{3,64}", data):
        s = m.group().decode("ascii")
        if s.endswith("PACK") or "_" in s:
            names.append((m.start(), s))
    (out / "amalga_tokens.txt").write_text(
        "\n".join(f"{off:06X}\t{s}" for off, s in names) + "\n", encoding="utf-8"
    )
    print(f"  amalga tokens={len(names)}")


def dump_sound_movies_inventory(out: Path):
    out.mkdir(parents=True, exist_ok=True)
    lines = []
    for sub in ("sound", "movies", "textures"):
        p = GAME / sub
        if not p.exists():
            continue
        for f in sorted(p.rglob("*")):
            if f.is_file():
                lines.append(f"{f.relative_to(GAME)}\t{f.stat().st_size}")
    (out / "loose_media_inventory.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    # Also list carved FSBs inside packs
    fsbs = list((DUMP / "packs_extracted").rglob("*.fsb")) if (DUMP / "packs_extracted").exists() else []
    (out / "carved_fsb_list.txt").write_text(
        "\n".join(str(f.relative_to(DUMP / "packs_extracted")) for f in fsbs) + "\n",
        encoding="utf-8",
    )
    print(f"  loose media={len(lines)} carved fsb={len(fsbs)}")


def write_completeness(out: Path):
    text = f"""# Dump completeness report

## Collected (usable now)

- Game.exe strings (45k+), GRAPHOPTS, address cheatsheet → `exe/`
- RTTI class map (2880), SLF methods (1479), pseudo_src buckets → `source_map/`
- All 1092 PCPACKs extracted (~11 GB) → `packs_extracted/`
- Pack indexes (443k files, fog/lod filters) → `packs_index/`
- pcinterface copy, amalga.toc strings → `toc/`, `pcinterface/`
- PE imports, embedded shader CTAB symbols → `exe/` (this pass)
- Resource interest catalogs (citylod/cloudtweak/…) → `packs_index/resources_*.txt`

## Intentionally NOT copied (too large / low RE value as bulk)

- Full `movies/*.bik` bitstream copies (listed in inventory only)
- Full `sound/` tree copies if separate from packs (inventory + carved FSB list)
- Rebuilding meshes to glTF/OBJ (sm3ext does not do this yet)

## Cannot collect without heavy interactive RE

- Original C++ source / PDB (does not exist in the release)
- Full decompiled function bodies for all of Game.exe (needs Ghidra/IDA project;
  we only recovered the *map*: classes, strings, addresses)
- Perfect amalga.toc structural schema (heuristic dump only)

## Next highest-value step for your haze/blur issue

1. `packs_index/resources_cloudtweak.txt` + `resources_citylod.txt` + `resources_buildinglod.txt`
2. `source_map/FOCUS_fog_lod_blur_classes.txt`
3. Ghidra on `entity_tracker__set_fog_*` / `sm_clouds` / `sm_buildinglod`
"""
    (DUMP / "COMPLETENESS.md").write_text(text, encoding="utf-8")
    print(f"  wrote COMPLETENESS.md")


def main():
    print("Filling dump gaps...")
    dump_pe_imports(DUMP / "exe")
    dump_shader_ctabs(DUMP / "exe")
    catalog_pack_resources(DUMP / "packs_index")
    parse_amalga_better(DUMP / "toc")
    dump_sound_movies_inventory(DUMP / "media")
    write_completeness(DUMP)
    print("DONE")


if __name__ == "__main__":
    main()
