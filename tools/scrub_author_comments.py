#!/usr/bin/env python3
"""Remove Depth3D / BlueSkyDefender author & license comments from remastered shaders.
Does not touch preprocessor directives or zryuyu credits.
"""
from __future__ import annotations

import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "vendor" / "remastered" / "reshade-shaders"
GAME = Path(r"D:/Projects/IdeaProjects/Spider-Man 3 - The Game/reshade-shaders")

MARKERS = re.compile(
    r"BlueSkyDefender|UntouchableBlueSky|Jose Negrete|Depth3D\.info|DEPTH3D\.info|"
    r"blueskydefender|github\.com/BlueSkyDefender|Copyright \(C\) Depth3D|"
    r"Depth3D - All Rights Reserved|http://www\.Depth3D|https://Depth3D",
    re.I,
)

# Lines that are safe to delete entirely if they match MARKERS
COMMENT_LINE = re.compile(r"^[ \t]*(?://|/\*|\*|//\*)")


def scrub_file(path: Path) -> bool:
    original = path.read_text(encoding="utf-8", errors="replace")
    if not MARKERS.search(original):
        return False

    out_lines: list[str] = []
    changed = False
    for line in original.splitlines(keepends=True):
        stripped = line.lstrip()
        # Never delete preprocessor / includes
        if stripped.startswith("#"):
            # May still have trailing // comment with author — strip trailing comment only
            if MARKERS.search(line) and "//" in line:
                code, _, _ = line.partition("//")
                # Keep code part; drop author comment
                if code.strip():
                    nl = "\n" if line.endswith("\n") else ""
                    out_lines.append(code.rstrip() + nl)
                    changed = True
                    continue
            out_lines.append(line)
            continue

        if MARKERS.search(line) and COMMENT_LINE.match(line):
            changed = True
            continue  # drop author/license comment line

        if MARKERS.search(line):
            # ui_text / ui_tooltip / string credits — blank the matching quoted chunks carefully
            def blank_str(m: re.Match) -> str:
                s = m.group(0)
                if MARKERS.search(s):
                    return '""'
                return s

            new_line = re.sub(r'"([^"\\]|\\.)*"', blank_str, line)
            if new_line != line:
                out_lines.append(new_line)
                changed = True
                continue
            # Fallback: drop the line if it's mostly credit prose without # 
            if not any(tok in line for tok in ("=", "{", "}", "(", ")", ";", "#include", "return", "float", "int ", "void ", "bool ")):
                changed = True
                continue
        out_lines.append(line)

    text = "".join(out_lines)
    # Collapse runs of empty comment banners left behind
    text = re.sub(r"(?m)^[ \t]*//[/\\*\-=]{10,}[ \t]*\n(?:[ \t]*\n){0,2}", "", text)
    if text != original:
        path.write_text(text, encoding="utf-8", newline="\n")
        return True
    return changed


def main() -> None:
    touched = 0
    for path in sorted(ROOT.rglob("*")):
        if path.suffix.lower() not in {".fx", ".fxh", ".txt", ".md"}:
            continue
        if scrub_file(path):
            print("scrubbed", path.relative_to(ROOT))
            touched += 1

    # Verify no markers remain (except folder names / technique names like Depth3D.fx path)
    leftovers = []
    for path in ROOT.rglob("*"):
        if path.suffix.lower() not in {".fx", ".fxh", ".txt", ".md"}:
            continue
        t = path.read_text(encoding="utf-8", errors="replace")
        if MARKERS.search(t):
            leftovers.append(path.relative_to(ROOT))

    print(f"touched={touched}")
    if leftovers:
        print("LEFTOVERS:")
        for p in leftovers:
            print(" ", p)
        raise SystemExit(1)

    # Deploy
    shutil.copytree(ROOT, GAME, dirs_exist_ok=True)
    cache = GAME.parent / "reshade-cache"
    if cache.exists():
        shutil.rmtree(cache)
    cache.mkdir(parents=True, exist_ok=True)
    print("deployed + cache cleared")


if __name__ == "__main__":
    main()
