[CmdletBinding()]
param(
    [string]$ProjectName = 'SonicPocket',
    [string]$ProgramName = 'SonicPocketAdventure'
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ToolsDirectory = Join-Path $ProjectRoot 'Tools'
$ProjectDirectory = Join-Path $ToolsDirectory 'ghidra-projects'
$ProjectLock = Join-Path $ProjectDirectory "$ProjectName.lock"
$SymbolsCsv = Join-Path $ProjectRoot 'analysis\symbols.csv'
$ScriptDirectory = Join-Path $PSScriptRoot 'ghidra'

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

& $Analyzer $ProjectDirectory $ProjectName `
    -process $ProgramName `
    -noanalysis `
    -scriptPath $ScriptDirectory `
    -postScript ImportSymbolsCsv.java $SymbolsCsv

if ($LASTEXITCODE -ne 0) {
    throw "Ghidra symbol import failed with exit code $LASTEXITCODE."
}
