#!/usr/bin/env python3
from __future__ import annotations

import re
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / "vendor" / "remastered" / "reshade-shaders"
GAME = Path(r"D:/Projects/IdeaProjects/Spider-Man 3 - The Game/reshade-shaders")

PAT = re.compile(
    r"BlueSkyDefender|UntouchableBlueSky|Jose Negrete|Depth3D\.info|DEPTH3D\.info|"
    r"blueskydefender|github\.com/BlueSkyDefender|buymeacoffee\.com/BlueSkyDefender|"
    r"Copyright \(C\) Depth3D",
    re.I,
)


def scrub_text(t: str) -> str:
    lines: list[str] = []
    for line in t.splitlines(keepends=True):
        if PAT.search(line):
            s = line.lstrip()
            is_pp = s.startswith(
                ("#if", "#else", "#endif", "#include", "#define", "#ifndef", "#elif", "#pragma")
            )
            if is_pp:
                if "//" in line:
                    code, _, _ = line.partition("//")
                    lines.append(code.rstrip() + ("\n" if line.endswith("\n") else ""))
                else:
                    lines.append(line)
                continue
            if s.startswith(("#", "//", "*", "/*")):
                # python comments or C comments — drop
                continue

            def blank(m: re.Match) -> str:
                return '""' if PAT.search(m.group(0)) else m.group(0)

            lines.append(re.sub(r'"([^"\\]|\\.)*"', blank, line))
            continue
        lines.append(line)
    return "".join(lines)


def main() -> None:
    for path in ROOT.rglob("*"):
        if path.suffix.lower() not in {".py", ".fx", ".fxh", ".txt", ".md"}:
            continue
        original = path.read_text(encoding="utf-8", errors="replace")
        if not PAT.search(original):
            continue
        path.write_text(scrub_text(original), encoding="utf-8", newline="\n")
        print("scrubbed", path.relative_to(ROOT))

    lic = re.compile(
        r"(?ms)^[ \t]*//-+LICENSE-+//.*?^[ \t]*//-+Code Start-+//\s*",
    )
    for rel in ("Shaders/Depth3D/Overwatch.fxh", "Shaders/AstrayFX/Overwatch.fxh"):
        p = ROOT / rel
        if not p.exists():
            continue
        t = p.read_text(encoding="utf-8", errors="replace")
        t2, n = lic.subn("//\n", t, count=1)
        if n:
            p.write_text(t2, encoding="utf-8", newline="\n")
            print("license banner removed", rel)

    leftovers = []
    for path in ROOT.rglob("*"):
        if path.suffix.lower() not in {".py", ".fx", ".fxh", ".txt", ".md"}:
            continue
        if PAT.search(path.read_text(encoding="utf-8", errors="replace")):
            leftovers.append(str(path.relative_to(ROOT)))
    print("leftovers", leftovers or "none")
    if leftovers:
        raise SystemExit(1)

    shutil.copytree(ROOT, GAME, dirs_exist_ok=True)
    cache = GAME.parent / "reshade-cache"
    if cache.exists():
        shutil.rmtree(cache)
    cache.mkdir(parents=True, exist_ok=True)
    print("deployed + cache cleared")


if __name__ == "__main__":
    main()
