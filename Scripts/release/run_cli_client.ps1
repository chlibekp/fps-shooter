# Headless C++ client (the Unreal client's NetCore, without the engine).
#   .\run_cli_client.ps1 --server 127.0.0.1:7777 --seconds 10 --pattern circle|square|idle
Push-Location $PSScriptRoot
try { & (Join-Path $PSScriptRoot "tfps_cli_client.exe") --definitions Shared/Definitions @args } finally { Pop-Location }
exit $LASTEXITCODE
