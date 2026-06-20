param(
    [string]$DataDirectory = "out\nsi1",
    [string]$RomTrace = "out\player-runtime-trace.csv",
    [string]$TeacherTrace = "out\native-teacher-trace.csv",
    [string]$NativeTrace = "out\native-runtime-trace.csv",
    [string]$Report = "out\port-improvement-report.md",
    [switch]$Strict
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

function Stop-Viewer {
    Get-Process sonic-pocket-viewer -ErrorAction SilentlyContinue | Stop-Process -Force
}

function Run-Capture {
    param([scriptblock]$Command)
    $Output = & $Command 2>&1
    $Code = $LASTEXITCODE
    return @{
        Code = $Code
        Text = (($Output | Out-String).TrimEnd())
    }
}

function Append-Section {
    param(
        [System.Text.StringBuilder]$Builder,
        [string]$Title,
        [string]$Body
    )
    [void]$Builder.AppendLine()
    [void]$Builder.AppendLine("## $Title")
    [void]$Builder.AppendLine()
    [void]$Builder.AppendLine('```text')
    [void]$Builder.AppendLine($Body)
    [void]$Builder.AppendLine('```')
}

Stop-Viewer

$MsBuild = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
$Build = Run-Capture { & $MsBuild "build\viewer\sonic-pocket-viewer.vcxproj" /p:Configuration=Release /p:Platform=x64 /m /nologo }
if ($Build.Code -ne 0) {
    throw "build failed"
}

$Teacher = Run-Capture {
    & "build\viewer\Release\sonic-pocket-viewer.exe" $DataDirectory --teacher-trace $RomTrace --teacher-out $TeacherTrace
}
if ($Teacher.Code -ne 0) {
    throw "teacher trace generation failed"
}

$TeacherAnalysis = Run-Capture {
    python "tools\analyze_teacher_trace.py" $TeacherTrace --limit 20
}
if ($TeacherAnalysis.Code -ne 0) {
    throw "teacher analysis failed"
}

$FloorLab = Run-Capture {
    python "tools\floor_correction_lab.py" $TeacherTrace --limit 40
}
if ($FloorLab.Code -ne 0) {
    throw "floor correction lab failed"
}

$Replay = Run-Capture {
    & "build\viewer\Release\sonic-pocket-viewer.exe" $DataDirectory --replay-trace $RomTrace --trace-out $NativeTrace
}
if ($Replay.Code -ne 0) {
    throw "normal replay generation failed"
}

$Compare = Run-Capture {
    python "tools\compare_player_trace.py" $RomTrace $NativeTrace
}

$ReportPath = Join-Path $Root $Report
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $ReportPath) | Out-Null

$Now = Get-Date -Format "yyyy-MM-dd HH:mm:ss zzz"
$Builder = [System.Text.StringBuilder]::new()
[void]$Builder.AppendLine("# Port improvement report")
[void]$Builder.AppendLine()
[void]$Builder.AppendLine("Generated: $Now")
[void]$Builder.AppendLine()
[void]$Builder.AppendLine("Ratchet rule:")
[void]$Builder.AppendLine()
[void]$Builder.AppendLine("- Generate teacher-forced trace.")
[void]$Builder.AppendLine("- Cluster independent one-step mismatches.")
[void]$Builder.AppendLine("- Patch only the routine owning the top mismatch class.")
[void]$Builder.AppendLine("- Keep a patch only if teacher metrics improve and replay does not regress.")
[void]$Builder.AppendLine()
[void]$Builder.AppendLine("This report is intentionally produced even when the current port fails parity.")
[void]$Builder.AppendLine("Use `-Strict` when this command is running as a hard CI/pre-push gate.")

Append-Section $Builder "Teacher trace generation" $Teacher.Text
Append-Section $Builder "Teacher mismatch analysis" $TeacherAnalysis.Text
Append-Section $Builder "Floor correction lab" $FloorLab.Text
Append-Section $Builder "Normal replay generation" $Replay.Text
Append-Section $Builder "Normal replay compare" $Compare.Text

[void]$Builder.AppendLine()
[void]$Builder.AppendLine("## Next target")
[void]$Builder.AppendLine()
[void]$Builder.AppendLine("Current evidence says the next patch should target `sub_39BC22` / `BGCollChk4` floor Y correction only.")
[void]$Builder.AppendLine("Do not touch camera, sprites, animation, or broad movement until `dy_raw` teacher mismatches drop.")

Set-Content -LiteralPath $ReportPath -Value $Builder.ToString()
Write-Host "Wrote $ReportPath"

if ($Strict -and $Compare.Code -ne 0) {
    exit $Compare.Code
}

exit 0
