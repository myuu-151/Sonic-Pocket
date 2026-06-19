[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Disassembler,

    [string]$Rom,

    [string]$Output
)

$ErrorActionPreference = 'Stop'
$ProjectRoot = Split-Path -Parent $PSScriptRoot

if (-not $Rom) {
    $candidates = @(Get-ChildItem -LiteralPath (Join-Path $ProjectRoot 'Rom') -File |
        Where-Object { $_.Extension -in '.bin', '.ngc', '.ngp', '.npc' })
    if ($candidates.Count -ne 1) {
        throw "Expected exactly one ROM in $ProjectRoot\Rom; found $($candidates.Count)."
    }
    $Rom = $candidates[0].FullName
}

if (-not $Output) {
    $Output = Join-Path $ProjectRoot 'out\entry.asm'
}

if (-not (Test-Path -LiteralPath $Disassembler -PathType Leaf)) {
    throw "Disassembler not found: $Disassembler"
}
if (-not (Test-Path -LiteralPath $Rom -PathType Leaf)) {
    throw "ROM not found: $Rom"
}

$OutputDirectory = Split-Path -Parent $Output
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

& $Disassembler -b 200000 -S 200040 -E 200200 $Rom |
    Set-Content -LiteralPath $Output -Encoding utf8
if ($LASTEXITCODE -ne 0) {
    throw "Disassembler exited with code $LASTEXITCODE."
}

Write-Host "Wrote entry-point disassembly to $Output"
