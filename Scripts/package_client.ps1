# Scripts\package_client.ps1 [-Platform Win64|Linux] [-Config Shipping|Development]
param([string]$Platform = "Win64", [string]$Config = "Shipping")
. "$PSScriptRoot\Common.ps1"
Require-UE
Sync-Definitions
$out = Join-Path $RepoRoot "Dist\client-$Platform"
& (Join-Path $env:UE_ROOT "Engine\Build\BatchFiles\RunUAT.bat") BuildCookRun "-project=$Project" -noP4 "-platform=$Platform" "-clientconfig=$Config" -build -cook -stage -pak -archive "-archivedirectory=$out" -utf8output
