@echo off
setlocal

if not exist "build" (
    echo [INFO] Creating build directory...
    mkdir build
)

cmake -S . -B build -A x64 -D GLEW_STATIC=ON
cmake --build build --config Release

echo.
echo [INFO] Copying DLL files...
if exist "dlls" (
    xcopy /Y /Q dlls\*.dll build\Release\
    echo [SUCCESS] DLL files copied to build\Release\
) else (
    echo [WARNING] dlls folder not found
)

echo.
echo [SUCCESS] Build finished.