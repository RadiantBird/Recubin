call build\Debug\Recubin.exe
if %errorlevel% neq 0 (
    echo Error: main.exe exited with code %errorlevel%
) else (
    echo main.exe executed successfully.
)