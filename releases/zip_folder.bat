@echo off

set "destinationFolder=%~1"

REM Zip folder
set zipPath="%destinationFolder%.zip"
echo Zipping folder %destinationFolder%...
powershell -noprofile -command "Add-Type -A 'System.IO.Compression.FileSystem'; [IO.Compression.ZipFile]::CreateFromDirectory('%destinationFolder%', '%zipPath%');"

if %errorlevel% equ 0 (
    echo "%destinationFolder%" has been zipped successfully.
) else (
    echo An error occurred while zipping "%destinationFolder%".
)

