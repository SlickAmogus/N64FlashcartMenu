@echo off
:: convert_bgm.bat — Re-encode audio to 128 kbps MP3 for background music.
::
:: Requires ffmpeg on PATH.  Accepts any format ffmpeg can decode (.mp3, .flac,
:: .wav, .ogg, .aac, etc.) and writes a 128 kbps CBR MP3.
::
:: Usage:
::   tools\convert_bgm.bat <input> [output_dir]
::
:: If output_dir is omitted, the converted file is placed next to the input.
:: The original file is never modified — output goes to a temp file first,
:: then replaces the destination only after a successful encode.
::
:: Examples:
::   tools\convert_bgm.bat music\theme.flac  sd_card\menu\music
::   tools\convert_bgm.bat music\theme.mp3   sd_card\menu\music

setlocal EnableDelayedExpansion

if "%~1"=="" (
    echo Usage: %~nx0 ^<input^> [output_dir]
    echo Supported: any format ffmpeg can decode ^(.mp3 .wav .flac .ogg etc.^)
    exit /b 1
)

where ffmpeg >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: ffmpeg not found on PATH.
    echo Install ffmpeg from https://ffmpeg.org/download.html
    exit /b 1
)

set "INPUT_ABS=%~f1"
set "INPUT_NAME=%~n1"

if "%~2"=="" (
    set "OUTPUT_DIR=%~dp1"
) else (
    set "OUTPUT_DIR=%~f2"
)

if not exist "%OUTPUT_DIR%" (
    mkdir "%OUTPUT_DIR%"
)

set "OUTPUT_FILE=%OUTPUT_DIR%\%INPUT_NAME%.mp3"
set "TEMP_FILE=%OUTPUT_DIR%\%INPUT_NAME%.tmp.mp3"

echo Input : %INPUT_ABS%
echo Output: %OUTPUT_FILE%

:: Encode to a temp file so the source is never touched mid-encode.
ffmpeg -y -i "%INPUT_ABS%" -codec:a libmp3lame -b:a 128k -ar 44100 -ac 2 "%TEMP_FILE%"

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: ffmpeg conversion failed.
    if exist "%TEMP_FILE%" del "%TEMP_FILE%"
    exit /b 1
)

:: Rename temp to final destination (overwrites if it already exists).
move /Y "%TEMP_FILE%" "%OUTPUT_FILE%" >nul

echo Done: %OUTPUT_FILE%
