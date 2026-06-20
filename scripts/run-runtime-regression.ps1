param(
    [string]$DataDirectory = "out\nsi1",
    [string]$RomTrace = "out\player-runtime-trace.csv",
    [string]$NativeTrace = "out\native-runtime-trace.csv",
    [string]$CollisionTrace = "out\native-scripted-collision-debug.csv"
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$MsBuild = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
& $MsBuild "build\viewer\sonic-pocket-viewer.vcxproj" /p:Configuration=Release /p:Platform=x64 /m /nologo

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& "build\viewer\Release\sonic-pocket-viewer.exe" $DataDirectory --replay-trace $RomTrace --trace-out $NativeTrace

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

python "tools\compare_player_trace.py" $RomTrace $NativeTrace
$TraceCompareExit = $LASTEXITCODE

if (Test-Path $CollisionTrace) {
    python "tools\collision_regression.py" $CollisionTrace --show 20
    $CollisionExit = $LASTEXITCODE
} else {
    Write-Host "collision trace not found: $CollisionTrace"
    $CollisionExit = 0
}

if ($TraceCompareExit -ne 0) {
    exit $TraceCompareExit
}
exit $CollisionExit
