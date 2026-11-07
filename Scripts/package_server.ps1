. "$PSScriptRoot\Common.ps1"
Push-Location $RepoRoot
cargo build --release -p tfps-server
$out = "Dist\server-windows-x64"
Remove-Item -Recurse -Force $out -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path "$out\Shared\Definitions\maps" | Out-Null
Copy-Item "target\release\tfps-server.exe" $out
Copy-Item "Shared\Definitions\maps\*.json" "$out\Shared\Definitions\maps"
Copy-Item "Shared\Definitions\weapons.json" "$out\Shared\Definitions"
Copy-Item "Shared\Definitions\gadgets.json" "$out\Shared\Definitions"
Pop-Location
