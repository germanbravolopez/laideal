# release.ps1 - Build, deploy and package La Ideal in one command.
#
# Usage (from any PowerShell prompt - the Qt cmd shortcut is no longer required):
#   .\releases\release.ps1 8.1
#
# Pipeline:
#   1. Prepends Qt + MinGW to PATH for this process.
#   2. Validates the supplied version matches `project(laideal VERSION ...)` in CMakeLists.txt.
#   3. Refuses to overwrite an existing release (zip / installer / staging dir).
#   4. Configures CMake (Release, MinGW Makefiles) into build-release/.
#   5. Builds with all available cores.
#   6. Stages laideal.exe into releases/<version>/ and runs windeployqt.
#   7. Runs Inno Setup ISCC.exe -> build-release/laideal_setup_<version>.exe.
#   8. Deletes the staging dir; leaves the installer in build-release/.
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
& cmake -S $RepoRoot -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { Fail "CMake configure failed" }

Step "Building"
& cmake --build $BuildDir
if ($LASTEXITCODE -ne 0) { Fail "Build failed" }

$builtExe = Join-Path $BuildDir 'src\app\laideal.exe'
if (-not (Test-Path $builtExe)) { Fail "Build succeeded but exe not at $builtExe" }

Step "Staging release in $StagingDir"
New-Item -ItemType Directory -Path $StagingDir | Out-Null
Copy-Item $builtExe -Destination $StagingDir

Step "Running windeployqt"
$stagedExe = Join-Path $StagingDir 'laideal.exe'
# Do NOT pass --release. windeployqt auto-detects the build type from the .exe PE
# header, and on MinGW Qt installs it mis-identifies the release plugin DLLs (e.g.
# plugins/platforms/qwindows.dll) as "debug" via its heuristic. With --release set
# explicitly it then filters those out and aborts with "Unable to find the platform
# plugin." Letting it auto-detect deploys the correct plugins.
& windeployqt --no-translations --no-system-d3d-compiler $stagedExe
if ($LASTEXITCODE -ne 0) { Fail "windeployqt failed" }

Step "Compiling installer (Inno Setup)"
# /O overrides the .iss OutputDir so the installer lands in build-release\ (wiped
# each run, gitignored) instead of accumulating inside releases\.
& $InnoSetupCompiler "/DMyAppVersion=$Version" "/O$BuildDir" $IssFile
if ($LASTEXITCODE -ne 0) { Fail "Inno Setup compile failed" }
if (-not (Test-Path $InstallerPath)) { Fail "Installer not produced at $InstallerPath" }

Step "Cleaning up staging"
Remove-Item -Recurse -Force $StagingDir

Write-Host ""
Write-Host "Release v$Version ready:" -ForegroundColor Green
Write-Host "  Installer: $InstallerPath"