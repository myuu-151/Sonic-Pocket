[CmdletBinding()]
param(
    [string[]]$Addresses = @(
        '0x3A59C4',
        '0x23C583',
        '0x3F18FB',
        '0x23E86A',
        '0x23BFE2',
        '0x2911B9',
        '0x23E17D',
        '0x3C9370',
        '0x23E9A1',
        '0x23C572'
    ),
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
    $Output = Join-Path $ProjectRoot 'out\vblank-decompilation.c'
}
if (-not [IO.Path]::IsPathRooted($Output)) {
    $Output = Join-Path $ProjectRoot $Output
}
$Output = [IO.Path]::GetFullPath($Output)
foreach ($Address in $Addresses) {
    if ($Address -notmatch '^0[xX][0-9a-fA-F]+$') {
        throw "Address '$Address' must be quoted hexadecimal text such as '0x2000A0'."
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
    'ExportDecompilation.java',
    $Output
) + $Addresses

& $Analyzer @AnalyzerArguments
if ($LASTEXITCODE -ne 0) {
    throw "Ghidra decompilation export failed with exit code $LASTEXITCODE."
}

Write-Host "Wrote Ghidra decompilation to $Output"
