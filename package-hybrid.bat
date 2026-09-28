@echo off
setlocal EnableExtensions
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

rem Build + assemble official publishable package (no game install required).
set "SKIP_DEPLOY=1"
call "%ROOT%\build.bat"
if errorlevel 1 exit /b 1

set /p VER=<"%ROOT%\VERSION"
set "VER=%VER: =%"
if "%VER%"=="" set "VER=1.0.0"

set "OUT=%ROOT%\dist\SM3-Spectacular-Edition-v%VER%"
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%" >nul
mkdir "%OUT%\reshade-shaders" >nul
mkdir "%OUT%\reshade-cache" >nul

copy /Y "%ROOT%\vendor\remastered\d3d9_reshade.dll" "%OUT%\d3d9.dll" >nul
copy /Y "%ROOT%\build\bin\binkw32.dll" "%OUT%\binkw32.dll" >nul
copy /Y "%ROOT%\build\bin\libwinpthread-1.dll" "%OUT%\libwinpthread-1.dll" >nul
rem Native d3d9_spectacular.dll intentionally NOT in public Nexus zip (extra PE;
rem hybrid uses Remastered as d3d9.dll only). Keep it in repo/build for advanced users.
copy /Y "%ROOT%\sm3spectacular.ini" "%OUT%\sm3spectacular.ini" >nul
copy /Y "%ROOT%\vendor\remastered\ReShade.ini" "%OUT%\ReShade.ini" >nul
copy /Y "%ROOT%\vendor\remastered\ReShadePreset.ini" "%OUT%\ReShadePreset.ini" >nul
copy /Y "%ROOT%\README.txt" "%OUT%\README.txt" >nul
copy /Y "%ROOT%\README.md" "%OUT%\README.md" >nul
copy /Y "%ROOT%\CHANGELOG.md" "%OUT%\CHANGELOG.md" >nul
copy /Y "%ROOT%\NOTICE.txt" "%OUT%\NOTICE.txt" >nul
copy /Y "%ROOT%\CREDITS.txt" "%OUT%\CREDITS.txt" >nul
copy /Y "%ROOT%\LICENSE-RESHADE.txt" "%OUT%\LICENSE-RESHADE.txt" >nul
copy /Y "%ROOT%\RELEASE.txt" "%OUT%\RELEASE.txt" >nul
copy /Y "%ROOT%\VERSION" "%OUT%\VERSION" >nul

rem Do NOT ship igct_ru.bnx / tools/*.py / INSTALL.bat in the public zip:
rem Nexus scanners quarantine scripts+DLL injectors; companion patches igct at runtime.

set "SHADER_SRC=%ROOT%\vendor\remastered\reshade-shaders-hybrid"
if not exist "%SHADER_SRC%\Shaders" set "SHADER_SRC=%ROOT%\vendor\remastered\reshade-shaders"
robocopy "%SHADER_SRC%" "%OUT%\reshade-shaders" /E /NFL /NDL /NJH /NJS /nc /ns /np >nul

> "%OUT%\READ_BINKW32_STOCK.txt" echo Rename your ORIGINAL game binkw32.dll to binkw32_stock.dll before copying this package's binkw32.dll. Or just run INSTALL.bat.

echo Verifying package...
python "%ROOT%\tools\verify_package.py" "%OUT%"
if errorlevel 1 exit /b 1

set "ZIP=%ROOT%\dist\SM3-Spectacular-Edition-v%VER%.zip"
if exist "%ZIP%" del /f /q "%ZIP%"
rem Nexus preview scanner rejects some PowerShell Compress-Archive zips — use 7-Zip.
"C:\Program Files\7-Zip\7z.exe" a -tzip -mx=9 "%ZIP%" "%OUT%\*" >nul
if errorlevel 1 exit /b 1

echo.
echo ========================================
echo  PUBLISH READY
echo ========================================
echo Package: %OUT%
echo Zip:     %ZIP%
for %%I in ("%ZIP%") do echo Size:    %%~zI bytes
echo.
dir /b "%OUT%"
endlocal
