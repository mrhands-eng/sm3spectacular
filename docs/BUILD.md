# Build guide

## Prerequisites

| Tool | Notes |
|------|--------|
| MinGW-w64 **i686** | Game.exe is 32-bit — x64 toolchains will not work |
| CMake 3.16+ | |
| Ninja | Used by `build.bat` |
| Python 3 | `tools/slim_reshade_pack.py`, package verify |

Example (MSYS2):

```bat
pacman -S mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-ninja
```

## Vendor Remastered pack

`build.bat` expects:

- `vendor/remastered/d3d9_reshade.dll`
- `vendor/remastered/ReShade.ini`
- `vendor/remastered/ReShadePreset.ini` (+ `.full.ini` after slim pack)
- `vendor/remastered/reshade-shaders/`

If missing, run `tools/sync_remastered.bat` (edit the source path inside the script to your Remastered install).

## Commands

```bat
build.bat
```

Builds `build/bin/binkw32.dll` and `build/bin/d3d9.dll` (native fallback).  
Optional: set `GAME_DIR` to auto-deploy; set `SKIP_DEPLOY=1` to build only.

```bat
package-hybrid.bat
```

Builds (with `SKIP_DEPLOY=1`), assembles `dist/SM3-Spectacular-Edition-vX.Y.Z/`, verifies, and creates a **7-Zip** `.zip` for distribution.

## Architecture

| File in game folder | Role |
|---------------------|------|
| `d3d9.dll` | Remastered ReShade (from `vendor/remastered/d3d9_reshade.dll`) |
| `binkw32.dll` | Spectacular companion (this repo) |
| `binkw32_stock.dll` | Player's original Bink (not redistributed) |
| `sm3spectacular.ini` | Companion config |

Native `d3d9` from this repo is packaged only for advanced / non-hybrid use (`UseReShade=0`); the public hybrid zip ships Remastered as `d3d9.dll` only.
