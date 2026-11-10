# TacticalFPS launcher (Windows). Double-click Play.bat / Practice.bat, or:
#   .\Play.ps1                      play: main menu; a lobby service runs on this
#                                   PC so you can host matches and give friends
#                                   the invite code
#   .\Play.ps1 practice [map] [N]   straight into a deathmatch on this PC against
#                                   N moving target bots (default 4).
#                                   Maps: test_arena, harbor, villa, consulate.
#   .\Play.ps1 join HOST            use a friend's lobby (they run Play.bat)
# Environment: TFPS_NAME, TFPS_PUBLIC_HOST, TFPS_CLIENT_ARGS, TFPS_LOG_DIR.
param([string]$Mode = "play", [string]$Arg1 = "", [string]$Arg2 = "")
$ErrorActionPreference = "Stop"
$Here = $PSScriptRoot
$Server = Join-Path $Here "Server"
Set-Location $Server
$Name = if ($env:TFPS_NAME) { $env:TFPS_NAME } else { $env:USERNAME }
$Name = ($Name -replace '[^A-Za-z0-9_-]', '')
if ($Name.Length -gt 16) { $Name = $Name.Substring(0, 16) }
if (-not $Name) { $Name = "Player" }
$Logs = if ($env:TFPS_LOG_DIR) { $env:TFPS_LOG_DIR } else { Join-Path $Here "Logs" }
New-Item -ItemType Directory -Force -Path $Logs | Out-Null
$Procs = @()

function Start-Background([string]$Exe, [string[]]$ArgList, [string]$Log) {
  return Start-Process -FilePath (Join-Path $Server $Exe) -ArgumentList $ArgList -WorkingDirectory $Server `
    -RedirectStandardOutput $Log -RedirectStandardError "$Log.err" -WindowStyle Hidden -PassThru
}

# Engine options (TFPS_CLIENT_ARGS) first, then "--" and the game's own options.
function Run-Game([string[]]$ArgList) {
  $exe = Get-ChildItem -Path (Join-Path $Here "Game") -Filter "TacticalFPS.exe" | Select-Object -First 1
  if (-not $exe) { throw "game not found in $Here\Game" }
  $all = @()
  if ($env:TFPS_CLIENT_ARGS) { $all += $env:TFPS_CLIENT_ARGS.Split(" ") }
  $all += "--"
  $all += $ArgList
  Start-Process -FilePath $exe.FullName -ArgumentList $all -Wait
}

function Lan-Address {
  if ($env:TFPS_PUBLIC_HOST) { return $env:TFPS_PUBLIC_HOST }
  $ip = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
    Where-Object { $_.IPAddress -notlike "127.*" -and $_.IPAddress -notlike "169.254.*" } |
    Select-Object -First 1 -ExpandProperty IPAddress
  if ($ip) { return $ip } else { return "127.0.0.1" }
}

try {
  switch ($Mode) {
    "play" {
      $busy = Get-NetTCPConnection -LocalPort 7700 -State Listen -ErrorAction SilentlyContinue
      if ($busy) {
        Write-Host "A lobby service is already running on port 7700; using it."
      } else {
        $hostAddr = Lan-Address
        $Procs += Start-Background "tfps-lobby.exe" @("--bind", "0.0.0.0:7700", "--public-host", $hostAddr, "--definitions", "Shared/Definitions") (Join-Path $Logs "lobby.log")
        Write-Host "Lobby service running: friends join with 'Play.ps1 join $hostAddr' (allow TCP 7700 and the game's UDP ports in the firewall)."
        Start-Sleep -Milliseconds 500
      }
      Run-Game @("--lobby", "http://127.0.0.1:7700", "--name", $Name)
    }
    "join" {
      if (-not $Arg1) { throw "usage: Play.ps1 join HOST" }
      Run-Game @("--lobby", "http://${Arg1}:7700", "--name", $Name)
    }
    "practice" {
      $map = if ($Arg1) { $Arg1 } else { "test_arena" }
      $bots = if ($Arg2) { [int]$Arg2 } else { 4 }
      $port = if ($env:TFPS_PORT) { $env:TFPS_PORT } else { "7777" }
      $Procs += Start-Background "tfps-server.exe" @("--bind", "127.0.0.1:$port", "--map", $map, "--mode", "deathmatch",
        "--max-players", ($bots + 1), "--stats-interval", "3600") (Join-Path $Logs "server.log")
      Start-Sleep -Milliseconds 500
      if ($bots -gt 0) {
        $Procs += Start-Background "tfps-bot.exe" @("--server", "127.0.0.1:$port", "--count", $bots, "--seconds", "86400",
          "--pattern", "random", "--map", "Shared/Definitions/maps/$map.json", "--weapons", "Shared/Definitions/weapons.json") (Join-Path $Logs "bots.log")
      }
      Run-Game @("--connect", "127.0.0.1:$port", "--map", $map, "--name", $Name)
    }
    default { Get-Content $PSCommandPath -TotalCount 9; exit 2 }
  }
} finally {
  foreach ($p in $Procs) { if ($p -and -not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } }
}
