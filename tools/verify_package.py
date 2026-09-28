#!/usr/bin/env python3
"""Verify a built SM3 Spectacular Edition package is publish-ready."""
from __future__ import annotations

import re
import sys
from pathlib import Path

REQUIRED_ROOT = [
    "d3d9.dll",
    "binkw32.dll",
    "libwinpthread-1.dll",
    "sm3spectacular.ini",
    "ReShade.ini",
    "ReShadePreset.ini",
    "README.txt",
    "NOTICE.txt",
    "LICENSE-RESHADE.txt",
    "VERSION",
]

REQUIRED_SHADERS = [
    "Shaders/SpectacularFinish.fx",
    "Shaders/qUINT/qUINT_mxao.fx",
    "Shaders/qUINT/qUINT_ssr.fx",
    "Shaders/FakeHDR.fx",
    "Shaders/AmbientLight.fx",
    "Shaders/Dehaze.fx",
    "Shaders/DPX.fx",
    "Shaders/PD80/PD80_02_Cinetools_LUT.fx",
    "Shaders/PD80/PD80_02_Bloom.fx",
    "Shaders/Deband.fx",
    "Shaders/SMAA.fx",
    "Shaders/CAS.fx",
    "Textures/pd80_cinelut.png",
    "Textures/AreaTex.png",
    "Textures/SearchTex.png",
]

TECH_FULL = (
    "MXAO@qUINT_mxao.fx,SSR@qUINT_ssr.fx,HDR@FakeHDR.fx,AmbientLight@AmbientLight.fx,"
    "DeHaze@Dehaze.fx,DPX@DPX.fx,prod80_02_Cinetools_LUT@PD80_02_Cinetools_LUT.fx,"
    "prod80_02_Bloom@PD80_02_Bloom.fx,Deband@Deband.fx,SMAA@SMAA.fx,"
    "ContrastAdaptiveSharpen@CAS.fx,SpectacularFinish@SpectacularFinish.fx"
)


def fail(msg: str) -> None:
    print(f"FAIL: {msg}")
    raise SystemExit(1)


def main() -> int:
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    errors = 0

    def check(cond: bool, msg: str) -> None:
        nonlocal errors
        if cond:
            print(f"OK   {msg}")
        else:
            print(f"FAIL {msg}")
            errors += 1

    for name in REQUIRED_ROOT:
        check((root / name).is_file(), f"root/{name}")

    shaders = root / "reshade-shaders"
    check(shaders.is_dir(), "reshade-shaders/")
    for rel in REQUIRED_SHADERS:
        check((shaders / rel).is_file(), f"reshade-shaders/{rel}")

    ini = (root / "sm3spectacular.ini").read_text(encoding="utf-8", errors="replace")
    check("Preset=Custom" in ini, "ini Preset=Custom")
    check("UseReShade=1" in ini, "ini UseReShade=1")
    check("Visuals=0" in ini, "ini Visuals=0")
    check("CityLife=1" in ini, "ini CityLife=1")
    check("SteamCompat=1" in ini, "ini SteamCompat=1")

    preset = (root / "ReShadePreset.ini").read_text(encoding="utf-8", errors="replace")
    m = re.search(r"(?m)^Techniques=(.+)$", preset)
    check(bool(m), "preset has Techniques=")
    if m:
        tech = m.group(1).strip()
        check(tech == TECH_FULL, "Techniques match shipped full stack")
        check("Unsharp@" not in tech, "Unsharp not in default Techniques")
        check("SpectacularFinish@" in tech, "SpectacularFinish enabled")

    fx_sections = len(re.findall(r"(?m)^\[.+\.fx\]", preset))
    check(fx_sections >= 12, f"preset fx sections ({fx_sections} >= 12)")

    reshade = (root / "ReShade.ini").read_text(encoding="utf-8", errors="replace")
    check("PresetPath=.\\ReShadePreset.ini" in reshade or "PresetPath=./ReShadePreset.ini" in reshade,
          "ReShade PresetPath")
    check("EffectSearchPaths=" in reshade, "ReShade EffectSearchPaths")
    check("reshade-cache" in reshade, "ReShade cache path")

    # Public zip must stay close to 1.0.0 layout (Nexus scanners).
    check(not (root / "INSTALL.bat").exists(), "no INSTALL.bat")
    check(not (root / "tools").exists(), "no tools/ scripts in public zip")
    check(not list(root.glob("**/*.py")), "no .py in public zip")
    check(not list(root.glob("**/*.bat")), "no .bat in public zip")

    d3d = root / "d3d9.dll"
    bink = root / "binkw32.dll"
    check(d3d.stat().st_size > 500_000, f"d3d9.dll size {d3d.stat().st_size}")
    check(bink.stat().st_size > 100_000, f"binkw32.dll size {bink.stat().st_size}")

    ver = (root / "VERSION").read_text(encoding="utf-8").strip()
    check(bool(re.match(r"^\d+\.\d+\.\d+$", ver)), f"VERSION={ver}")

    if errors:
        fail(f"{errors} check(s) failed")
    print(f"\nPackage OK — SM3 Spectacular Edition v{ver}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
