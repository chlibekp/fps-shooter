# Scripts\run_lobby.ps1 --bind 0.0.0.0:7700 --public-host my.server.example
. "$PSScriptRoot\Common.ps1"
Push-Location $RepoRoot
if (-not (Test-Path "target\release\tfps-lobby.exe")) { cargo build --release -p tfps-lobby }
& "target\release\tfps-lobby.exe" @args
Pop-Location
