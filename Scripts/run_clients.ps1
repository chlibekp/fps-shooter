# Scripts\run_clients.ps1 [-Count 2] [-Server 127.0.0.1:7777]
param([int]$Count = 2, [string]$Server = "127.0.0.1:7777")
. "$PSScriptRoot\Common.ps1"
Require-UE
$editor = Join-Path $env:UE_ROOT "Engine\Binaries\Win64\UnrealEditor.exe"
for ($i = 1; $i -le $Count; $i++) {
    $x = ($i - 1) * 960
    Start-Process $editor -ArgumentList "`"$Project`"", "-game", "-windowed", "-ResX=960", "-ResY=540", "-WinX=$x", "-WinY=40", "-connect=$Server", "-name=Player$i", "-log", "-NOSPLASH"
}
