param(
    [string]$CandidatePatch = "",
    [string]$DataDirectory = "out\nsi1",
    [string]$RomTrace = "out\player-runtime-trace.csv",
    [string]$NativeTrace = "out\native-runtime-trace.csv",
    [string]$CollisionTrace = "out\native-scripted-collision-debug.csv",
    [int]$MinimumFirstMismatchRow = 527,
    [switch]$KeepOnFullPass,
    [switch]$KeepOnImprovement
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
Set-Location $Root

$ViewerSource = Join-Path $Root "src\viewer\main.cpp"
$Backup = Join-Path $Root "work\failsafe-main.cpp.bak"
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Backup) | Out-Null
Copy-Item -LiteralPath $ViewerSource -Destination $Backup -Force

function Restore-ViewerSource {
    Copy-Item -LiteralPath $Backup -Destination $ViewerSource -Force
}

function Stop-Viewer {
    Get-Process sonic-pocket-viewer -ErrorAction SilentlyContinue | Stop-Process -Force
}

function Run-Build {
    $MsBuild = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\amd64\MSBuild.exe"
    & $MsBuild "build\viewer\sonic-pocket-viewer.vcxproj" /p:Configuration=Release /p:Platform=x64 /m /nologo
    if ($LASTEXITCODE -ne 0) {
        throw "build failed"
    }
}

function Run-ReplayCompare {
    & "build\viewer\Release\sonic-pocket-viewer.exe" $DataDirectory --replay-trace $RomTrace --trace-out $NativeTrace
    if ($LASTEXITCODE -ne 0) {
        throw "native replay generation failed"
    }

    $CompareOutput = python "tools\compare_player_trace.py" $RomTrace $NativeTrace 2>&1
    $CompareExit = $LASTEXITCODE
    $CompareText = ($CompareOutput | Out-String).TrimEnd()
    Write-Host $CompareText

    if ($CompareExit -eq 0) {
        return @{
            FullPass = $true
            FirstMismatchRow = [int]::MaxValue
            Output = $CompareText
        }
    }

    $Match = [regex]::Match($CompareText, "\s(?<row>\d+)\s+rows:\s+frame\s+(?<frame>\d+)")
    if (-not $Match.Success) {
        throw "unable to parse first mismatch row from compare output"
    }

    return @{
        FullPass = $false
        FirstMismatchRow = [int]$Match.Groups["row"].Value
        Output = $CompareText
    }
}

function Run-CollisionRegression {
    if (-not (Test-Path $CollisionTrace)) {
        Write-Host "collision trace not found: $CollisionTrace"
        return
    }
    python "tools\collision_regression.py" $CollisionTrace --show 20
    if ($LASTEXITCODE -ne 0) {
        throw "collision regression failed"
    }
}

$KeepCandidate = $false

try {
    Stop-Viewer

    if ($CandidatePatch -ne "") {
        if (-not (Test-Path $CandidatePatch)) {
            throw "candidate patch not found: $CandidatePatch"
        }
        git apply --check $CandidatePatch
        if ($LASTEXITCODE -ne 0) {
            throw "candidate patch does not apply cleanly"
        }
        git apply $CandidatePatch
        if ($LASTEXITCODE -ne 0) {
            throw "candidate patch apply failed"
        }
    }

    Run-Build
    $Replay = Run-ReplayCompare
    Run-CollisionRegression

    if ($Replay.FullPass) {
        Write-Host "FAILSAFE RESULT: full ROM replay pass."
        $KeepCandidate = [bool]$KeepOnFullPass
    } elseif ($Replay.FirstMismatchRow -gt $MinimumFirstMismatchRow) {
        Write-Host "FAILSAFE RESULT: improved first mismatch row $($Replay.FirstMismatchRow) > $MinimumFirstMismatchRow."
        $KeepCandidate = [bool]$KeepOnImprovement
    } else {
        throw "candidate rejected: first mismatch row $($Replay.FirstMismatchRow) <= $MinimumFirstMismatchRow"
    }
} catch {
    Write-Host "FAILSAFE RESULT: rejected - $($_.Exception.Message)"
    $KeepCandidate = $false
} finally {
    if (-not $KeepCandidate) {
        Restore-ViewerSource
        Write-Host "Viewer source restored from failsafe backup."
    } else {
        Write-Host "Candidate kept by explicit switch."
    }
}

if ($KeepCandidate) {
    exit 0
}

exit 1
