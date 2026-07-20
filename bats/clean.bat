@echo off
setlocal

set TARGET_DIR=build

if exist "%TARGET_DIR%" (
    echo [INFO] Removing %TARGET_DIR% directory...
    rmdir /s /q "%TARGET_DIR%"
    if %errorlevel% equ 0 (
        echo [SUCCESS] Clean completed.
    ) else (
        echo [ERROR] Failed to remove %TARGET_DIR%. Ensure no files are open.
    )
) else (
    echo [INFO] %TARGET_DIR% does not exist. Nothing to clean.
)

pause