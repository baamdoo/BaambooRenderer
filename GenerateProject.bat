@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0GenerateProject.ps1"
set "result=%errorlevel%"
if /I not "%~1"=="--no-pause" pause
exit /b %result%
