@echo off
setlocal
set "SAUCE_ENGINE_ROOT=%~dp0"
set "SAUCE_ENGINE_EXE=%~dp0SauceEngine.exe"
if not exist "%SAUCE_ENGINE_EXE%" (
    echo SauceEngine.exe was not found. Run build.bat first.
    pause
    exit /b 1
)
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -Command "Start-Process -FilePath $env:SAUCE_ENGINE_EXE -WorkingDirectory $env:SAUCE_ENGINE_ROOT -Verb RunAs"
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" pause
exit /b %RESULT%
