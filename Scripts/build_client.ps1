# Scripts\build_client.ps1 [Editor|Game]
param([string]$Kind = "Editor")
. "$PSScriptRoot\Common.ps1"
Require-UE
Sync-Definitions
$target = if ($Kind -eq "Editor") { "TacticalFPSEditor" } else { "TacticalFPS" }
& (Join-Path $env:UE_ROOT "Engine\Build\BatchFiles\Build.bat") $target Win64 Development "-Project=$Project" -WaitMutex
