@echo off
rem Builds src\echo_server.cpp into assets\echo_server.exe.
rem If cl.exe is not on PATH, this looks for Visual Studio's vcvars64.bat and loads it.
rem (Keep this file ASCII-only: cmd.exe reads it with the system code page.)
setlocal
cd /d "%~dp0"

where cl >nul 2>nul
if errorlevel 1 (
    for %%V in (
        "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
        "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
    ) do (
        if exist %%V (
            call %%V >nul
            goto :compile
        )
    )
    echo cl.exe not found. Run this from a Developer Command Prompt for Visual Studio.
    exit /b 1
)

:compile
if not exist assets mkdir assets
if not exist obj mkdir obj
rem /utf-8: the source contains Japanese comments (the default CP932 breaks parsing).
cl /nologo /utf-8 /std:c++17 /EHsc /O2 /Fo:obj\ /Fe:assets\echo_server.exe src\echo_server.cpp
if errorlevel 1 exit /b 1
echo Built assets\echo_server.exe
