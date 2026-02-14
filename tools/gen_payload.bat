@echo off
echo === Generating FireRush Payload ===

set CSC=C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe
set MANAGED_DIR=C:\Users\Administrator\Documents\firerush\firerush_Data\Managed

REM Unity 4.x uses a single UnityEngine.dll
set UNITY_DLL=%MANAGED_DIR%\UnityEngine.dll

if not exist "%CSC%" (
    echo [!] C# compiler not found at: %CSC%
    echo [!] Trying Framework64 variant...
    set CSC=C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe
)

if not exist "%CSC%" (
    echo [!] C# compiler not found. Install .NET Framework 4.x SDK.
    pause
    exit /b 1
)

if not exist "%UNITY_DLL%" (
    echo [!] UnityEngine.dll not found at: %UNITY_DLL%
    echo [!] Please set the correct MANAGED_DIR in this script.
    pause
    exit /b 1
)

echo [*] Using managed DLLs from: %MANAGED_DIR%
echo [*] Compiling Payload.cs...
"%CSC%" /target:library /reference:"%UNITY_DLL%" /out:Payload.dll ..\payload\Payload.cs
if errorlevel 1 (
    echo [!] Compilation failed.
    pause
    exit /b 1
)

echo [*] Generating payload_bytes.h...
python bin2header.py Payload.dll > ..\src\payload\payload_bytes.h
if errorlevel 1 (
    echo [!] Header generation failed.
    pause
    exit /b 1
)

echo [+] Done! payload_bytes.h has been updated.
del Payload.dll 2>nul
pause
