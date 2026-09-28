#!/usr/bin/env python3
"""Restore author/license header blocks from TeaserPlay Remastered originals.
Keeps functional watermark-removal edits (clean Out()) intact.
"""
from __future__ import annotations

import re
from pathlib import Path

ORIG = Path(r"D:/Projects/IdeaProjects/Spider Man 3 Remastered/reshade-shaders")
OURS = Path(__file__).resolve().parents[1] / "vendor" / "remastered" / "reshade-shaders"

# Files we previously scrubbed that still exist in vendor
TARGETS = [
    "Shaders/AstrayFX/BloomingHDR.fx",
    "Shaders/AstrayFX/Depth_Cues.fx",
    "Shaders/AstrayFX/DLAA_Plus.fx",
    "Shaders/AstrayFX/Flair.fx",
    "Shaders/AstrayFX/GloomAO.fx",
    "Shaders/AstrayFX/NFAA.fx",
    "Shaders/AstrayFX/OneShot/Limbo_Mod.fx",
    "Shaders/AstrayFX/OneShot/SnowScape.fx",
    "Shaders/AstrayFX/Overwatch.fxh",
    "Shaders/AstrayFX/RadiantGI.fx",
    "Shaders/AstrayFX/Smart_Sharp.fx",
    "Shaders/AstrayFX/Temporal_AA.fx",
    "Shaders/AstrayFX/Trails.fx",
    "Shaders/AstrayFX/TobiiEye_FreePie_AstrayFX.py",
    "Shaders/Depth3D/Game_Help.txt",
    "Shaders/Depth3D/Others/3DToElse.fx",
    "Shaders/Depth3D/Others/Depth_Tool.fx",
    "Shaders/Depth3D/Others/Dimension_Plus.fx",
    "Shaders/Depth3D/Others/Polynomial_Barrel_Distortion_for_HMDs.fx",
    "Shaders/Depth3D/Others/SuperDepth3D_WoWvx.fx",
    "Shaders/Depth3D/Others/VirtualNose.fx",
    "Shaders/Depth3D/Overwatch.fxh",
    "Shaders/Depth3D/SuperDepth3D.fx",
    "Shaders/Depth3D/SuperDepth3D_VR+.fx",
    "Shaders/Depth3D/TobiiEye_FreePie_Depth3D.py",
    "Shaders/Depth3D.fx",
    "Shaders/VRS_Map.fx",
    "Shaders/VRS_Map.fxh",
]


def extract_header(text: str) -> str | None:
    """Take leading comment/license banner until first non-comment code-ish line after banner."""
    lines = text.splitlines(keepends=True)
    if not lines:
        return None
    # Prefer: everything up to and including the big //// LICENSE banner end
    # Fall back: first ~120 comment lines
    buf: list[str] = []
    saw_license = False
    for i, line in enumerate(lines):
        s = line.lstrip()
        is_comment = (
            s.startswith("//")
            or s.startswith("/*")
            or s.startswith("*")
            or s.startswith("////")
            or s.strip() == ""
            or (s.startswith("///") )
        )
        if "LICENSE" in line.upper() or "BlueSkyDefender" in line or "Creative Commons" in line:
            saw_license = True
        if i < 5:
            buf.append(line)
            continue
        if is_comment or (saw_license and s.startswith("//")):
            buf.append(line)
            # End after closing banner line of ===== or //// after BlueSky credit
            if saw_license and ("github.com/BlueSkyDefender" in line or "Depth3D" in line and i > 40):
                # include a few more trailing banner lines
                for j in range(i + 1, min(i + 5, len(lines))):
                    if lines[j].lstrip().startswith("//") or lines[j].strip() == "" or "////" in lines[j]:
                        buf.append(lines[j])
                    else:
                        break
                return "".join(buf)
            continue
        break
    if saw_license or any("BlueSkyDefender" in x for x in buf):
        return "".join(buf)
    return None


def restore_blooming_hdr() -> None:
    """Special-case: restore full original header, keep our clean Out()."""
    rel = "Shaders/AstrayFX/BloomingHDR.fx"
    orig = (ORIG / rel).read_text(encoding="utf-8", errors="replace")
    ours = (OURS / rel).read_text(encoding="utf-8", errors="replace")

    # Original header ends before first #if exists "Flair
    m = re.search(r'(?m)^#if exists "Flair\.fx"', orig)
    if not m:
        raise SystemExit("BloomingHDR: cannot find Flair interceptor")
    header = orig[: m.start()]

    # Our body from Flair interceptor onward, but Out() already clean
    m2 = re.search(r'(?m)^#if exists "Flair\.fx"', ours)
    if not m2:
        # find Shared Texture comment
        m2 = re.search(r"(?m)^//Shared Texture for Blooming HDR", ours)
    if not m2:
        raise SystemExit("BloomingHDR ours: cannot find body start")
    body = ours[m2.start() :]

    note = (
        "//*\n"
        "//* SM3 Spectacular Edition note:\n"
        "//* On-screen DEPTH3D.info logo/watermark drawing code in Out() was removed for gameplay.\n"
        "//* Effect logic (HDROut) is unchanged. See NOTICE.txt in the package root.\n"
        "//*\n"
    )
    # Insert note before closing banner if possible
    if "https://creativecommons.org/licenses/by-nd/4.0/" in header:
        header = header.replace(
            "https://creativecommons.org/licenses/by-nd/4.0/\n",
            "https://creativecommons.org/licenses/by-nd/4.0/\n" + note,
        )
    else:
        header = header + note

    (OURS / rel).write_text(header + body, encoding="utf-8", newline="\n")
    print("restored BloomingHDR header + kept clean Out()")


def restore_generic(rel: str) -> bool:
    src = ORIG / rel
    dst = OURS / rel
    if not src.is_file() or not dst.is_file():
        return False
    orig = src.read_text(encoding="utf-8", errors="replace")
    ours = dst.read_text(encoding="utf-8", errors="replace")
    if "BlueSkyDefender" in ours and "Copyright (C) Depth3D" in ours:
        return False  # already attributed
    if "BlueSkyDefender" not in orig and "Copyright (C) Depth3D" not in orig:
        return False

    # For Overwatch.fxh / help / py: restore whole file from original if we don't ship modified logic
    if rel.endswith((".fxh", ".txt", ".py")) and "Out(" not in ours:
        dst.write_text(orig, encoding="utf-8", newline="\n")
        print("restored whole", rel)
        return True

    header = extract_header(orig)
    if not header:
        print("skip (no header)", rel)
        return False

    # Drop our leading comment block if present, keep code from first #if / #include / uniform / texture / float
    m = re.search(
        r'(?m)^(?:#if |#include |#define |uniform |texture |float |void |int |bool |static )',
        ours,
    )
    if not m:
        print("skip (no body)", rel)
        return False
    body = ours[m.start() :]
    note = (
        "// SM3 Spectacular: see NOTICE.txt — attribution restored; on-screen logo overlays removed where present.\n"
    )
    dst.write_text(header + note + body, encoding="utf-8", newline="\n")
    print("restored header", rel)
    return True


def main() -> None:
    if not ORIG.is_dir():
        raise SystemExit(f"Remastered source missing: {ORIG}")
    restore_blooming_hdr()
    for rel in TARGETS:
        if rel.endswith("BloomingHDR.fx"):
            continue
        restore_generic(rel)


if __name__ == "__main__":
    main()
