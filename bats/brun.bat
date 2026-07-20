@echo off

call py build.py brun %*
if not %errorlevel%==9009 (
    exit /b %errorlevel%
)

echo [ERROR] Python launcher not found. Install py.exe, py, python.exe, python, python3.exe, or python3.
exit /b 9009
