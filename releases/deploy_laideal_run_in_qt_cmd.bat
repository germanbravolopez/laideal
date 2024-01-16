@echo off

REM Get the full path of the current batch file
set "full_path=%~dp0"

REM Get the parent directory by changing the current directory and echoing the absolute path
pushd "%full_path%..\.."
set "parent_dir=%CD%"
popd

REM Set the source file and folder and check if it exists
set "sourceFolder=%parent_dir%\build-laideal-Desktop_Qt_6_4_3_MinGW_64_bit-Release"
set "sourceSubFolder=src\app"
set "fileName=laideal.exe"
if not exist "%sourceFolder%\%sourceSubFolder%\%fileName%" (
    echo Release folder or executable file does not exist.
    exit /b
)

REM Prompt the user to enter the name of the release name, concatenate with the destination folder
set /p "releaseName=Enter the release to do (r1.0, r2.0, ...): "
set "destinationFolder=C:\Users\gebra\work\tintoreria\laideal\releases\%releaseName%"
REM Check if folder exists, otherwise create it
if exist "%destinationFolder%" (
    echo Release already exists, check the folder: "%destinationFolder%".
    exit /b
)
echo Create destination folder.
mkdir "%destinationFolder%"
if errorlevel 1 (
    echo Failed to create destination folder.
    exit /b
)

REM Copy file to destination folder
xcopy "%sourceFolder%\%sourceSubFolder%\%fileName%" "%destinationFolder%" /C /I /Y

REM Add windeploypath to environmental variables
set "qtPath=C:\Qt\6.4.3\mingw_64\bin"

REM Check if the path already exists in the user's environmental variables
echo %PATH% | find /i "%qtPath%" > nul
if %errorlevel% equ 0 (
    echo Qt bin path already exists.
) else (
    echo Adding locally Qt bin to path...
    setx PATH "%PATH%;%qtPath%"
)

echo Run windeployqt.
cd %destinationFolder%
windeployqt %fileName%

echo Rename release folder to avoid generation of same release
ren "%sourceFolder%" "build-laideal-Desktop_Qt_6_4_3_MinGW_64_bit-Release_%releaseName%"

cd ..

REM Call the script to zip the released folder
call C:\Users\gebra\work\tintoreria\laideal\releases\zip_folder.bat %destinationFolder%

REM Call the script to update the iss file
call C:\Users\gebra\work\tintoreria\laideal\releases\find_replace.bat %releaseName%
