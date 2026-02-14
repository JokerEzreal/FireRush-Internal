@echo off
setlocal

set "SRCDIR=%~dp0"
REM Strip trailing backslash so cmake quoting works
if "%SRCDIR:~-1%"=="\" set "SRCDIR=%SRCDIR:~0,-1%"
set "BUILDDIR=%SRCDIR%\build"

REM Find Visual Studio vcvarsall.bat
set "VCVARS="
set "NINJA="
for %%V in (
    "C:\Program Files\Microsoft Visual Studio\18\Community"
    "C:\Program Files\Microsoft Visual Studio\2022\Community"
    "C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
    "C:\Program Files\Microsoft Visual Studio\2022\Professional"
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community"
    "C:\Program Files (x86)\Microsoft Visual Studio\2019\Enterprise"
) do (
    if exist "%%~V\VC\Auxiliary\Build\vcvarsall.bat" (
        set "VCVARS=%%~V\VC\Auxiliary\Build\vcvarsall.bat"
        set "NINJA=%%~V\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
        goto :found
    )
)
echo [!] vcvarsall.bat not found. Install Visual Studio with C++ tools.
pause
exit /b 1

:found
echo [*] Using: %VCVARS%
echo [*] Setting up MSVC x86 environment...
call "%VCVARS%" x86 >nul 2>&1

if not exist "%BUILDDIR%" mkdir "%BUILDDIR%"
cd /d "%BUILDDIR%"

echo [*] CMake configure (Ninja / x86)...
cmake "%SRCDIR%" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_MAKE_PROGRAM=%NINJA%"
if errorlevel 1 (
    echo [!] CMake configure FAILED
    pause
    exit /b 1
)

echo [*] Building...
"%NINJA%"
if errorlevel 1 (
    echo [!] Build FAILED
    pause
    exit /b 1
)

echo [+] SUCCESS
echo [+] DLL:      %BUILDDIR%\firerush_overlay.dll
echo [+] Injector: %BUILDDIR%\injector.exe
pause
