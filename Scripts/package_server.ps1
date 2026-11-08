# Scripts\package_server.ps1 [-OutDir Dist\server-windows-x64]
# Packages the dedicated server, the lobby service and the headless bot client
# (tfps-bot) with the shared definitions they load at runtime.
param([string]$OutDir = "Dist\server-windows-x64")
. "$PSScriptRoot\Common.ps1"
Push-Location $RepoRoot
cargo build --release -p tfps-server -p tfps-lobby -p tfps-devclient
if ($LASTEXITCODE -ne 0) { Pop-Location; exit $LASTEXITCODE }
$out = $OutDir
Remove-Item -Recurse -Force $out -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path "$out\Shared\Definitions\maps" | Out-Null
Copy-Item "target\release\tfps-server.exe" $out
Copy-Item "target\release\tfps-lobby.exe" $out
Copy-Item "target\release\tfps-bot.exe" $out
Copy-Item "Shared\Definitions\maps\*.json" "$out\Shared\Definitions\maps"
Copy-Item "Shared\Definitions\weapons.json" "$out\Shared\Definitions"
Copy-Item "Shared\Definitions\gadgets.json" "$out\Shared\Definitions"
Pop-Location
