@echo off
setlocal enabledelayedexpansion

:: ============================================================
:: convert_bg.bat  --  Bulk-convert PNG backgrounds to 640x480
::
:: Usage:
::   convert_bg.bat [input_folder] [mode]
::
::   input_folder  Folder containing PNG files (default: current dir)
::   mode          crop    Scale to cover 640x480 then center-crop (default)
::                 fit     Scale to fit inside 640x480, pad black bars
::                 stretch Stretch to exactly 640x480 (ignores aspect ratio)
::
:: Output is written to  <input_folder>\converted\
:: Requires ffmpeg.exe on PATH (or in the same folder as this script).
:: ============================================================

:: -- Locate ffmpeg -----------------------------------------------
set "FFMPEG=ffmpeg"
where ffmpeg >nul 2>&1
if errorlevel 1 (
    if exist "%~dp0ffmpeg.exe" (
        set "FFMPEG=%~dp0ffmpeg.exe"
    ) else (
        echo ERROR: ffmpeg not found on PATH or in the tools folder.
        echo Download from https://ffmpeg.org/download.html
        exit /b 1
    )
)

:: -- Arguments ---------------------------------------------------
set "INPUT_DIR=%~1"
if "%INPUT_DIR%"=="" set "INPUT_DIR=%CD%"

set "MODE=%~2"
if "%MODE%"=="" set "MODE=crop"

:: -- Validate mode -----------------------------------------------
if /i "%MODE%"=="crop"    goto :mode_ok
if /i "%MODE%"=="fit"     goto :mode_ok
if /i "%MODE%"=="stretch" goto :mode_ok
echo ERROR: Unknown mode "%MODE%". Use: crop  fit  stretch
exit /b 1
:mode_ok

:: -- Build ffmpeg -vf filter -------------------------------------
if /i "%MODE%"=="crop" (
    set "VF=scale=640:480:force_original_aspect_ratio=increase,crop=640:480"
    set "MODE_DESC=scale-to-cover then center-crop"
)
if /i "%MODE%"=="fit" (
    set "VF=scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:(ow-iw)/2:(oh-ih)/2:color=black"
    set "MODE_DESC=scale-to-fit with black bars"
)
if /i "%MODE%"=="stretch" (
    set "VF=scale=640:480"
    set "MODE_DESC=stretch to fill"
)

:: -- Output folder -----------------------------------------------
set "OUT_DIR=%INPUT_DIR%\converted"
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

:: -- Convert each PNG --------------------------------------------
set COUNT=0
set SKIP=0

for %%F in ("%INPUT_DIR%\*.png") do (
    set "FNAME=%%~nxF"
    set "FBASE=%%~nF"
    echo Converting: !FNAME!
    "%FFMPEG%" -y -i "%%F" -vf "!VF!" -pix_fmt rgb24 -compression_level 6 -update 1 "%OUT_DIR%\!FBASE!.png" -loglevel warning
    if errorlevel 1 (
        echo   FAILED: !FNAME!
        set /a SKIP+=1
    ) else (
        set /a COUNT+=1
    )
)

:: -- Summary -----------------------------------------------------
echo.
echo Done.  Mode: %MODE_DESC%
echo   Converted : %COUNT% file(s)  ->  %OUT_DIR%
if %SKIP% gtr 0 echo   Failed    : %SKIP% file(s)

endlocal
