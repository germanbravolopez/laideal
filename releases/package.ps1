# package.ps1 - Turn a built laideal.exe into the shipped files. The one packaging
# recipe, used by the ci.yml release and ui-smoke jobs and by release.ps1.
#
# Usage:
#   .\releases\package.ps1 -Exe build\src\app\laideal.exe -Version 12.4 -OutDir build-release
#
# Needs windeployqt (Qt 6.4.3 bin) and the MinGW 11.2 bin on PATH, and Inno Setup 6.
# Produces in OutDir: <Version>.zip, laideal_setup_<Version>.exe and its .sha256.
# A UI-test build (--fake-aeat) is refused unless -AllowUiTest (the UI smoke test).

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string]$Exe,
    [Parameter(Mandatory = $true)] [string]$Version,
    [Parameter(Mandatory = $true)] [string]$OutDir,
    [switch]$AllowUiTest,
    [string]$InnoSetupCompiler = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
)

$ErrorActionPreference = 'Stop'

# laideal.iss takes its files from releases\<Version>\.
$staging = Join-Path $PSScriptRoot $Version
if (Test-Path $staging) { throw "Already exists - delete first: $staging" }
if (-not (Test-Path $Exe)) { throw "No exe at $Exe" }
if (-not (Test-Path $InnoSetupCompiler)) { throw "Inno Setup compiler not found at $InnoSetupCompiler" }
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

$marker = [BitConverter]::ToString([Text.Encoding]::Unicode.GetBytes('--fake-aeat'))
if (-not $AllowUiTest -and [BitConverter]::ToString([IO.File]::ReadAllBytes($Exe)).Contains($marker)) {
    throw "laideal.exe is a UI-test build (--fake-aeat): it must never be released."
}

try {
    New-Item -ItemType Directory -Path $staging | Out-Null
    Copy-Item $Exe -Destination $staging

    # Do NOT pass --release: on MinGW Qt windeployqt then mistakes the release plugins
    # for debug ones and aborts with "Unable to find the platform plugin".
    & windeployqt --no-translations --no-system-d3d-compiler (Join-Path $staging 'laideal.exe')
    if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }
    # Without these the installed app does not start on a clean machine, has no
    # database (SQLite driver) or no HTTPS (AEAT, gateway, updater).
    foreach ($d in @('libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll', 'Qt6Core.dll',
                     'platforms\qwindows.dll', 'sqldrivers\qsqlite.dll', 'tls\qschannelbackend.dll')) {
        if (-not (Test-Path (Join-Path $staging $d))) { throw "windeployqt did not deploy '$d'" }
    }

    $zip = Join-Path $OutDir "$Version.zip"
    if (Test-Path $zip) { Remove-Item $zip }
    Add-Type -AssemblyName 'System.IO.Compression.FileSystem'
    [IO.Compression.ZipFile]::CreateFromDirectory($staging, $zip)

    & $InnoSetupCompiler "/DMyAppVersion=$Version" "/O$OutDir" (Join-Path $PSScriptRoot 'laideal.iss')
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup compile failed" }
    $installer = Join-Path $OutDir "laideal_setup_$Version.exe"
    if (-not (Test-Path $installer)) { throw "Installer not produced at $installer" }

    # The updater runs the installer only when its SHA-256 matches this file.
    $hash = (Get-FileHash $installer -Algorithm SHA256).Hash.ToLower()
    [IO.File]::WriteAllText("$installer.sha256", "$hash  laideal_setup_$Version.exe`n")
}
finally {
    if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
}

Write-Host "Packaged $Version in ${OutDir}: $Version.zip, laideal_setup_$Version.exe (+ .sha256)"
