[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ToolsDirectory = Join-Path $ProjectRoot 'Tools'

$GhidraDirectories = @(Get-ChildItem -LiteralPath $ToolsDirectory -Directory -Filter 'ghidra_*_PUBLIC')
$JdkDirectories = @(Get-ChildItem -LiteralPath $ToolsDirectory -Directory -Filter 'jdk-*')

if ($GhidraDirectories.Count -ne 1) {
    throw "Expected exactly one Ghidra installation in $ToolsDirectory."
}
if ($JdkDirectories.Count -ne 1) {
    throw "Expected exactly one JDK installation in $ToolsDirectory."
}

$env:JAVA_HOME = $JdkDirectories[0].FullName
$env:PATH = (Join-Path $env:JAVA_HOME 'bin') + [IO.Path]::PathSeparator + $env:PATH
$GhidraLauncher = Join-Path $GhidraDirectories[0].FullName 'ghidraRun.bat'

if (-not (Test-Path -LiteralPath $GhidraLauncher -PathType Leaf)) {
    throw "Ghidra launcher not found: $GhidraLauncher"
}

Start-Process -FilePath $GhidraLauncher -WorkingDirectory $ProjectRoot
