# make.ps1 - the everyday build and test commands of La Ideal in one place (the
# Windows equivalent of a Makefile). It only wraps the commands README.md documents:
# it puts the Qt toolchain on PATH for itself, so no set-up line is needed first.
#
#   .\make.ps1                 the list of targets
#   .\make.ps1 test            build, then every test suite
#   .\make.ps1 regression      everything CI runs, on this PC
#
# From cmd.exe: make <target>   (make.cmd)
#
# .github\workflows\ci.yml runs these same targets. There the toolchain comes from
# .github\actions\setup-toolchain (PATH and the CXX variable), not from $ToolDirs.

[CmdletBinding()]
param(
    [Parameter(Position = 0)] [string]$Target = 'help',
    [string]$Filter,          # test: only the suites matching this regex (ctest -R)
    [int]$Jobs = 4,           # test: suites run in parallel
    [string]$Blocks,          # ui-smoke: only these blocks, e.g. AB
    [string]$Printer,         # ui-smoke: printer queue for block C (tests\ui\printer.py install)
    [string]$Settings,        # aeat-real: settings file with the issuer and its certificate
    [string]$Exe,             # ui-smoke / aeat-dry: an installed UI-test app, instead of build-uitest
    [string]$Tools,           # ui-smoke / aeat-dry: the folder of fake_aeat_server.exe and escpos_preview.exe
    [string]$Installer,       # ui-smoke: laideal_setup_X.Y.exe, for the reinstall step E4
    [string]$Out,             # ui-smoke / aeat-dry / aeat-real: the results folder
    [switch]$Force            # clean-all / aeat-real: do not ask
)

$ErrorActionPreference = 'Stop'
$Root = $PSScriptRoot

$ToolDirs = @(
    'C:\Qt\Tools\Ninja',
    'C:\Qt\Tools\CMake_64\bin',
    'C:\Qt\Tools\mingw1120_64\bin',
    'C:\Qt\6.4.3\mingw_64\bin'
)
$QtPrefix   = 'C:/Qt/6.4.3/mingw_64'
$VenvPython = Join-Path $Root '.venv\Scripts\python.exe'
$UiTestExe  = Join-Path $Root 'build-uitest\src\app\laideal.exe'
$UiTools    = Join-Path $Root 'build-uitest\tests'
$BuildDirs  = @('build', 'build-debug', 'build-uitest', 'build-release')
$OnGitHub   = $env:GITHUB_ACTIONS -eq 'true'

function Use-Toolchain {
    if ($OnGitHub) { return }
    foreach ($dir in $ToolDirs) {
        if (-not (Test-Path $dir)) { throw "Toolchain folder not found: $dir (see README.md, Build)" }
    }
    $env:PATH = ($ToolDirs -join ';') + ';' + $env:PATH
}

# Runs a native command, echoed first; a non-zero exit code stops the script.
function Invoke-Step {
    param([string]$Exe, [string[]]$Arguments)
    Write-Host ">> $Exe $($Arguments -join ' ')" -ForegroundColor Cyan
    # Only the exit code decides: under 'Stop', a tool's text on stderr (a pip notice) would end the run.
    $ErrorActionPreference = 'Continue'
    & $Exe @Arguments
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($code -ne 0) { throw "$Exe failed with exit code $code" }
}

# Configures the tree when it has no build.ninja (new, or left half-configured), then builds.
function Invoke-Tree {
    param([string]$Dir, [string[]]$ConfigureArgs, [string[]]$Targets)
    Use-Toolchain
    if (-not (Test-Path (Join-Path $Root "$Dir\build.ninja"))) {
        Invoke-Step 'cmake' (@('-S', $Root, '-B', (Join-Path $Root $Dir), '-G', 'Ninja') + $ConfigureArgs)
    }
    $buildArgs = @('--build', (Join-Path $Root $Dir))
    if ($Targets) { $buildArgs += @('--target') + $Targets }
    Invoke-Step 'cmake' $buildArgs
}

function Invoke-Build   { Invoke-Tree 'build' @('-DCMAKE_BUILD_TYPE=Release') @() }
function Invoke-Debug   { Invoke-Tree 'build-debug' @('-DCMAKE_BUILD_TYPE=Debug', "-DCMAKE_PREFIX_PATH=$QtPrefix") @('laideal') }
function Invoke-UiTest  { Invoke-Tree 'build-uitest' @('-DCMAKE_BUILD_TYPE=Release', '-DLAIDEAL_UI_TEST=ON') @('laideal', 'fake_aeat_server', 'escpos_preview') }

function Invoke-Test {
    Invoke-Build
    $ctestArgs = @('--test-dir', (Join-Path $Root 'build'), '--output-on-failure', '--no-tests=error',
                   '--timeout', '120', '-j', "$Jobs")
    if ($Filter) { $ctestArgs += @('-R', $Filter) }
    try {
        Invoke-Step 'ctest' $ctestArgs
    } finally {
        # The run's summary page on GitHub, also for a red suite.
        if ($OnGitHub) {
            & (Join-Path $Root '.github\scripts\Render-TestSummary.ps1') -Title 'Test results' -BuildDir (Join-Path $Root 'build')
        }
    }
}

function Invoke-Xsd {
    Invoke-Build
    $ErrorActionPreference = 'Continue'
    & python -c "import lxml" 2>$null
    $hasLxml = $LASTEXITCODE -eq 0
    $ErrorActionPreference = 'Stop'
    if (-not $hasLxml) {
        Write-Host "aeat_xsd is skipped without lxml: python -m pip install lxml" -ForegroundColor Yellow
    }
    Invoke-Step 'ctest' @('--test-dir', (Join-Path $Root 'build'), '--output-on-failure', '-R', 'aeat_record|aeat_response|aeat_xsd')
}

# The UI scripts' Python environment; requirements are installed again when their file changed.
function Invoke-Venv {
    $requirements = Join-Path $Root 'tests\ui\requirements.txt'
    $stamp = Join-Path $Root '.venv\requirements.stamp'
    if (-not (Test-Path $VenvPython)) {
        Invoke-Step 'python' @('-m', 'venv', (Join-Path $Root '.venv'))
    }
    $current = (Test-Path $stamp) -and ((Get-Item $stamp).LastWriteTimeUtc -ge (Get-Item $requirements).LastWriteTimeUtc)
    if (-not $current) {
        Invoke-Step $VenvPython @('-m', 'pip', 'install', '--quiet', '-r', $requirements)
        Set-Content -Path $stamp -Value (Get-Date -Format s) -Encoding ascii
    }
}

# The capturing printer queue of smoke block C; Windows asks for an administrator.
function Invoke-Printer {
    Invoke-Step 'python' @((Join-Path $Root 'tests\ui\printer.py'), 'install')
}

# The app under test: the UI-test build, or with -Exe an installed one (-Tools is then its
# tools folder). An installed app must start on the DLLs next to it, so the build's Qt and
# MinGW leave PATH.
function Resolve-UiApp {
    if (-not $Exe) {
        if ($Tools -or $Installer) { throw "-Tools and -Installer go with -Exe (an installed UI-test app)" }
        Invoke-UiTest | Out-Host   # its output must not join the value returned here
        return @{ Exe = $UiTestExe; Tools = $UiTools }
    }
    if (-not $Tools) { throw "-Exe needs -Tools <folder of fake_aeat_server.exe and escpos_preview.exe>" }
    foreach ($path in @($Exe, $Tools)) {
        if (-not (Test-Path $path)) { throw "Not found: $path" }
    }
    $env:PATH = ($env:PATH -split ';' | Where-Object { $_ -notmatch '\\Qt\\' -and $_ -notmatch 'mingw' }) -join ';'
    return @{ Exe = (Resolve-Path $Exe).Path; Tools = (Resolve-Path $Tools).Path }
}

function Invoke-UiSmoke {
    $app = Resolve-UiApp
    Invoke-Venv
    $folder = Join-Path $Root 'ui-smoke-out'
    if ($Out) { $folder = $Out }
    $smokeArgs = @((Join-Path $Root 'tests\ui\smoke.py'), '--exe', $app.Exe, '--tools', $app.Tools, '--out', $folder)
    if ($Blocks)    { $smokeArgs += @('--blocks', $Blocks) }
    if ($Printer)   { $smokeArgs += @('--printer', $Printer) }
    if ($Installer) { $smokeArgs += @('--installer', (Resolve-Path $Installer).Path) }
    Write-Host "The UI smoke test takes the mouse and keyboard for a few minutes." -ForegroundColor Yellow
    Invoke-Step $VenvPython $smokeArgs
}

# aeat_preproduction.py wants an empty folder: every run gets its own, unless -Out names one.
function New-AeatRunFolder {
    param([string]$Kind)
    $folder = Join-Path $Root ("aeat-run\{0}-{1}" -f (Get-Date -Format 'yyyyMMdd-HHmmss'), $Kind)
    if ($Out) { $folder = $Out }
    Write-Host "Results: $folder"
    return $folder
}

function Invoke-AeatDry {
    $app = Resolve-UiApp
    Invoke-Venv
    $folder = New-AeatRunFolder 'dry'
    Invoke-Step $VenvPython @((Join-Path $Root 'tests\ui\aeat_preproduction.py'), '--exe', $app.Exe,
                              '--dry-run', '--tools', $app.Tools, '--out', $folder)
}

function Invoke-AeatReal {
    if (-not $Settings) { throw "aeat-real needs -Settings <file> (the settings with the issuer and its certificate)" }
    if (-not (Test-Path $Settings)) { throw "Settings file not found: $Settings" }
    if (-not $Force) {
        Write-Host "This sends records to AEAT's TEST environment with the real certificate." -ForegroundColor Yellow
        if ((Read-Host "Type SI to go on") -ne 'SI') { throw "Cancelled." }
    }
    Invoke-UiTest
    Invoke-Venv
    $folder = New-AeatRunFolder 'real'
    Invoke-Step $VenvPython @((Join-Path $Root 'tests\ui\aeat_preproduction.py'), '--exe', $UiTestExe,
                              '--settings-from', (Resolve-Path $Settings).Path, '--out', $folder)
}

function Invoke-Regression {
    # Its stages cannot share a results folder: aeat-dry wants an empty one.
    if ($Out) { throw "regression takes no -Out: every stage writes to its own folder" }
    $stages = [ordered]@{
        'build'    = { Invoke-Build }
        'test'     = { Invoke-Test }
        'uitest'   = { Invoke-UiTest }
        'ui-smoke' = { Invoke-UiSmoke }
        'aeat-dry' = { Invoke-AeatDry }
    }
    $results = @()
    $failed = $null
    foreach ($name in $stages.Keys) {
        if ($failed) { $results += "  SKIPPED  $name"; continue }
        Write-Host "`n=== $name ===" -ForegroundColor Green
        $started = Get-Date
        try {
            & $stages[$name]
            $results += "  PASSED   $name ({0:N0} s)" -f ((Get-Date) - $started).TotalSeconds
        } catch {
            $failed = $_
            $results += "  FAILED   $name - $($_.Exception.Message)"
        }
    }
    Write-Host "`nRegression:"
    $results | ForEach-Object { Write-Host $_ }
    if ($failed) { throw "Regression failed." }
}

function Remove-Paths {
    param([string[]]$Paths)
    foreach ($path in $Paths) {
        foreach ($item in @(Get-Item -Path (Join-Path $Root $path) -ErrorAction SilentlyContinue)) {
            Write-Host "Deleting $($item.FullName)"
            Remove-Item -LiteralPath $item.FullName -Recurse -Force
        }
    }
}

function Invoke-Clean {
    # Real AEAT runs are never deleted: their report is evidence for the release (smoke test M6).
    Remove-Paths @('ui-smoke-out', 'aeat-run\*-dry', 'print-jobs', 'build\test-results-*.xml', 'build\aeat-xml')
}

function Invoke-CleanAll {
    $present = @($BuildDirs | Where-Object { Test-Path (Join-Path $Root $_) })
    if ($present -and -not $Force) {
        Write-Host "Build folders to delete: $($present -join ', ')" -ForegroundColor Yellow
        if ((Read-Host "Type SI to delete them") -ne 'SI') { throw "Cancelled." }
    }
    Invoke-Clean
    Remove-Paths $present
}

$Targets = [ordered]@{
    'build'      = @{ Text = 'Release build into build\ (configures it when needed)';                    Run = { Invoke-Build } }
    'debug'      = @{ Text = 'Debug build of the app into build-debug\ (the tree VSCode F5 uses)';       Run = { Invoke-Debug } }
    'test'       = @{ Text = 'build, then the test suites       [-Filter <regex>] [-Jobs <n>]';          Run = { Invoke-Test } }
    'xsd'        = @{ Text = 'the direct client''s XML against AEAT''s schemas (needs Python lxml)';      Run = { Invoke-Xsd } }
    'uitest'     = @{ Text = 'UI-test build into build-uitest\ (never for the shop)';                    Run = { Invoke-UiTest } }
    'venv'       = @{ Text = 'the Python environment of the UI scripts (.venv)';                         Run = { Invoke-Venv } }
    'printer'    = @{ Text = 'the capturing printer queue LaIdeal-Smoke of block C (as administrator)';  Run = { Invoke-Printer } }
    'ui-smoke'   = @{ Text = 'UI smoke test, takes mouse and keyboard  [-Blocks AB] [-Printer <queue>]'; Run = { Invoke-UiSmoke } }
    'aeat-dry'   = @{ Text = 'AEAT pre-production run against the fake AEAT (no certificate)';           Run = { Invoke-AeatDry } }
    'aeat-real'  = @{ Text = 'the same against AEAT''s test environment   -Settings <file> [-Force]';     Run = { Invoke-AeatReal } }
    'regression' = @{ Text = 'build, test, uitest, ui-smoke, aeat-dry; stops at the first failure';      Run = { Invoke-Regression } }
    'clean'      = @{ Text = 'delete test outputs (ui-smoke-out, dry AEAT runs, print-jobs, test results)'; Run = { Invoke-Clean } }
    'clean-all'  = @{ Text = 'clean, and delete the build folders   [-Force]';                           Run = { Invoke-CleanAll } }
}

function Show-Help {
    Write-Host "Usage: .\make.ps1 <target> [options]`n"
    foreach ($name in $Targets.Keys) {
        Write-Host ("  {0,-11} {1}" -f $name, $Targets[$name].Text)
    }
    Write-Host "`nui-smoke and aeat-dry test an installed UI-test app with -Exe <laideal.exe> -Tools <folder>"
    Write-Host "(ui-smoke also -Installer <setup.exe>); -Out <folder> names the results folder."
    Write-Host "`nNot here: a release (releases\release.ps1 X.Y, or /release) and the manual"
    Write-Host "smoke checklist (docs\testing\smoke_test.md). Details: README.md, Tests."
}

if ($Target -eq 'help') { Show-Help; exit 0 }
if (-not $Targets.Contains($Target)) {
    Write-Host "Unknown target: $Target`n" -ForegroundColor Red
    Show-Help
    exit 1
}

try {
    & $Targets[$Target].Run
} catch {
    Write-Host "make ${Target}: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
exit 0
