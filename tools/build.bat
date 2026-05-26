@echo off
:: build.bat — Build sc64menu.n64 using the n64menu-dev Docker container.
::
:: Usage: tools\build.bat
::   Then choose (I)ncremental or (F)ull when prompted.
::
::   Incremental — recompiles only changed files (~10 sec).  Use this normally.
::   Full        — clobbers and rebuilds everything from scratch (several min).
::                 Use after updating the libdragon submodule or if the build
::                 is in a broken state.

setlocal

:: Check Docker is reachable before doing anything else.
docker info >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Docker is not running or not reachable.
    echo Start Docker Desktop and try again.
    exit /b 1
)

echo.
echo  SlickAmogus N64 Menu Build
echo  --------------------------
echo  [I] Incremental  - fast, recompiles changed files only
echo  [F] Full rebuild - slow, clobbers everything first
echo  [Q] Quit
echo.

:prompt
set /p "CHOICE=Select (I/F/Q): "

if /I "%CHOICE%"=="I" goto incremental
if /I "%CHOICE%"=="F" goto full
if /I "%CHOICE%"=="Q" goto quit
echo Invalid choice. Please enter I, F, or Q.
goto prompt

:: ---------------------------------------------------------------------------
:incremental
echo.
echo [Incremental build]
docker run --rm ^
    -v "%~dp0..:/workspace" ^
    -w /workspace ^
    n64menu-dev ^
    bash -c "export N64_INST=/opt/libdragon && cd libdragon && make install tools-install -j -s 2>/dev/null; cd /workspace && make all 2>&1"
goto done

:: ---------------------------------------------------------------------------
:full
echo.
echo [Full rebuild — this will take several minutes]
docker run --rm ^
    -v "%~dp0..:/workspace" ^
    -w /workspace ^
    n64menu-dev ^
    bash -c "export N64_INST=/opt/libdragon && cd libdragon && make clobber -j && make libdragon tools -j && make install tools-install -j && cd /workspace && make all 2>&1"
goto done

:: ---------------------------------------------------------------------------
:done
if %ERRORLEVEL%==0 (
    echo.
    echo Build successful: output\sc64menu.n64
) else (
    echo.
    echo Build FAILED. See output above for errors.
    exit /b 1
)

:quit
endlocal
