[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string[]]$Ranges,
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

if ($Ranges.Count -eq 0 -or $Ranges.Count % 2 -ne 0) {
    throw 'Ranges must contain start/end address pairs.'
}
foreach ($Address in $Ranges) {
    if ($Address -notmatch '^0[xX][0-9a-fA-F]+$') {
        throw "Address '$Address' must be quoted hexadecimal text such as '0x23B080'."
    }
}
if (-not $Output) {
    $Output = Join-Path $ProjectRoot 'out\ghidra-listing.txt'
}
if (-not [IO.Path]::IsPathRooted($Output)) {
    $Output = Join-Path $ProjectRoot $Output
}
$Output = [IO.Path]::GetFullPath($Output)
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
    'ExportListingRanges.java',
    $Output
) + $Ranges

& $Analyzer @AnalyzerArguments
if ($LASTEXITCODE -ne 0) {
    throw "Ghidra listing export failed with exit code $LASTEXITCODE."
}
if (-not (Test-Path -LiteralPath $Output -PathType Leaf)) {
    throw 'Ghidra completed without producing the requested listing. Check the script errors above.'
}

Write-Host "Wrote Ghidra listing to $Output"
