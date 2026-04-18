@echo off
setlocal

if not exist "build" (
    echo [INFO] Creating build directory...
    mkdir build
)

:: Clear the previous errorlevel
set errorlevel=0

cmake -S . -B build -A x64 -D GLEW_STATIC=ON
if %errorlevel% neq 0 (
    echo [ERROR] CMake configuration failed.
    exit /b %errorlevel%
)

cmake --build build --config Release --parallel
if %errorlevel% neq 0 (
    echo [ERROR] Build execution failed.
    exit /b %errorlevel%
)

echo.
echo [INFO] Copying DLL files...
if exist "dlls" (
    xcopy /Y /Q "dlls\*.dll" "build\Release\"
    echo [SUCCESS] DLL files copied.
) else (
    echo [WARNING] dlls folder missing.
)

echo.
echo [SUCCESS] Build process completed.