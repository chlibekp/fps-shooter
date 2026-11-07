# Dot-sourced by the other PowerShell scripts.
$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Project = Join-Path $RepoRoot "Client\TacticalFPS.uproject"

function Require-UE {
    if (-not $env:UE_ROOT -or -not (Test-Path (Join-Path $env:UE_ROOT "Engine"))) {
        Write-Error "Set UE_ROOT to your Unreal Engine 5.4+ install, e.g. `$env:UE_ROOT='C:\Program Files\Epic Games\UE_5.4'"
        exit 2
    }
}

function Sync-Definitions {
    $dest = Join-Path $RepoRoot "Client\Content\Definitions\Maps"
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    Copy-Item (Join-Path $RepoRoot "Shared\Definitions\maps\*.json") $dest -Force
}
