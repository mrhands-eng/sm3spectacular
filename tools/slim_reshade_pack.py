#!/usr/bin/env python3
"""Build a slim reshade-shaders tree with only active Techniques + deps."""
from __future__ import annotations

import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "vendor" / "remastered" / "reshade-shaders"
DST = ROOT / "vendor" / "remastered" / "reshade-shaders-hybrid"
PRESET = ROOT / "vendor" / "remastered" / "ReShadePreset.ini"
PRESET_FULL = ROOT / "vendor" / "remastered" / "ReShadePreset.full.ini"

# BloomingHDR (CC BY-ND) is intentionally NOT shipped — PD80 bloom covers glow.
# Pipeline (1.1.5): depth → lighting → grade → bloom → AA → soft CAS → SpectacularFinish.
# Unsharp removed from defaults (fought CAS). Bonus LUT / MB / FilmicPass optional
# (FilmicPass auto-on for Steam-safe path only).
TECHNIQUES = [
    "qUINT/qUINT_mxao.fx",
    "qUINT/qUINT_ssr.fx",
    "FakeHDR.fx",
    "AmbientLight.fx",
    "Dehaze.fx",
    "DPX.fx",
    "PD80/PD80_02_Cinetools_LUT.fx",
    "PD80/PD80_02_Bloom.fx",
    "Deband.fx",
    "SMAA.fx",
    "CAS.fx",
    "SpectacularFinish.fx",
]

OPTIONAL_SHADERS = [
    "Unsharp.fx",
    "FakeMotionBlur.fx",
    "CinematicMotionBlur.fx",
    "FilmicPass.fx",
    "PD80/PD80_02_Bonus_LUT_pack.fx",
]

TECHNIQUE_LINE = [
    "MXAO@qUINT_mxao.fx",
    "SSR@qUINT_ssr.fx",
    "HDR@FakeHDR.fx",
    "AmbientLight@AmbientLight.fx",
    "DeHaze@Dehaze.fx",
    "DPX@DPX.fx",
    "prod80_02_Cinetools_LUT@PD80_02_Cinetools_LUT.fx",
    "prod80_02_Bloom@PD80_02_Bloom.fx",
    "Deband@Deband.fx",
    "SMAA@SMAA.fx",
    "ContrastAdaptiveSharpen@CAS.fx",
    "SpectacularFinish@SpectacularFinish.fx",
]

KEEP_FX = {Path(t).name for t in TECHNIQUES} | {Path(t).name for t in OPTIONAL_SHADERS}

INC_RE = re.compile(r'#include\s+["<]([^">]+)[">]')
SRC_RE = re.compile(r'source\s*=\s*"([^"]+)"', re.I)
DEFINE_TEX_RE = re.compile(
    r'#\s*define\s+PD80_LUT_File_Name\s+"([^"]+)"',
    re.I,
)
BUILTIN_SOURCES = {
    "timer",
    "frametime",
    "framecount",
    "pingpong",
    "random",
    "date",
    "overlay_active",
    "key",
    "mousepoint",
    "mousedelta",
    "mousebutton",
}


def find_include(from_file: Path, rel: str) -> Path | None:
    rel_path = Path(rel.replace("\\", "/"))
    shaders = SRC / "Shaders"
    candidates = [
        from_file.parent / rel_path,
        shaders / rel_path,
        shaders / rel_path.name,
    ]
    # Prefer canonical root headers over nested vendor copies (CorgiFX/Fubax/…).
    if rel_path.name.lower() in {"reshade.fxh", "reshadeui.fxh"}:
        root_hit = shaders / rel_path.name
        if root_hit.is_file():
            return root_hit.resolve()
    for c in candidates:
        if c.is_file():
            return c.resolve()
    hits = sorted((SRC / "Shaders").rglob(rel_path.name))
    # Prefer shortest path (root-ish) if we must rglob.
    if hits:
        hits.sort(key=lambda p: len(p.parts))
        return hits[0].resolve()
    return None


def collect() -> tuple[set[Path], set[str]]:
    shaders_root = SRC / "Shaders"
    queue = [shaders_root / t.replace("/", "\\") for t in TECHNIQUES]
    queue.extend(shaders_root / t.replace("/", "\\") for t in OPTIONAL_SHADERS)
    for name in ("ReShade.fxh", "ReShadeUI.fxh"):
        root_hdr = shaders_root / name
        if root_hdr.is_file():
            queue.append(root_hdr)

    seen: set[str] = set()
    files: set[Path] = set()
    textures: set[str] = set()
    missing: list[str] = []

    while queue:
        p = queue.pop()
        if not p.is_file():
            missing.append(str(p))
            continue
        key = str(p.resolve()).lower()
        if key in seen:
            continue
        seen.add(key)
        files.add(p.resolve())
        text = p.read_text(encoding="utf-8", errors="replace")
        for m in INC_RE.finditer(text):
            found = find_include(p, m.group(1))
            if found:
                queue.append(found)
            else:
                if Path(m.group(1)).name.lower() != "smaa.h":
                    missing.append(f"{p.name} -> {m.group(1)}")
        for m in SRC_RE.finditer(text):
            name = m.group(1)
            if name.lower() in BUILTIN_SOURCES:
                continue
            textures.add(name)
        for m in DEFINE_TEX_RE.finditer(text):
            textures.add(m.group(1))

    if missing:
        print("WARN missing:")
        for m in missing:
            print(" ", m)
    return files, textures


def write_slim_preset() -> None:
    """Keep only active techniques and matching [file.fx] sections.

    Always rebuild from ReShadePreset.full.ini when present so repeated slim
    runs cannot wipe tuned effect parameters.
    """
    src_preset = PRESET_FULL if PRESET_FULL.is_file() else PRESET
    text = src_preset.read_text(encoding="utf-8", errors="replace")
    tech_csv = ",".join(TECHNIQUE_LINE)
    text = re.sub(r"(?m)^Techniques=.*$", "Techniques=" + tech_csv, text, count=1)
    text = re.sub(r"(?m)^TechniqueSorting=.*$", "TechniqueSorting=" + tech_csv, text, count=1)

    # Drop orphan [Something.fx] blocks for shaders not in the slim pack.
    def keep_section(match: re.Match[str]) -> str:
        name = match.group(1)
        return match.group(0) if name in KEEP_FX else ""

    text = re.sub(
        r"(?m)^\[([^\]\r\n]+\.fx)\]\r?\n(?:(?!\[)[^\n]*\r?\n)*",
        keep_section,
        text,
    )

    header_notes = (
        "; qUINT_rtgi.fx is not redistributed — MXAO covers AO.\n"
        "; BloomingHDR.fx (CC BY-ND) is not shipped — use PD80 bloom.\n"
    )
    text = re.sub(r"(?m)^; qUINT_rtgi\.fx.*\n(; BloomingHDR\.fx.*\n)?", "", text)
    if "PreprocessorDefinitions=" in text:
        text = text.replace(
            "PreprocessorDefinitions=\n",
            "PreprocessorDefinitions=\n" + header_notes,
            1,
        )

    text = re.sub(r"\n{3,}", "\n\n", text)
    kept = len(re.findall(r"(?m)^\[.+\.fx\]", text))
    min_sections = 5
    if kept < min_sections:
        raise SystemExit(
            f"ERROR: slim preset would keep only {kept} fx sections (need >= {min_sections}). "
            f"Check KEEP_FX matching / section regex. Refusing to write broken preset."
        )
    PRESET.write_text(text, encoding="utf-8", newline="\n")
    print("updated", PRESET, f"(from {src_preset.name}, {kept} fx sections kept)")


def main() -> None:
    files, textures = collect()
    if DST.exists():
        shutil.rmtree(DST)
    (DST / "Shaders").mkdir(parents=True)
    (DST / "Textures").mkdir(parents=True)

    shaders_root = (SRC / "Shaders").resolve()
    for src in sorted(files):
        rel = src.relative_to(shaders_root)
        dst = DST / "Shaders" / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)

    tex_root = SRC / "Textures"
    for name in sorted(textures):
        src = tex_root / name
        if src.is_file():
            shutil.copy2(src, DST / "Textures" / name)
        else:
            hits = list(tex_root.rglob(Path(name).name))
            if hits:
                rel = hits[0].relative_to(tex_root)
                dst = DST / "Textures" / rel
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(hits[0], dst)
            else:
                print("TEX MISS", name)

    write_slim_preset()
    print(f"slim pack: {len(files)} shaders, {len(textures)} texture refs -> {DST}")


if __name__ == "__main__":
    main()
