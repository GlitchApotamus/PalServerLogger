$verFile = "$PSScriptRoot/version.txt"

if (Test-Path $verFile) {
    $content = (Get-Content $verFile -Raw).Trim()
    $parts = $content.Split('.')
    [int]$minor = $parts[0]
    [int]$patch = $parts[1]
} else {
    $minor = 0
    $patch = 0
}

$timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$versionStr = "1.$minor.$patch-$timestamp"

Compress-Archive -Path 'd3d9.dll', 'PalServerLogger.dll' -DestinationPath "PalServerLogger-$versionStr.zip" -Force
Write-Host "Successfully packaged PalServerLogger-$versionStr.zip"