@echo off
setlocal
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "SRC=D:\Projects\IdeaProjects\Spider Man 3 Remastered"
set "DST=%ROOT%\vendor\remastered"

if not exist "%SRC%\d3d9.dll" (
  echo Remastered pack missing: %SRC%
  exit /b 1
)

mkdir "%DST%" 2>nul
copy /Y "%SRC%\d3d9.dll" "%DST%\d3d9_reshade.dll" >nul
copy /Y "%SRC%\ReShadePreset.ini" "%DST%\ReShadePreset.ini" >nul
powershell -NoProfile -Command ^
  "$i=Get-Content -LiteralPath '%SRC%\ReShade.ini' -Raw;" ^
  "$i=$i -replace 'IntermediateCachePath=.*','IntermediateCachePath=.\reshade-cache';" ^
  "$i=$i -replace 'SavePath=.*','SavePath=.\screenshots';" ^
  "Set-Content -LiteralPath '%DST%\ReShade.ini' -Value $i -NoNewline"
robocopy "%SRC%\reshade-shaders" "%DST%\reshade-shaders" /E /NFL /NDL /NJH /NJS /nc /ns /np >nul
echo Synced Remastered -^> %DST%
endlocal
