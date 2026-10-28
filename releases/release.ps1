# release.ps1 - Build, deploy and package La Ideal in one command.
#
# Usage (from any PowerShell prompt - the Qt cmd shortcut is no longer required):
#   .\releases\release.ps1 8.1
#
# Pipeline:
#   1. Prepends Qt + MinGW to PATH for this process.
#   2. Validates the supplied version matches `project(laideal VERSION ...)` in CMakeLists.txt.
#   3. Refuses to overwrite an existing release (zip / installer / staging dir).
#   4. Configures CMake (Release, Ninja) into build-release/.
#   5. Builds with all available cores, then runs ctest (as ci.yml does).
#   6. Packages with releases/package.ps1 (the recipe the CI release job uses):
#      windeployqt + DLL check, build-release/<version>.zip, Inno Setup
#      build-release/laideal_setup_<version>.exe and its .sha256.
#
# The installer is written into build-release/ (gitignored, wiped on each run)
# rather than into a tracked releases/ subfolder, so the manual release flow
# never accumulates artifacts inside the repo's releases/ directory.

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true, Position=0)]
    [string]$Version
)

$ErrorActionPreference = 'Stop'

$ReleasesDir       = $PSScriptRoot
$RepoRoot          = Split-Path -Parent $ReleasesDir
$BuildDir          = Join-Path $RepoRoot 'build-release'
$StagingDir        = Join-Path $ReleasesDir $Version
$InstallerPath     = Join-Path $BuildDir "laideal_setup_$Version.exe"
$IssFile           = Join-Path $ReleasesDir 'laideal.iss'

$QtBinDir          = 'C:\Qt\6.4.3\mingw_64\bin'
$MingwBinDir       = 'C:\Qt\Tools\mingw1120_64\bin'
$CMakeBinDir       = 'C:\Qt\Tools\CMake_64\bin'
$NinjaBinDir       = 'C:\Qt\Tools\Ninja'
$InnoSetupCompiler = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'

function Step([string]$msg) {
    Write-Host ""
    Write-Host "==> $msg" -ForegroundColor Cyan
}
function Fail([string]$msg) {
    Write-Host ""
    Write-Host "FAIL: $msg" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $QtBinDir))          { Fail "Qt bin dir not found at $QtBinDir" }
if (-not (Test-Path $MingwBinDir))       { Fail "MinGW bin dir not found at $MingwBinDir" }
if (-not (Test-Path $CMakeBinDir))       { Fail "CMake bin dir not found at $CMakeBinDir" }
if (-not (Test-Path $NinjaBinDir))       { Fail "Ninja bin dir not found at $NinjaBinDir" }
if (-not (Test-Path $InnoSetupCompiler)) { Fail "Inno Setup compiler not found at $InnoSetupCompiler" }

# Prepend any of the four tool dirs that are not already on PATH. Previously this
# only checked $QtBinDir, which made the script silently skip the prefix when Qt
# was on PATH from another source while Ninja was not - cmake -G Ninja then
# failed with "unable to find a build program corresponding to Ninja".
foreach ($dir in @($NinjaBinDir, $CMakeBinDir, $MingwBinDir, $QtBinDir)) {
    if ($env:PATH -notlike "*$dir*") {
        $env:PATH = "$dir;$env:PATH"
    }
}

Step "Release pipeline for v$Version"

Step "Verifying CMakeLists.txt VERSION"
$cmakeContent = Get-Content (Join-Path $RepoRoot 'CMakeLists.txt') -Raw
if ($cmakeContent -notmatch 'project\s*\(\s*laideal\s+VERSION\s+([0-9]+\.[0-9]+)') {
    Fail "Could not find 'project(laideal VERSION X.Y)' in CMakeLists.txt"
}
$cmakeVersion = $matches[1]
if ($cmakeVersion -ne $Version) {
    Fail "Version mismatch: CMakeLists.txt has '$cmakeVersion', you passed '$Version'. Update CMakeLists.txt first."
}

if (Test-Path $StagingDir) { Fail "Already exists - delete first: $StagingDir" }

if (Test-Path $BuildDir) {
    Step "Cleaning build dir $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
}

Step "Configuring CMake (Release, Ninja)"
& cmake -S $RepoRoot -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release -DLAIDEAL_UI_TEST=OFF
if ($LASTEXITCODE -ne 0) { Fail "CMake configure failed" }

Step "Building"
& cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) { Fail "Build failed" }

Step "Running tests (ctest)"
& ctest --test-dir $BuildDir --output-on-failure --no-tests=error --timeout 120
if ($LASTEXITCODE -ne 0) { Fail "ctest failed - no release is built from a red suite" }

$builtExe = Join-Path $BuildDir 'src\app\laideal.exe'
if (-not (Test-Path $builtExe)) { Fail "Build succeeded but exe not at $builtExe" }

# Refuses a UI-test build and throws on any failure ($ErrorActionPreference = 'Stop').
Step "Packaging (releases\package.ps1, the same recipe as the CI release job)"
& (Join-Path $ReleasesDir 'package.ps1') -Exe $builtExe -Version $Version -OutDir $BuildDir -InnoSetupCompiler $InnoSetupCompiler
$ZipPath = Join-Path $BuildDir "$Version.zip"

Write-Host ""
Write-Host "Release v$Version ready:" -ForegroundColor Green
Write-Host "  ZIP:       $ZipPath"
Write-Host "  Installer: $InstallerPath"
Write-Host "  SHA-256:   $InstallerPath.sha256 (upload it with the installer)"