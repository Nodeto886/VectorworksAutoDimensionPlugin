param(
    [string]$VectorworksRoot = "C:\Program Files\Vectorworks 2025"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$sourceRoot = Join-Path $repoRoot "dist\2025"
$sourceVlb = Join-Path $sourceRoot "AutoDimensionPlugin.vlb"
$sourceVwr = Join-Path $sourceRoot "AutoDimensionPlugin.vwr"
$appPlugRoot = Join-Path $VectorworksRoot "Plug-Ins"
$userPlugRoot = Join-Path $env:APPDATA "Nemetschek\Vectorworks\2025\Plug-ins"
$quarantineRoot = Join-Path $repoRoot ("install-quarantine\2025-user-" + (Get-Date -Format "yyyyMMddHHmmss"))
$cacheBackupRoot = Join-Path $repoRoot ("plugin-cache-backup\2025-user-" + (Get-Date -Format "yyyyMMddHHmmss"))

if (!(Test-Path -LiteralPath $sourceVlb -PathType Leaf)) {
    throw "Missing plugin binary: $sourceVlb"
}

if (!(Test-Path -LiteralPath $sourceVwr -PathType Leaf)) {
    throw "Missing plugin resources: $sourceVwr"
}

if (!(Test-Path -LiteralPath $appPlugRoot -PathType Container)) {
    throw "Vectorworks Plug-Ins folder not found: $appPlugRoot"
}

New-Item -ItemType Directory -Force -Path $quarantineRoot | Out-Null
New-Item -ItemType Directory -Force -Path $cacheBackupRoot | Out-Null

if (Test-Path -LiteralPath $userPlugRoot -PathType Container) {
    foreach ($name in @(
        "AutoDimensionPlugin.vlb",
        "AutoDimensionPlugin.vwr",
        "AutoDimensionPlugin",
        "AutoDimension.py",
        "AutoDimension_Standalone.py",
        "AutoDimension.vsm"
    )) {
        $path = Join-Path $userPlugRoot $name
        if (Test-Path -LiteralPath $path) {
            Move-Item -LiteralPath $path -Destination (Join-Path $quarantineRoot $name) -Force
        }
    }

    foreach ($name in @("VWPluginLibraryRoutines.h", "VWPluginLibraryRoutines.p", "WorksheetFunctionOptionsRegistry.txt")) {
        $path = Join-Path $userPlugRoot $name
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Move-Item -LiteralPath $path -Destination (Join-Path $cacheBackupRoot $name) -Force
        }
    }
}

Copy-Item -LiteralPath $sourceVlb -Destination (Join-Path $appPlugRoot "AutoDimensionPlugin.vlb") -Force
Copy-Item -LiteralPath $sourceVwr -Destination (Join-Path $appPlugRoot "AutoDimensionPlugin.vwr") -Force

Write-Host "Installed AutoDimensionPlugin 2025 to: $appPlugRoot"
Write-Host "User duplicate files moved to: $quarantineRoot"
Write-Host "User plugin cache moved to: $cacheBackupRoot"
