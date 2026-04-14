@echo off
echo [INFO] Ensuring DLL files are present...
if exist "dlls" (
    xcopy /Y /Q dlls\*.dll build\Release\ >nul 2>&1
)

call build\Release\Recubin.exe
call py watchSnake.py %errorlevel%