# Scripts\run_server.ps1 --bind 0.0.0.0:7777 --tick-rate 128
. "$PSScriptRoot\Common.ps1"
Push-Location $RepoRoot
if (-not (Test-Path "target\release\tfps-server.exe")) { cargo build --release -p tfps-server }
& "target\release\tfps-server.exe" @args
Pop-Location
