#!/usr/bin/env python3
"""Surgically add FPS menu keys to any Spider-Man 3 pcinterface/igct_*.bnx (UTF-16 LE).

Does NOT replace the whole locale file — only inserts:
  BX_3DMENU_VIDEO_OPTIONS_FPS_30/60/120/144/UNLIMITED
and renames AUTOEXP row title to FPS Limit / Лимит FPS.

Usage:
  python tools/patch_igct_fps.py [path-to-game-or-pcinterface]
"""
from __future__ import annotations

import sys
from pathlib import Path

KEYS = [
    "BX_3DMENU_VIDEO_OPTIONS_FPS_30=30",
    "BX_3DMENU_VIDEO_OPTIONS_FPS_60=60",
    "BX_3DMENU_VIDEO_OPTIONS_FPS_120=120",
    "BX_3DMENU_VIDEO_OPTIONS_FPS_144=144",
    "BX_3DMENU_VIDEO_OPTIONS_FPS_UNLIMITED=Unlimited",
]
AUTOEXP_PREFIX = "BX_3DMENU_VIDEO_OPTIONS_AUTOEXP="
FPS60_MARKER = "BX_3DMENU_VIDEO_OPTIONS_FPS_60="


def patch_text(text: str, russian: bool) -> str | None:
    if FPS60_MARKER in text:
        return None
    nl = "\r\n" if "\r\n" in text else "\n"
    auto = AUTOEXP_PREFIX + ("Лимит FPS" if russian else "FPS Limit")
    block = auto + nl + nl.join(KEYS) + nl
    lines = text.splitlines(keepends=True)
    out: list[str] = []
    replaced = False
    for line in lines:
        raw = line.rstrip("\r\n")
        if not replaced and raw.startswith(AUTOEXP_PREFIX):
            # Keep file newline style via block (already has nl).
            out.append(block)
            replaced = True
        else:
            out.append(line if line.endswith(("\n", "\r")) else line + nl)
    if not replaced:
        body = "".join(out)
        if body and not body.endswith(("\n", "\r")):
            body += nl
        return body + block
    return "".join(out)


def patch_file(path: Path) -> bool:
    raw = path.read_bytes()
    if raw[:2] != b"\xff\xfe":
        print(f"skip (no UTF-16 LE BOM): {path}")
        return False
    text = raw[2:].decode("utf-16-le")
    russian = "igct_ru" in path.name.lower()
    new = patch_text(text, russian)
    if new is None:
        print(f"ok (already patched): {path}")
        return True
    bak = path.with_suffix(path.suffix + ".stock.bak")
    if not bak.exists():
        bak.write_bytes(raw)
        print(f"backup: {bak}")
    path.write_bytes(b"\xff\xfe" + new.encode("utf-16-le"))
    print(f"patched: {path}")
    return True


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    if (root / "pcinterface").is_dir():
        folder = root / "pcinterface"
    elif root.name.lower() == "pcinterface":
        folder = root
    else:
        folder = root
    files = sorted(folder.glob("igct_*.bnx"))
    if not files:
        print(f"no igct_*.bnx under {folder}")
        return 1
    ok = 0
    for f in files:
        if patch_file(f):
            ok += 1
    print(f"done: {ok}/{len(files)}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
