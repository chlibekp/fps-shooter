# Dedicated server (UDP, default 0.0.0.0:7777). Arguments are passed through:
#   .\run_server.ps1 --bind 0.0.0.0:7777 --map harbor --mode defuse
#   .\run_server.ps1 --help
# Runs from the package folder so the relative Shared\Definitions paths resolve.
Push-Location $PSScriptRoot
try { & (Join-Path $PSScriptRoot "tfps-server.exe") @args } finally { Pop-Location }
exit $LASTEXITCODE
