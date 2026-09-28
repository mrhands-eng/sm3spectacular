# SM3 Spectacular Edition

Hybrid remaster for **Spider-Man 3 (PC, 2007)** — Activision retail `Game.exe`.

**Author:** zryuyu · **Version:** see [`VERSION`](VERSION)

Remastered ReShade runs as `d3d9.dll`. The Spectacular companion runs as `binkw32.dll` (FPS, CityLife, LOD/FOV/AF, Steam compat). Brand New Day–inspired look via **SpectacularFinish**.

> You need a legal copy of the game. This repository does **not** ship `Game.exe` or original Bink.

## Player install (release zip)

1. Rename `binkw32.dll` → `binkw32_stock.dll` (once) in the `Game.exe` folder  
2. Copy package files into that folder  
3. In-game: **MSAA / FSAA = Off**  
4. Launch · ReShade overlay = **Home** · log = `sm3spectacular.log`

## Repository layout

```
src/                  Companion + native d3d9 sources
tools/                Pack / sync / verify scripts
tools/research/       One-off RE / dump helpers (optional)
vendor/remastered/    Remastered ReShade + shaders (build input)
pcinterface/          Optional FPS-label reference (igct)
textures/ shaders/    Native postfx assets (fallback path)
docs are root txt/md  README, NOTICE, CREDITS, CHANGELOG
```

## Build (developers)

**Requirements:** Windows · MinGW i686 (`g++`) · CMake · Ninja · Python 3

```bat
:: Sync Remastered pack into vendor\ (edit path in tools\sync_remastered.bat if needed)
tools\sync_remastered.bat

:: Build companion (binkw32.dll) + native d3d9_spectacular.dll
build.bat

:: Assemble verified release zip under dist\
package-hybrid.bat
```

Details: [`docs/BUILD.md`](docs/BUILD.md)

## Features

- Visual pipeline: MXAO → SSR → FakeHDR → AL → Dehaze → DPX → LUT → Bloom → Deband → SMAA → CAS → SpectacularFinish  
- FPS menu 30/60/120/144/Unlimited · **F8** mission-safe 30 FPS  
- CityLife · LOD/AF/FOV · FogVolumetric · SteamCompat  
- Multi-locale FPS labels (surgical `igct_*.bnx` patch at runtime)

## License / credits

- Original companion code: [`LICENSE`](LICENSE) (MIT)  
- Third-party: [`NOTICE.txt`](NOTICE.txt) · [`CREDITS.txt`](CREDITS.txt) · [`LICENSE-RESHADE.txt`](LICENSE-RESHADE.txt)  
- Fan project — not affiliated with Activision, Marvel, Neversoft, TeaserPlay, or ReShade authors

## Changelog

See [`CHANGELOG.md`](CHANGELOG.md).
