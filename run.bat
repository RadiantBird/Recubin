@echo off
call py.exe build.py run %*
if not %errorlevel%==9009 (
    exit /b %errorlevel%
)

call py build.py run %*
if not %errorlevel%==9009 (
    exit /b %errorlevel%
)

call python.exe build.py run %*
if not %errorlevel%==9009 (
    exit /b %errorlevel%
)

call python build.py run %*
if not %errorlevel%==9009 (
    exit /b %errorlevel%
)

call python3.exe build.py run %*
if not %errorlevel%==9009 (
    exit /b %errorlevel%
)

call python3 build.py run %*
if not %errorlevel%==9009 (
    exit /b %errorlevel%
)

echo [ERROR] Python launcher not found. Install py.exe, py, python.exe, python, python3.exe, or python3.
exit /b 9009
