@echo off
setlocal
chcp 65001 >nul
set "ROOT=%~dp0"
set "ARGS="
if /I "%~1"=="--no-run" set "ARGS=-NoRun"
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%ROOT%tools\build.ps1" %ARGS%
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" pause
exit /b %RESULT%
