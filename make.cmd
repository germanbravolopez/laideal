@echo off
REM make <target> from cmd.exe or PowerShell: runs make.ps1 (see .\make.ps1 help).
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make.ps1" %*
exit /b %ERRORLEVEL%
