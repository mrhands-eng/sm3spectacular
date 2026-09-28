# Changelog — SM3 Spectacular Edition

## [1.2.0] — Public package (2026-09-28)

- Package integrity check before zip (verify_package.py)
- Package no longer ships `igct_ru.bnx` as a full locale replace (multi-language safe)
- ASCII-safe `sm3spectacular.ini`; clear player README
- No INSTALL.bat in the zip (Nexus-safe — manual copy install only)

## 1.1.8 — Multi-locale FPS menu labels (2026-09-28)

- Companion auto-patches **every** `pcinterface\igct_*.bnx` (RU/EN/FR/…) with FPS keys — no full-file RU overwrite
- Installer/deploy uses surgical `tools/patch_igct_fps.py` instead of replacing the locale table
- Each Game.exe SKU hardcodes its own `igct_XX.bnx`; patching all covers other language versions

## 1.1.7 — Hybrid conflict closure (2026-09-28)

- Hybrid now wraps **device Reset** (MSAA Off + AF rebind on Video Apply / alt-tab) — was CreateDevice-only
- AF/mip hooks **skip callers from ReShade (`d3d9.dll`)** so effect passes keep POINT/LINEAR
- SteamCompat re-applied once after first CreateDevice (late overlay / Techniques race)
- SSR FOV aligned to companion **75°** (was 50° — wrong unprojection vs remaster FOV)

## 1.1.6 — Full-mod audit (non-visual) (2026-09-28)

- **Preset=Custom** default — Ultra no longer overwrites `TextureLodBias` / LOD / CityLife from ini
- **F8** toggles mission 30 FPS and syncs the Video menu highlight (`setPendingFpsLimit`)
- Defaults aligned with shipped ini: FogVolumetric on, ParticleScale 3600, softer mip −0.25, entity fade distances
- **SteamCompat=0** no longer rewrites Techniques; Steam/non-Steam writes only when the preset actually differs
- AF sampler hooks rebind after device recreate / Reset
- Native `d3d9` path also forces MSAA Off via `worldPreparePresentParams`
- Docs: igct_ru.bnx is RU-only; NOTICE/CMake version synced to 1.1.6

## 1.1.5 — Pipeline audit fixes (2026-09-28)

- Fixed effect order: **MXAO → SSR → HDR → AL → Dehaze → DPX → LUT → Bloom → Deband → SMAA → CAS → Finish**
- Removed **Unsharp** from defaults (conflicted with full-strength CAS) — CAS set to 0.55
- Steam-safe stack now keeps **AmbientLight + Dehaze** (no depth needed) + mild FilmicPass
- Softer mip bias (−0.25) so textures don’t over-crunch with sharpening
- Synced SpectacularFinish shader defaults with preset

## 1.1.4 — Slightly less saturation (2026-09-28)

- Softened Color pop / Hero chroma / DPX — still colorful, less loud

## 1.1.3 — Less mirror asphalt / no sand grain (2026-09-28)

- SSR intensity cut hard (~0.32) + tighter fresnel/fade — dry roads stop looking wet
- Film grain off; PD80 LUT dither off; bloom/AmbientLight dither reduced

## 1.1.2 — Kill midtone glow (2026-09-28)

- Concrete / walls no longer “emit light”: lower FakeHDR, AmbientLight, bloom threshold/mix
- SpectacularFinish: negative Exposure, warmth only on true highlights, deeper ink blacks
- Keeps Brand New Day color pop without the washed bright look

## 1.1.1 — Brand New Day color grade (2026-09-28)

- **SpectacularFinish** rebuilt for Spider-Man: Brand New Day cinema look
- Rich reds/blues (hero chroma), inky blacks, stronger vibrance + contrast — clearly visible
- Sun warmth / cool shadows split-tone; clearer dehaze; punchier DPX + bloom color
- Inspired by BND DP notes: saturated suit primaries + deep comic-ink shadows

## 1.1.0 — Spectacular identity complete (2026-09-28)

- **Single Cinetools LUT** by default (Bonus LUT optional) — clearer authored color
- **SpectacularFinish**: vignette, depth, grain, warmth + optional **2.39 letterbox**
- **Steam Spectacular** path: mild FilmicPass when MXAO/SSR are disabled
- Companion: **FogVolumetric=1** for atmospheric depth (set 0 for clear skyline)
- Keeps Remastered core look; polish is framing/film identity, not a regrade war

## 1.0.9 — Spectacular Finish style pass (2026-09-28)

- Added authored **SpectacularFinish** (last in chain): soft vignette, midtone depth, light grain, warm highlights
- Keeps the restored **1.0.0 Remastered grade** underneath — this is framing/film identity, not a regrade
- Design goal: give the mod a coherent “Spectacular Edition” look beyond stock TeaserPlay stacking

## 1.0.8 — Restore official 1.0.0 visuals (2026-09-28)

- Restored the **exact 1.0.0 release look** (Techniques + values from the dist archive)
- No FilmicPass in defaults; both PD80 LUTs back; original bloom / AmbientLight / DPX / fog
- Kept later stability fixes (MSAA off, CreateDevice race, Steam-safe path)
- FilmicPass / motion blur shaders still shipped but off
- Reference copy saved: `vendor/remastered/ReShadePreset.v1.0.0.ini`

## 1.0.7 — Richer color / depth (2026-09-28)

- Mid-ground between 1.0.5 (too harsh) and 1.0.6 (too flat)
- More color & contrast: LUT, DPX, FilmicPass (still no second LUT)
- Slightly deeper FakeHDR / AmbientLight / bloom / SSR — without neon overload

## 1.0.6 — Natural eye-comfort pass (2026-09-28)

- Retuned the whole visual stack for a calmer, more natural look
- Dropped **Bonus LUT** from defaults (kept optional) — no more double color grade
- Softened FilmicPass (bleach/strength), FakeHDR, DPX, Dehaze
- AmbientLight + PD80 bloom dialed down (less neon glow)
- CAS 0.45 + lighter Unsharp (less crunchy sharpening)
- Softer SSR reflections
- Companion: FogVolumetric back on, milder mip bias, fewer particles

## 1.0.5 — Bloom up, motion blur deferred (2026-09-28)

- **Motion blur off** for now (CinematicMotionBlur kept in pack, not in Techniques — revisit later)
- **PD80 bloom** stronger / more visible (higher mix, saturation, wider glow, lower threshold)

## 1.0.4 — Cinematic motion blur (2026-09-28)

- Added **CinematicMotionBlur** — per-pixel screen-space blur along moving content (objects/scene)
- **No mouse/camera-look blur** — streaks only where the image actually moved between frames
- Sharp when still; runs last after CAS
- Subtle defaults (shutter 0.40, max 18px)
- Old FakeMotionBlur remains optional/off

## 1.0.3 — Motion blur fix (2026-09-28)

- Motion blur **off by default** (same as TeaserPlay Remastered — it was never in their active Techniques)
- Fixed FakeMotionBlur permanent ghosting (`lerp(..., diff+0.1)` → `saturate(diff)`)
- Softer optional preset values if you enable it in ReShade (Home): mbRecall=0.07, mbSoftness=0.35
- Shader still shipped so players can turn MotionBlur on if they want

## 1.0.2 — Post-FX update (2026-09-28)

- Added motion blur + FilmicPass; bloom via PD80

## 1.0.1 — Hotfix (2026-09-28)

- Fixed broken `ReShadePreset.ini` in the public pack (effect tunings were wiped → flat/default look)
- Slim pack now always rebuilds preset from `ReShadePreset.full.ini` and **fails the build** if too few fx sections remain
- Package script refuses to zip a techniques-only preset
- Launch stability fixes (MSAA force-off, CreateDevice race, Steam-safe path)

## 1.0.0 — Official public release (2026-09-28)

First official public build of **SM3 Spectacular Edition** (hybrid stack).

- Hybrid: Remastered ReShade as `d3d9.dll` + Spectacular companion as `binkw32.dll`
- Visuals: FakeHDR, SSR, MXAO, AmbientLight, DeHaze, Unsharp, DPX, PD80 LUT/Bloom, Deband, SMAA, CAS
- Companion: FPS menu 30/60/120/144/Unlimited, **F8** mission-safe 30 FPS toggle
- CityLife, LOD/shadow boost, FOV + ultrawide aspect correction, fog/particles knobs
- Slim `reshade-shaders` (active techniques only)
- **No RTGI** / **No BloomingHDR** (license) — see NOTICE.txt
