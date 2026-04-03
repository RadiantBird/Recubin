call build\Release\Recubin.exe
if %errorlevel% neq 0 (
    echo Error: Recubin.exe exited with code %errorlevel%
) else (
    echo Recubin.exe executed successfully.
)