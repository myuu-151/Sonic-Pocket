[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string[]]$Addresses,
    [string]$Output,
    [string]$ProjectName = 'SonicPocket',
    [string]$ProgramName = 'SonicPocketAdventure'
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ToolsDirectory = Join-Path $ProjectRoot 'Tools'
$ProjectDirectory = Join-Path $ToolsDirectory 'ghidra-projects'
$ProjectLock = Join-Path $ProjectDirectory "$ProjectName.lock"
$ScriptDirectory = Join-Path $PSScriptRoot 'ghidra'

if (-not $Output) {
    $Output = Join-Path $ProjectRoot 'out\ghidra-references.csv'
}
if (-not [IO.Path]::IsPathRooted($Output)) {
    $Output = Join-Path $ProjectRoot $Output
}
$Output = [IO.Path]::GetFullPath($Output)
foreach ($Address in $Addresses) {
    if ($Address -notmatch '^0[xX][0-9a-fA-F]+$') {
        throw "Address '$Address' must be quoted hexadecimal text such as '0x6F82'."
    }
}
if (Test-Path -LiteralPath $ProjectLock) {
    throw "Ghidra project is open or locked: $ProjectLock"
}

$GhidraDirectories = @(Get-ChildItem -LiteralPath $ToolsDirectory -Directory -Filter 'ghidra_*_PUBLIC')
$JdkDirectories = @(Get-ChildItem -LiteralPath $ToolsDirectory -Directory -Filter 'jdk-*')
if ($GhidraDirectories.Count -ne 1 -or $JdkDirectories.Count -ne 1) {
    throw 'Expected exactly one portable Ghidra installation and one JDK under Tools.'
}

$env:JAVA_HOME = $JdkDirectories[0].FullName
$env:PATH = (Join-Path $env:JAVA_HOME 'bin') + [IO.Path]::PathSeparator + $env:PATH
$Analyzer = Join-Path $GhidraDirectories[0].FullName 'support\analyzeHeadless.bat'
$AnalyzerArguments = @(
    $ProjectDirectory,
    $ProjectName,
    '-process',
    $ProgramName,
    '-noanalysis',
    '-scriptPath',
    $ScriptDirectory,
    '-postScript',
    'ExportReferencesTo.java',
    $Output
) + $Addresses

& $Analyzer @AnalyzerArguments
if ($LASTEXITCODE -ne 0) {
    throw "Ghidra reference export failed with exit code $LASTEXITCODE."
}

Write-Host "Wrote Ghidra references to $Output"
