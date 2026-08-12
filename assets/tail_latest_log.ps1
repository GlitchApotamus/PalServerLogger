param(
    [string]$LogDirectory = $PSScriptRoot,
    [string]$Filter = '*.log',
    [int]$PollMs = 250
)

$ErrorActionPreference = 'Stop'

function Get-LatestLogFile {
    param(
        [string]$Directory,
        [string]$FileFilter
    )

    $files = Get-ChildItem -Path $Directory -File -Filter $FileFilter -ErrorAction SilentlyContinue |
        Where-Object { $_.Length -gt 0 } |
        Sort-Object LastWriteTimeUtc -Descending

    if (-not $files) {
        throw "No matching log files found in '$Directory' using filter '$FileFilter'."
    }

    return $files[0]
}

try {
    $currentLog = Get-LatestLogFile -Directory $LogDirectory -FileFilter $Filter
    Write-Host "Tailing newest log: $($currentLog.FullName)" -ForegroundColor Cyan

    $reader = [System.IO.File]::OpenText($currentLog.FullName)
    $lastKnownWriteTime = $currentLog.LastWriteTimeUtc

    while ($true) {
        $line = $reader.ReadLine()

        if ($null -ne $line) {
            Write-Output $line
            continue
        }

        $latestLog = Get-LatestLogFile -Directory $LogDirectory -FileFilter $Filter
        if ($latestLog.FullName -ne $currentLog.FullName) {
            Write-Host "Switched to newer log: $($latestLog.FullName)" -ForegroundColor Yellow
            $reader.Close()
            $currentLog = $latestLog
            $reader = [System.IO.File]::OpenText($currentLog.FullName)
            $reader.ReadToEnd() | Out-Null
        }

        Start-Sleep -Milliseconds $PollMs
    }
}
catch {
    Write-Error $_.Exception.Message
    exit 1
}
finally {
    if ($reader) {
        $reader.Close()
    }
}
