# Headless Rust bot client(s), for trying out or load-testing a server.
#   .\run_bot.ps1 --server 127.0.0.1:7777 --count 4 --seconds 60
#   .\run_bot.ps1 --help
Push-Location $PSScriptRoot
try { & (Join-Path $PSScriptRoot "tfps-bot.exe") @args } finally { Pop-Location }
exit $LASTEXITCODE
