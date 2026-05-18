@echo off
setlocal enabledelayedexpansion

set "search=1.0"
set "replace=%~1"

set "input_file=laideal.iss"
set "output_file=laideal_upd.iss"

if not exist "%input_file%" (
  echo %input_file% file does not exist.
  exit /b
)

if exist "%output_file%" (
  echo Delete %output_file% file before start.
  del "%output_file%"
)

for /f "usebackq delims=" %%a in ("%input_file%") do (
  set "line=%%a"
  setlocal enabledelayedexpansion
  set "line=!line:%search%=%replace%!"
  echo !line!>> "%output_file%"
  endlocal
)

echo Updated the file %input_file% from %search% to %replace% and save it to %output_file%.
