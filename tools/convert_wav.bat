@echo off
:: convert_wav.bat — Convert WAV files to .wav64 format using the n64menu-dev Docker image.
::
:: Usage:
::   tools\convert_wav.bat <input.wav> [output_dir]
::
:: If output_dir is omitted the converted file is placed next to the input file.
::
:: Example:
::   tools\convert_wav.bat assets\cursor.wav sd_card\menu\effects
::
:: The output file will have the same base name with a .wav64 extension.

setlocal EnableDelayedExpansion

if "%~1"=="" (
    echo Usage: %~nx0 ^<input.wav^> [output_dir]
    exit /b 1
)

set "INPUT=%~1"
set "INPUT_ABS=%~f1"
set "INPUT_DIR=%~dp1"
set "INPUT_NAME=%~n1"

if "%~2"=="" (
    set "OUTPUT_DIR=%INPUT_DIR%"
) else (
    set "OUTPUT_DIR=%~f2"
)

:: Convert Windows paths to forward-slash paths for Docker
set "INPUT_DOCKER=%INPUT_ABS:\=/%"
set "OUTPUT_DOCKER=%OUTPUT_DIR:\=/%"

:: Strip drive letter (e.g. C:/ -> /)
set "INPUT_DOCKER=/%INPUT_DOCKER:~3%"
set "OUTPUT_DOCKER=/%OUTPUT_DOCKER:~3%"

echo Converting: %INPUT_ABS%
echo Output dir: %OUTPUT_DIR%

docker run --rm ^
    -v "%INPUT_DIR%:/input" ^
    -v "%OUTPUT_DIR%:/output" ^
    n64menu-dev ^
    bash -c "export N64_INST=/opt/libdragon && /opt/libdragon/bin/audioconv64 --wav-compress 1 -o /output /input/%INPUT_NAME%.wav"

if %ERRORLEVEL%==0 (
    echo Done: %OUTPUT_DIR%\%INPUT_NAME%.wav64
) else (
    echo ERROR: conversion failed.
    exit /b %ERRORLEVEL%
)
