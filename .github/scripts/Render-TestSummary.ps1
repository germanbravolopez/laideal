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
#>
[CmdletBinding()]
param(
    [string]$Title    = "Test results",
    [string]$BuildDir = "build"
)

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
