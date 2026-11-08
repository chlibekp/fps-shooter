# Lobby service (HTTP API, default 0.0.0.0:7700). Matches run in-process.
#   .\run_lobby.ps1 --bind 0.0.0.0:7700 --public-host my.server.example
#   .\run_lobby.ps1 --help
# Runs from the package folder so the relative Shared\Definitions path resolves.
Push-Location $PSScriptRoot
try { & (Join-Path $PSScriptRoot "tfps-lobby.exe") @args } finally { Pop-Location }
exit $LASTEXITCODE
