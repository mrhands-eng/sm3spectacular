SM3 Spectacular Edition v1.2.0 — by zryuyu
==========================================
Official public release

INSTALL (simple)
  1) Backup your game folder (optional but smart)
  2) Rename binkw32.dll -> binkw32_stock.dll  (once)
  3) Copy ALL files from this package into the Game.exe folder (merge)
  4) In-game: MSAA / FSAA = Off
  5) Launch

FPS MENU LABELS (all languages)
  Companion patches every pcinterface\igct_*.bnx on first launch
  (does not replace your whole language file with Russian).

WHAT YOU GET
  d3d9.dll                 Remastered ReShade visuals
  binkw32.dll              Companion (FPS / CityLife / LOD / FOV / AF / Steam)
  sm3spectacular.ini       Companion config (Preset=Custom — knobs stick)
  ReShade.ini + preset     Full visual stack + tunings
  reshade-shaders\         Slim shader pack + textures

CONTROLS
  Video menu FPS: 30 / 60 / 120 / 144 / Unlimited
  F8 = mission-safe 30 FPS (menu stays in sync)
  ReShade overlay = Home
  FogVolumetric=0 for clearer skyline
  CityLife=0 if a mission breaks
  SteamCompat=0 if you customize Techniques by hand

STEAM
  SteamCompat=1 (default): no MXAO/SSR under Steam; FilmicPass + Finish stay.
  Full stack: launch Game.exe outside Steam.

DO NOT
  - Delete binkw32_stock.dll
  - Enable MSAA/FSAA
  - Chain another ReShade / version.dll / winmm.dll injector

UNINSTALL
  Restore binkw32_stock.dll -> binkw32.dll
  Delete d3d9.dll, ReShade*, reshade-shaders, sm3spectacular.ini
  Restore igct_*.bnx.stock.bak if present
