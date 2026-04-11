@echo off
call build\Release\Recubin.exe
call py watchSnake.py %errorlevel%