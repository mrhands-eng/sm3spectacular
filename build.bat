@echo off
setlocal EnableExtensions
set PATH=C:\msys64\mingw32\bin;C:\Program Files\CMake\bin;%PATH%
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

rem Optional: set GAME_DIR before calling. Set SKIP_DEPLOY=1 to build only.
if not defined GAME_DIR set "GAME_DIR=D:\Projects\IdeaProjects\Spider-Man 3 - The Game"

if not exist "C:\msys64\mingw32\bin\g++.exe" (
  echo Need MinGW i686: pacman -S mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-ninja
  exit /b 1
)

if not exist "%ROOT%\vendor\remastered\d3d9_reshade.dll" (
  echo Syncing Remastered pack into vendor...
  call "%ROOT%\tools\sync_remastered.bat"
  if not exist "%ROOT%\vendor\remastered\d3d9_reshade.dll" (
    echo ERROR: vendor\remastered missing — run tools\sync_remastered.bat
    exit /b 1
  )
)

echo Building slim reshade pack...
python "%ROOT%\tools\slim_reshade_pack.py"
if errorlevel 1 exit /b 1

cmake -S "%ROOT%" -B "%ROOT%\build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=C:/msys64/mingw32/bin/g++.exe -DCMAKE_CXX_FLAGS=-finput-charset=UTF-8
if errorlevel 1 exit /b 1
cmake --build "%ROOT%\build" --target d3d9 binkw32
if errorlevel 1 exit /b 1

set "SHADER_SRC=%ROOT%\vendor\remastered\reshade-shaders-hybrid"
if not exist "%SHADER_SRC%\Shaders" set "SHADER_SRC=%ROOT%\vendor\remastered\reshade-shaders"

if /I not "%SKIP_DEPLOY%"=="1" if exist "%GAME_DIR%\Game.exe" (
  copy /Y "%ROOT%\vendor\remastered\d3d9_reshade.dll" "%GAME_DIR%\d3d9.dll" >nul
  copy /Y "%ROOT%\build\bin\d3d9.dll" "%GAME_DIR%\d3d9_spectacular.dll" >nul

  if not exist "%GAME_DIR%\binkw32_stock.dll" (
    if exist "%GAME_DIR%\binkw32.dll" copy /Y "%GAME_DIR%\binkw32.dll" "%GAME_DIR%\binkw32_stock.dll" >nul
  )
  copy /Y "%ROOT%\build\bin\binkw32.dll" "%GAME_DIR%\binkw32.dll" >nul

  copy /Y "%ROOT%\sm3spectacular.ini" "%GAME_DIR%\sm3spectacular.ini" >nul
  copy /Y "%ROOT%\build\bin\libwinpthread-1.dll" "%GAME_DIR%\libwinpthread-1.dll" >nul

  copy /Y "%ROOT%\vendor\remastered\ReShade.ini" "%GAME_DIR%\ReShade.ini" >nul
  copy /Y "%ROOT%\vendor\remastered\ReShadePreset.ini" "%GAME_DIR%\ReShadePreset.ini" >nul
  if exist "%GAME_DIR%\reshade-shaders" rmdir /s /q "%GAME_DIR%\reshade-shaders" >nul 2>nul
  mkdir "%GAME_DIR%\reshade-shaders" >nul 2>nul
  robocopy "%SHADER_SRC%" "%GAME_DIR%\reshade-shaders" /E /NFL /NDL /NJH /NJS /nc /ns /np >nul
  if exist "%GAME_DIR%\reshade-cache" rmdir /s /q "%GAME_DIR%\reshade-cache" >nul 2>nul
  mkdir "%GAME_DIR%\reshade-cache" >nul 2>nul

  if exist "%GAME_DIR%\pcinterface" (
    echo Patching FPS labels into all pcinterface\igct_*.bnx ...
    python "%ROOT%\tools\patch_igct_fps.py" "%GAME_DIR%"
  )

  if exist "%GAME_DIR%\d3d9_reshade.dll" del /f /q "%GAME_DIR%\d3d9_reshade.dll" >nul 2>nul
  if exist "%GAME_DIR%\version.dll" del /f /q "%GAME_DIR%\version.dll" >nul 2>nul
  if exist "%GAME_DIR%\winmm.dll" del /f /q "%GAME_DIR%\winmm.dll" >nul 2>nul
  if exist "%GAME_DIR%\textures" rmdir /s /q "%GAME_DIR%\textures" >nul 2>nul
  if exist "%GAME_DIR%\shaders" rmdir /s /q "%GAME_DIR%\shaders" >nul 2>nul
  if exist "%GAME_DIR%\sm3spectacular_cache" rmdir /s /q "%GAME_DIR%\sm3spectacular_cache" >nul 2>nul

  echo Deployed hybrid to: %GAME_DIR%
) else (
  if /I "%SKIP_DEPLOY%"=="1" (
    echo Skip deploy — SKIP_DEPLOY=1
  ) else (
    echo Skip deploy — GAME_DIR missing or no Game.exe: %GAME_DIR%
  )
)

echo.
echo Built: %ROOT%\build\bin\d3d9.dll + binkw32.dll
echo Slim shaders: %SHADER_SRC%
endlocal
