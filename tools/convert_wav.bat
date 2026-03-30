@echo off
:: convert_wav.bat — Convert audio files to .wav64 format using the n64menu-dev Docker image.
::
:: Supports .wav and .ogg input (and any format ffmpeg can decode, e.g. .mp3, .flac).
:: OGG and other non-WAV formats are first decoded to a temporary WAV via ffmpeg,
:: then passed to audioconv64.  ffmpeg must be on your PATH for non-WAV inputs.
::
:: Usage:
::   tools\convert_wav.bat <input> [output_dir]
::
:: If output_dir is omitted the converted file is placed next to the input file.
::
:: Examples:
::   tools\convert_wav.bat assets\cursor.wav  sd_card\menu\effects
::   tools\convert_wav.bat assets\cursor.ogg  sd_card\menu\effects

setlocal EnableDelayedExpansion

if "%~1"=="" (
    echo Usage: %~nx0 ^<input^> [output_dir]
    echo Supported: .wav .ogg .mp3 .flac ^(any format ffmpeg can decode^)
    exit /b 1
)

:: Resolve the workspace root (one level up from this script)
set "SCRIPT_DIR=%~dp0"
set "WORKSPACE=%SCRIPT_DIR:~0,-7%"

set "INPUT_ABS=%~f1"
set "INPUT_DIR=%~dp1"
set "INPUT_NAME=%~n1"
set "INPUT_EXT=%~x1"

if "%~2"=="" (
    set "OUTPUT_DIR=%INPUT_DIR%"
) else (
    set "OUTPUT_DIR=%~f2"
)

echo Input : %INPUT_ABS%
echo Output: %OUTPUT_DIR%

:: -----------------------------------------------------------------------
:: For non-WAV formats, use ffmpeg to decode to a temporary WAV first.
:: -----------------------------------------------------------------------
set "TEMP_WAV="
set "CONVERT_DIR=%INPUT_DIR%"
set "CONVERT_NAME=%INPUT_NAME%"

if /I NOT "%INPUT_EXT%"==".wav" (
    where ffmpeg >nul 2>&1
    if !ERRORLEVEL! NEQ 0 (
        echo ERROR: ffmpeg not found on PATH.
        echo Install ffmpeg from https://ffmpeg.org/download.html or convert to .wav manually.
        exit /b 1
    )

    set "TEMP_WAV=%TEMP%\%INPUT_NAME%.wav"
    echo Decoding via ffmpeg...
    ffmpeg -y -i "%INPUT_ABS%" -ar 44100 -ac 2 -sample_fmt s16 "!TEMP_WAV!" 2>&1
    if !ERRORLEVEL! NEQ 0 (
        echo ERROR: ffmpeg decode failed.
        exit /b 1
    )

    set "CONVERT_DIR=%TEMP%\"
    set "CONVERT_NAME=%INPUT_NAME%"
)

:: -----------------------------------------------------------------------
:: Run audioconv64 inside Docker.
:: audioconv64 is built from source and needs 'make tools-install' to copy
:: it into /opt/libdragon/bin inside the container — this is fast (no compile).
:: -----------------------------------------------------------------------
set "WORKSPACE_DOCKER=%WORKSPACE:\=/%"
set "CONV_DIR_DOCKER=!CONVERT_DIR:\=/!"
set "OUTPUT_DOCKER=%OUTPUT_DIR:\=/%"

:: Strip drive letters (C:/ -> /)
set "WORKSPACE_DOCKER=/%WORKSPACE_DOCKER:~3%"
set "CONV_DIR_DOCKER=/!CONV_DIR_DOCKER:~3!"
set "OUTPUT_DOCKER=/%OUTPUT_DOCKER:~3%"

docker run --rm ^
    -v "%WORKSPACE%:/workspace" ^
    -v "!CONVERT_DIR!:/input" ^
    -v "%OUTPUT_DIR%:/output" ^
    -w /workspace ^
    n64menu-dev ^
    bash -c "export N64_INST=/opt/libdragon && cd libdragon && make tools-install -j -s 2>/dev/null; /opt/libdragon/bin/audioconv64 --wav-compress 1 -o /output /input/!CONVERT_NAME!.wav"

set RESULT=%ERRORLEVEL%

if defined TEMP_WAV (
    if exist "!TEMP_WAV!" del "!TEMP_WAV!"
)

if %RESULT%==0 (
    echo Done: %OUTPUT_DIR%\%INPUT_NAME%.wav64
) else (
    echo ERROR: audioconv64 failed.
    exit /b %RESULT%
)
