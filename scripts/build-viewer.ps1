param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$Run
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $projectRoot 'build\viewer'
$cmake = 'C:\Program Files\CMake\bin\cmake.exe'

if (-not (Test-Path -LiteralPath $cmake)) {
    $cmake = (Get-Command cmake -ErrorAction Stop).Source
}

& $cmake -S $projectRoot -B $buildDirectory -G 'Visual Studio 18 2026' -A x64
if ($LASTEXITCODE -ne 0) {
    throw 'CMake configuration failed.'
}

& $cmake --build $buildDirectory --config $Configuration
if ($LASTEXITCODE -ne 0) {
    throw 'Viewer build failed.'
}

$viewer = Join-Path $buildDirectory "$Configuration\sonic-pocket-viewer.exe"
Write-Host "Built: $viewer"
$stageData = Join-Path $projectRoot 'out\nsi1'

if (Test-Path -LiteralPath (Join-Path $stageData 'stage.png')) {
    & $viewer $stageData --smoke-test
    if ($LASTEXITCODE -ne 0) {
        throw 'Viewer smoke test failed.'
    }
}

if ($Run) {
    & $viewer $stageData
}
