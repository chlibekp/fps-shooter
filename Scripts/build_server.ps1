. "$PSScriptRoot\Common.ps1"
Push-Location $RepoRoot
cargo build --release -p tfps-server -p tfps-devclient
Pop-Location
