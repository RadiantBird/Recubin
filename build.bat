@echo off
setlocal

if not exist "build" (
    echo [INFO] Creating build directory...
    mkdir build
)

cmake -S . -B build -D GLEW_STATIC=ON
cmake --build build --config Release

echo.
echo [SUCCESS] Build finished.