@echo off
REM Thin wrapper so cmd.exe (and the Qt cmd prompt) can run release.ps1.
REM Usage: release.bat ^<version^>     e.g.  release.bat 8.1

if "%~1"=="" (
    echo Usage: %~nx0 ^<version^>
    exit /b 1
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0release.ps1" %1
exit /b %ERRORLEVEL%