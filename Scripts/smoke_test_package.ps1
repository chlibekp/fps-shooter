# Smoke test of a packaged Windows release archive (the .zip). Same checks as
# smoke_test_package.sh, but through the packaged .ps1 run scripts:
#  1. unpacks the archive into a temporary folder
#  2. starts the server (run_server.ps1) on a free UDP port and the lobby
#     (run_lobby.ps1) on a free TCP port, both on 127.0.0.1
#  3. checks the lobby's /health answers ok
#  4. connects the packaged C++ client and Rust bot and checks both are accepted
#  5. stops everything (whole process trees)
#   pwsh -File Scripts\smoke_test_package.ps1 <tfps-...-windows-x86_64.zip>
# NOTE: written for the release workflow's windows-latest job; it could not be
# run on the Linux machine it was written on.
param([Parameter(Mandatory = $true)][string]$Archive)
$ErrorActionPreference = "Stop"

$Archive = (Resolve-Path $Archive).Path
$Work = Join-Path ([System.IO.Path]::GetTempPath()) ("tfps-smoke-" + [System.Guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Path $Work | Out-Null
$procs = @()

function Stop-All {
    foreach ($p in $script:procs) {
        if ($p -and -not $p.HasExited) { & taskkill.exe /PID $p.Id /T /F 2>&1 | Out-Null }
    }
}

function Fail([string]$Message) {
    Write-Host "SMOKE TEST FAILED: $Message"
    Get-ChildItem -Path $Work -Filter "*.log" -ErrorAction SilentlyContinue | ForEach-Object {
        Write-Host "--- $($_.Name)"
        Get-Content $_.FullName -Tail 40 | Write-Host
    }
    Stop-All
    exit 1
}

function Get-FreePort([string]$Kind) {
    $ep = [System.Net.IPEndPoint]::new([System.Net.IPAddress]::Loopback, 0)
    if ($Kind -eq "udp") {
        $s = [System.Net.Sockets.UdpClient]::new($ep)
        $port = ([System.Net.IPEndPoint]$s.Client.LocalEndPoint).Port
        $s.Close()
    } else {
        $s = [System.Net.Sockets.TcpListener]::new($ep)
        $s.Start()
        $port = ([System.Net.IPEndPoint]$s.LocalEndpoint).Port
        $s.Stop()
    }
    return $port
}

Expand-Archive -Path $Archive -DestinationPath $Work
$pkg = Get-ChildItem -Path $Work -Directory | Select-Object -First 1
if (-not $pkg) { Fail "archive has no top-level folder" }
$pkg = $pkg.FullName
if (-not (Test-Path (Join-Path $pkg "run_server.ps1"))) { Fail "run_server.ps1 missing in $Archive" }
Write-Host "unpacked to $pkg"
Get-ChildItem $pkg | Format-Table -AutoSize | Out-String | Write-Host

$udp = Get-FreePort "udp"
$http = Get-FreePort "tcp"
Write-Host "server udp 127.0.0.1:$udp, lobby http 127.0.0.1:$http"

# Started from another directory on purpose: the run scripts must find their
# definitions relative to themselves.
Set-Location $Work
$shell = (Get-Process -Id $PID).Path
$procs += Start-Process -FilePath $shell -PassThru -NoNewWindow `
    -RedirectStandardOutput (Join-Path $Work "server.log") -RedirectStandardError (Join-Path $Work "server.err.log") `
    -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $pkg "run_server.ps1"), "--bind", "127.0.0.1:$udp", "--stats-interval", "3600")
$server = $procs[-1]
$procs += Start-Process -FilePath $shell -PassThru -NoNewWindow `
    -RedirectStandardOutput (Join-Path $Work "lobby.log") -RedirectStandardError (Join-Path $Work "lobby.err.log") `
    -ArgumentList @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", (Join-Path $pkg "run_lobby.ps1"), "--bind", "127.0.0.1:$http", "--game-bind-ip", "127.0.0.1")
$lobby = $procs[-1]

$health = $null
for ($i = 0; $i -lt 60; $i++) {
    if ($lobby.HasExited) { Fail "lobby exited" }
    try {
        $health = Invoke-RestMethod -Uri "http://127.0.0.1:$http/health" -TimeoutSec 2
        break
    } catch {
        Start-Sleep -Milliseconds 500
    }
}
Write-Host "lobby /health: $($health | ConvertTo-Json -Compress)"
if (-not $health -or $health.ok -ne $true) { Fail "lobby /health did not answer ok" }

if ($server.HasExited) { Fail "server exited" }
# Client output goes partly to stderr; that must not count as a script error.
$ErrorActionPreference = "Continue"
$cli = & (Join-Path $pkg "run_cli_client.ps1") --server "127.0.0.1:$udp" --name Smoke --seconds 3 --pattern idle 2>&1 | Out-String
$cliExit = $LASTEXITCODE
Write-Host $cli
if ($cliExit -ne 0) { Fail "C++ client exited with $cliExit" }
if ($cli -notmatch "connected as player") { Fail "C++ client did not connect" }

$bot = & (Join-Path $pkg "run_bot.ps1") --server "127.0.0.1:$udp" --seconds 2 --pattern idle --report 0 2>&1 | Out-String
$botExit = $LASTEXITCODE
Write-Host $bot
if ($botExit -ne 0) { Fail "bot exited with $botExit" }
if ($bot -notmatch "connected as player") { Fail "bot did not connect" }
$ErrorActionPreference = "Stop"

if ($server.HasExited) { Fail "server died during the test" }
if ($lobby.HasExited) { Fail "lobby died during the test" }
Stop-All
Set-Location ([System.IO.Path]::GetTempPath())
Remove-Item -Recurse -Force $Work -ErrorAction SilentlyContinue
Write-Host "SMOKE TEST PASSED: $(Split-Path -Leaf $Archive)"
exit 0
