# Scripts\run_clients.ps1 [-Count 2] [-Server 127.0.0.1:7777] [-Map test_arena]
# Starts several Godot game clients (run Scripts/godot/fetch_godot.sh and
# Scripts/godot/build_extension.sh first, e.g. from Git Bash).
param([int]$Count = 2, [string]$Server = "127.0.0.1:7777", [string]$Map = "test_arena")
. "$PSScriptRoot\Common.ps1"
$godot = Join-Path $RepoRoot "build\godot\godot.exe"
for ($i = 1; $i -le $Count; $i++) {
    $x = ($i - 1) * 960
    Start-Process $godot -ArgumentList "--path", (Join-Path $RepoRoot "Game"), "--windowed", "--resolution", "960x540", "--position", "$x,40", "--", "--connect", $Server, "--map", $Map, "--name", "Player$i"
}
