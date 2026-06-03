<#
.SYNOPSIS
    Render a foldable per-suite test report from Qt Test JUnit XML.

.DESCRIPTION
    Reads every build/test-results-*.xml (one Qt JUnit file per test suite, each
    with one <testcase> per test method) and emits a Markdown report: a top-level
    pass/fail heading plus a collapsible <details> section per suite with a
    per-method table. The lifecycle methods initTestCase/cleanupTestCase are
    filtered out so only real test methods are listed.

    When GITHUB_STEP_SUMMARY is set (in GitHub Actions) the report is appended
    there; otherwise it is written to stdout, so the script is runnable locally:
        ctest --test-dir build --output-on-failure
        .github/scripts/Render-TestSummary.ps1

    This script only renders - it does not run the tests or affect the exit code.
    The workflow runs ctest (the gate) and calls this afterwards.

.PARAMETER Title
    Heading text after the status emoji (e.g. "Test results (release 9.2)").

.PARAMETER BuildDir
    Directory holding the test-results-*.xml files (default: build).

.PARAMETER PreviewDir
    Directory holding the ticket preview_*.txt ASCII mocks produced by the
    test_ticket_preview suite (default: <BuildDir>/tests). They are shown in a
    fenced code block in the summary - GitHub strips inline images from job
    summaries, so the graphical SVG renders live committed in
    docs/modules/printer/ and in the 'ticket-previews' artifact instead.
#>
[CmdletBinding()]
param(
    [string]$Title      = "Test results",
    [string]$BuildDir   = "build",
    [string]$PreviewDir = ""
)
if (-not $PreviewDir) { $PreviewDir = Join-Path $BuildDir 'tests' }

$pass = [char]0x2705  # white check mark
$fail = [char]0x274C  # cross mark
$lifecycle = @('initTestCase', 'cleanupTestCase', 'initTestCase_data')

$files = Get-ChildItem $BuildDir -Filter 'test-results-*.xml' -ErrorAction SilentlyContinue | Sort-Object Name

$grandTotal = 0
$grandFail  = 0
$blocks     = @()

foreach ($f in $files) {
    [xml]$doc = Get-Content $f.FullName
    $suite   = $doc.testsuite
    $methods = @($suite.testcase | Where-Object { $lifecycle -notcontains $_.name })
    $count   = $methods.Count
    $failed  = @($methods | Where-Object { $_.failure -or $_.error }).Count
    $grandTotal += $count
    $grandFail  += $failed

    $icon = if ($failed -eq 0) { $pass } else { $fail }
    $blocks += "<details><summary>$icon <b>$($suite.name)</b> - $count tests, $failed failed ($($suite.time)s)</summary>"
    $blocks += ""                       # blank line so the table renders inside <details>
    $blocks += "| Result | Test | Time (s) |"
    $blocks += "| :----: | ---- | -------: |"
    foreach ($m in $methods) {
        $mi = if ($m.failure -or $m.error) { $fail } else { $pass }
        $blocks += "| $mi | $($m.name) | $($m.time) |"
    }
    $blocks += ""
    $blocks += "</details>"
    $blocks += ""
}

if ($files.Count -eq 0) {
    $blocks += "_No JUnit results found in ``$BuildDir`` (expected ``test-results-*.xml``)._"
}

# Ticket previews (from test_ticket_preview): GitHub strips inline images
# (PNG/SVG, whether data: URIs or inline <svg>) from job summaries, so the
# graphic can't be embedded here. Render the monospace ASCII receipt (always
# shows) in a fenced block; the crisp graphical SVGs live committed in
# docs/modules/printer/ and in the 'ticket-previews' artifact (PNG + SVG).
$previews = Get-ChildItem -Path $PreviewDir -Filter 'preview_*.txt' -ErrorAction SilentlyContinue | Sort-Object Name
if ($previews) {
    $blocks += "<details open><summary><b>Ticket previews</b> (rendered recibo / factura)</summary>"
    $blocks += ""
    if ($env:GITHUB_SERVER_URL -and $env:GITHUB_REPOSITORY) {
        $ref    = if ($env:GITHUB_SHA) { $env:GITHUB_SHA } else { 'HEAD' }
        $docUrl = "$env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/blob/$ref/docs/modules/printer/README.md#sample-rendered-output"
        $blocks += "_Graphical SVG renders: [docs/modules/printer]($docUrl) - or download the **ticket-previews** artifact (PNG + SVG)._"
    } else {
        $blocks += "_Graphical SVG renders: ``docs/modules/printer/preview_*.svg`` (and the PNGs in ``$PreviewDir``)._"
    }
    $blocks += ""
    foreach ($p in $previews) {
        $name = $p.BaseName -replace '^preview_', ''
        $blocks += "**$name**"
        $blocks += ""
        $blocks += '```text'
        # -Encoding utf8: the .txt is UTF-8 (no BOM); without this Windows
        # PowerShell 5.1 reads it as ANSI and the Spanish accents become mojibake.
        $blocks += (Get-Content $p.FullName -Raw -Encoding utf8).TrimEnd()
        $blocks += '```'
        $blocks += ""
    }
    $blocks += "</details>"
    $blocks += ""
}

$top = if ($grandFail -eq 0 -and $files.Count -gt 0) { $pass } else { $fail }
$md  = @(
    "## $top $Title",
    "",
    "**$grandTotal tests, $grandFail failed** across $($files.Count) suite(s)",
    ""
) + $blocks

$text = ($md -join "`n")
if ($env:GITHUB_STEP_SUMMARY) {
    $text | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
} else {
    $text
}
