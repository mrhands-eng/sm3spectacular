@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem SM3 Spectacular Edition — safe installer
rem Run from the extracted package OR from the Game.exe folder after copying files.

set "SRC=%~dp0"
if "%SRC:~-1%"=="\" set "SRC=%SRC:~0,-1%"

set "DST="
if not "%~1"=="" set "DST=%~1"
if not defined DST if exist "%CD%\Game.exe" set "DST=%CD%"
if not defined DST if exist "%SRC%\Game.exe" set "DST=%SRC%"

if not defined DST (
  echo.
  echo SM3 Spectacular Edition installer
  echo.
  echo Could not find Game.exe in this folder.
  echo.
  echo Option A: copy ALL files from this package into your Spider-Man 3 folder
  echo           ^(next to Game.exe^), then run INSTALL.bat again from there.
  echo.
  echo Option B: type the full path to the folder that contains Game.exe
  set /p "DST=Game folder: "
)

if not exist "%DST%\Game.exe" (
  echo ERROR: Game.exe not found in:
  echo   %DST%
  exit /b 1
)

echo.
echo Installing SM3 Spectacular Edition into:
echo   %DST%
echo.

rem --- Conflict injectors that break hybrid ---
if exist "%DST%\version.dll" (
  echo Removing conflicting version.dll
  del /f /q "%DST%\version.dll" >nul 2>nul
)
if exist "%DST%\winmm.dll" (
  echo Removing conflicting winmm.dll
  del /f /q "%DST%\winmm.dll" >nul 2>nul
)
if exist "%DST%\d3d9_reshade.dll" (
  del /f /q "%DST%\d3d9_reshade.dll" >nul 2>nul
)

rem --- Bink: keep original once ---
if not exist "%DST%\binkw32_stock.dll" (
  if exist "%DST%\binkw32.dll" (
    echo Backing up original binkw32.dll -^> binkw32_stock.dll
    copy /Y "%DST%\binkw32.dll" "%DST%\binkw32_stock.dll" >nul
  )
)

rem --- Copy package payload if installer runs from package folder ---
if /I not "%SRC%"=="%DST%" (
  if exist "%SRC%\d3d9.dll" (
    echo Copying package files...
    copy /Y "%SRC%\d3d9.dll" "%DST%\d3d9.dll" >nul
    copy /Y "%SRC%\binkw32.dll" "%DST%\binkw32.dll" >nul
    if exist "%SRC%\d3d9_spectacular.dll" copy /Y "%SRC%\d3d9_spectacular.dll" "%DST%\d3d9_spectacular.dll" >nul
    if exist "%SRC%\libwinpthread-1.dll" copy /Y "%SRC%\libwinpthread-1.dll" "%DST%\libwinpthread-1.dll" >nul
    copy /Y "%SRC%\sm3spectacular.ini" "%DST%\sm3spectacular.ini" >nul
    copy /Y "%SRC%\ReShade.ini" "%DST%\ReShade.ini" >nul
    if not exist "%DST%\ReShadePreset.ini.stock.bak" if exist "%DST%\ReShadePreset.ini" (
      copy /Y "%DST%\ReShadePreset.ini" "%DST%\ReShadePreset.ini.stock.bak" >nul
    )
    copy /Y "%SRC%\ReShadePreset.ini" "%DST%\ReShadePreset.ini" >nul
    if exist "%DST%\reshade-shaders" rmdir /s /q "%DST%\reshade-shaders" >nul 2>nul
    mkdir "%DST%\reshade-shaders" >nul 2>nul
    robocopy "%SRC%\reshade-shaders" "%DST%\reshade-shaders" /E /NFL /NDL /NJH /NJS /nc /ns /np >nul
    if exist "%DST%\reshade-cache" rmdir /s /q "%DST%\reshade-cache" >nul 2>nul
    mkdir "%DST%\reshade-cache" >nul 2>nul
    for %%F in (README.txt README.md CHANGELOG.md NOTICE.txt CREDITS.txt LICENSE-RESHADE.txt RELEASE.txt VERSION READ_BINKW32_STOCK.txt) do (
      if exist "%SRC%\%%F" copy /Y "%SRC%\%%F" "%DST%\%%F" >nul
    )
    if exist "%SRC%\tools" (
      if not exist "%DST%\tools" mkdir "%DST%\tools" >nul
      copy /Y "%SRC%\tools\*.*" "%DST%\tools\" >nul 2>nul
    )
  ) else (
    echo WARNING: d3d9.dll not next to INSTALL.bat — assuming files already in game folder.
  )
) else (
  rem Already in game folder: ensure companion DLL is the package one (user copied zip contents).
  if not exist "%DST%\binkw32.dll" (
    echo ERROR: binkw32.dll missing. Copy package files into the game folder first.
    exit /b 1
  )
  if not exist "%DST%\d3d9.dll" (
    echo ERROR: d3d9.dll missing. Copy package files into the game folder first.
    exit /b 1
  )
  if not exist "%DST%\reshade-cache" mkdir "%DST%\reshade-cache" >nul
)

rem --- FPS menu labels: surgical patch, never replace whole locale ---
if exist "%DST%\pcinterface" (
  where python >nul 2>nul
  if not errorlevel 1 (
    if exist "%SRC%\tools\patch_igct_fps.py" (
      echo Patching FPS labels into igct_*.bnx ...
      python "%SRC%\tools\patch_igct_fps.py" "%DST%"
    ) else if exist "%DST%\tools\patch_igct_fps.py" (
      echo Patching FPS labels into igct_*.bnx ...
      python "%DST%\tools\patch_igct_fps.py" "%DST%"
    )
  ) else (
    echo Python not found — companion will patch igct_*.bnx on first launch.
  )
) else (
  echo NOTE: no pcinterface folder — FPS menu labels patch skipped ^(companion will try on launch^).
)

rem --- Clear leftover native-only trees that confuse hybrid ---
if exist "%DST%\textures" if not exist "%DST%\textures\.keep" rmdir /s /q "%DST%\textures" >nul 2>nul
if exist "%DST%\shaders" if not exist "%DST%\shaders\.keep" rmdir /s /q "%DST%\shaders" >nul 2>nul
if exist "%DST%\sm3spectacular_cache" rmdir /s /q "%DST%\sm3spectacular_cache" >nul 2>nul

echo.
echo ========================================
echo  Install OK
echo ========================================
echo.
echo 1. In-game Video: set MSAA / FSAA = Off
echo 2. Launch Game.exe ^(or via Steam — SteamCompat auto-adjusts visuals^)
echo 3. ReShade overlay: Home
echo 4. F8 = mission-safe 30 FPS
echo.
echo Do NOT replace binkw32_stock.dll — that is your original Bink.
echo Config: sm3spectacular.ini ^(Preset=Custom keeps your knobs^)
echo.
pause
endlocal
