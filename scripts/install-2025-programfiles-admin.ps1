param(
    [ValidateSet("2025", "2026")]
    [string]$Version = "2025",

    [string]$VectorworksRoot = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$pluginBaseName = "KeeplAutoDimTest"
$sourceRoot = Join-Path $repoRoot "dist\$Version"
$sourceVlb = Join-Path $sourceRoot "$pluginBaseName.vlb"
$sourceVwr = Join-Path $sourceRoot "$pluginBaseName.vwr"
if ([string]::IsNullOrWhiteSpace($VectorworksRoot)) {
    $VectorworksRoot = "C:\Program Files\Vectorworks $Version"
}
$appPlugRoot = Join-Path $VectorworksRoot "Plug-Ins"
$userPlugRoot = Join-Path $env:APPDATA "Nemetschek\Vectorworks\$Version\Plug-ins"
$quarantineId = Get-Date -Format "yyyyMMddHHmmss"
$userQuarantineRoot = Join-Path $repoRoot ("install-quarantine\$Version-user-" + $quarantineId)
$appQuarantineRoot = Join-Path $repoRoot ("install-quarantine\$Version-programfiles-" + $quarantineId)
$cacheBackupRoot = Join-Path $repoRoot ("plugin-cache-backup\$Version-user-" + (Get-Date -Format "yyyyMMddHHmmss"))

if (!(Test-Path -LiteralPath $sourceVlb -PathType Leaf)) {
    throw "Missing plugin binary: $sourceVlb"
}

if (!(Test-Path -LiteralPath $sourceVwr -PathType Leaf)) {
    throw "Missing plugin resources: $sourceVwr"
}

if (!(Test-Path -LiteralPath $appPlugRoot -PathType Container)) {
    throw "Vectorworks Plug-Ins folder not found: $appPlugRoot"
}

New-Item -ItemType Directory -Force -Path $userQuarantineRoot, $appQuarantineRoot | Out-Null
New-Item -ItemType Directory -Force -Path $cacheBackupRoot | Out-Null

if (Test-Path -LiteralPath $userPlugRoot -PathType Container) {
    foreach ($name in @(
        "AutoDimensionPlugin.vlb",
        "AutoDimensionPlugin.vwr",
        "AutoDimensionPlugin",
        "KeeplAutoDimTest.vlb",
        "KeeplAutoDimTest.vwr",
        "AutoDimension.py",
        "AutoDimension_Standalone.py",
        "AutoDimension.vsm"
    )) {
        $path = Join-Path $userPlugRoot $name
        if (Test-Path -LiteralPath $path) {
            Move-Item -LiteralPath $path -Destination (Join-Path $userQuarantineRoot $name)
        }
    }

    foreach ($name in @("VWPluginLibraryRoutines.h", "VWPluginLibraryRoutines.p", "WorksheetFunctionOptionsRegistry.txt")) {
        $path = Join-Path $userPlugRoot $name
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Move-Item -LiteralPath $path -Destination (Join-Path $cacheBackupRoot $name) -Force
        }
    }
}

foreach ($name in @(
    "AutoDimensionPlugin.vlb",
    "AutoDimensionPlugin.vwr",
    "$pluginBaseName.vlb",
    "$pluginBaseName.vwr"
)) {
    $existingAppPath = Join-Path $appPlugRoot $name
    if (Test-Path -LiteralPath $existingAppPath -PathType Leaf) {
        Move-Item -LiteralPath $existingAppPath -Destination (Join-Path $appQuarantineRoot $name)
    }
}

Copy-Item -LiteralPath $sourceVlb -Destination (Join-Path $appPlugRoot "$pluginBaseName.vlb") -Force
Copy-Item -LiteralPath $sourceVwr -Destination (Join-Path $appPlugRoot "$pluginBaseName.vwr") -Force

Write-Host "Installed $pluginBaseName $Version to: $appPlugRoot"
Write-Host "User duplicate files moved to: $userQuarantineRoot"
Write-Host "Program Files legacy files moved to: $appQuarantineRoot"
Write-Host "User plugin cache moved to: $cacheBackupRoot"
