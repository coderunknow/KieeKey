#============================================================================
# KieeKey - scripts/run_web_bridge.ps1
#
# Run the Arcade Hub web player (tools/arcade_serve) as a long-lived service on
# Windows: the C++ engine stays in memory, web/ is served, and the same /api/*
# the browser client expects is answered by the real engine.
#
#   powershell -ExecutionPolicy Bypass -File scripts\run_web_bridge.ps1
#   powershell -File scripts\run_web_bridge.ps1 -Port 9000 -RestartAlways
#
# Why this exists: the hosted sandbox preview dies with the sandbox. Running
# this on your own PC gives an always-on player at http://localhost:<port>/ and
# on the LAN at http://<your-ip>:<port>/ (add an inbound firewall rule once for
# a non-loopback bind).
#
# To keep it alive across logons, register a scheduled task (run as the user,
# "At log on", restart on failure):
#   schtasks /create /tn "KieeKey Arcade Bridge" /sc onlogon /rl highest ^
#     /tr "powershell -WindowStyle Hidden -File \"%CD%\scripts\run_web_bridge.ps1\""
#============================================================================
[CmdletBinding()]
param(
    [int]    $Port = 8765,
    [string] $Bind = '0.0.0.0',
    [int]    $Fps = 60,
    [string] $Web = '',
    [string] $BuildDir = '',
    [switch] $RestartAlways
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $Web)      { $Web = Join-Path $repo 'web' }
if (-not $BuildDir) { $BuildDir = Join-Path $repo 'build\web-bridge' }

function Find-Binary {
    $candidates = @(
        (Join-Path $BuildDir 'Release\arcade_serve.exe'),
        (Join-Path $BuildDir 'arcade_serve.exe'),
        (Join-Path $repo 'out\x64\Release\arcade_serve.exe'),
        (Join-Path $repo 'build\Release\arcade_serve.exe')
    )
    foreach ($c in $candidates) { if (Test-Path $c) { return $c } }
    return $null
}

function Build-Binary {
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { return $null }
    Write-Host "[bridge] configuring with cmake in $BuildDir"
    cmake -S $repo -B $BuildDir -A x64 -DCMAKE_BUILD_TYPE=Release `
          -DKIEEKEY_BUILD_TESTS=OFF -DKIEEKEY_BUILD_UI=none | Out-Null
    if ($LASTEXITCODE -ne 0) { return $null }
    cmake --build $BuildDir --config Release --target arcade_serve --parallel | Out-Null
    if ($LASTEXITCODE -ne 0) { return $null }
    return (Find-Binary)
}

$bin = Find-Binary
if (-not $bin) { $bin = Build-Binary }
if (-not $bin) {
    Write-Error "arcade_serve.exe not found and could not be built. Configure once with: cmake -S . -B build -A x64 -DKIEEKEY_BUILD_TESTS=ON"
    exit 1
}

$ip = (Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
       Where-Object { $_.IPAddress -notlike '127.*' -and $_.PrefixOrigin -ne 'WellKnown' } |
       Select-Object -First 1 -ExpandProperty IPAddress)

Write-Host "[bridge] binary : $bin"
Write-Host "[bridge] serving: http://localhost:$Port/  (LAN: http://$ip`:$Port/)"
Write-Host "[bridge] web    : $Web"
Write-Host "[bridge] Ctrl+C to stop. Restart on exit: $($RestartAlways.IsPresent)"

$guard = 0
while ($true) {
    $guard++
    & $bin --port $Port --host $Bind --web $Web --fps $Fps
    Write-Host "[bridge] exited with code $LASTEXITCODE (run $guard)"
    if (-not $RestartAlways) { break }
    Start-Sleep -Seconds 2
}
