@echo off
setlocal
cd /d "%~dp0"
set "TRADETOGETHER_BUILD_DIR=out\build-release"

if "%VCPKG_ROOT%"=="" set "VCPKG_ROOT=C:\dev\vcpkg"

if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
    echo [TradeTogether] vcpkg introuvable dans: %VCPKG_ROOT%
    echo Definis VCPKG_ROOT ou modifie build_release.bat.
    exit /b 1
)

if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    echo [TradeTogether] vcpkg.exe introuvable dans: %VCPKG_ROOT%
    exit /b 1
)

echo [TradeTogether] Synchronisation de la baseline vcpkg locale...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\ensure_vcpkg_baseline.ps1" -VcpkgRoot "%VCPKG_ROOT%"
if errorlevel 1 exit /b %errorlevel%

echo [TradeTogether] Configuration CMake...
cmake -S . -B "%TRADETOGETHER_BUILD_DIR%" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if errorlevel 1 exit /b %errorlevel%

echo [TradeTogether] Compilation Release...
cmake --build "%TRADETOGETHER_BUILD_DIR%" --config Release
if errorlevel 1 exit /b %errorlevel%

echo.
echo [TradeTogether] Build termine.
echo DLL: package\Data\SKSE\Plugins\TradeTogether.dll

echo.
call "%~dp0make_vortex_archive.bat"
if errorlevel 1 exit /b %errorlevel%

echo.
echo [TradeTogether] Tout est pret.
echo Archive Vortex: dist\TradeTogether-v0.9.1-strpm-Vortex.zip
endlocal
