@echo off
setlocal enabledelayedexpansion

:: ============================================================
:: rename_bg.bat  --  Rename all PNGs in a folder to bg1, bg2, ...
::
:: Usage:
::   rename_bg.bat [folder]
::
::   folder  Folder containing PNG files (default: current dir)
:: ============================================================

set "TARGET=%~1"
if "%TARGET%"=="" set "TARGET=%CD%"

set COUNT=0

for %%F in ("%TARGET%\*.png") do (
    set /a COUNT+=1
    ren "%%F" "bg!COUNT!.png"
)

echo Renamed %COUNT% file(s) in %TARGET%

endlocal
