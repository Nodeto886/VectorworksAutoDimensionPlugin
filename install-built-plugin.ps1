param(
    [ValidateSet("2025", "2026")]
    [string]$Version = "2025",

    [string]$SourceDirectory = "",

    [switch]$All
)

$ErrorActionPreference = "Stop"
$PluginBaseName = "KeeplAutoDimTest"
$LegacyPluginBaseName = "AutoDimensionPlugin"

function Install-AutoDimensionPlugin {
    param(
        [Parameter(Mandatory = $true)]
        [ValidateSet("2025", "2026")]
        [string]$TargetVersion,

        [string]$SourceRoot
    )

    if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
        $SourceRoot = Join-Path $PSScriptRoot "dist\$TargetVersion"
    }

    $sourceVlb = Join-Path $SourceRoot "$PluginBaseName.vlb"
    $sourceVwr = Join-Path $SourceRoot "$PluginBaseName.vwr"

    if (!(Test-Path -LiteralPath $sourceVlb)) {
        throw "Missing plugin binary: $sourceVlb"
    }

    if (!(Test-Path -LiteralPath $sourceVwr)) {
        throw "Missing plugin resources: $sourceVwr"
    }

    $target = Join-Path $env:APPDATA "Nemetschek\Vectorworks\$TargetVersion\Plug-ins"
    New-Item -ItemType Directory -Force -Path $target | Out-Null

    $legacyFiles = @("$LegacyPluginBaseName.vlb", "$LegacyPluginBaseName.vwr")
    $existingLegacyFiles = @($legacyFiles | Where-Object { Test-Path -LiteralPath (Join-Path $target $_) -PathType Leaf })
    if ($existingLegacyFiles.Count -gt 0) {
        $quarantine = Join-Path (Split-Path -Parent $target) ("KeeplAutoDimTest-quarantine\" + (Get-Date -Format "yyyyMMddHHmmss") + "-" + [guid]::NewGuid().ToString("N"))
        New-Item -ItemType Directory -Force -Path $quarantine | Out-Null
        foreach ($legacyFile in $existingLegacyFiles) {
            Move-Item -LiteralPath (Join-Path $target $legacyFile) -Destination (Join-Path $quarantine $legacyFile)
        }
        Write-Host "Legacy plugin files moved to: $quarantine"
    }

    Copy-Item -LiteralPath $sourceVlb -Destination (Join-Path $target "$PluginBaseName.vlb") -Force
    Copy-Item -LiteralPath $sourceVwr -Destination (Join-Path $target "$PluginBaseName.vwr") -Force

    Write-Host "Installed $PluginBaseName $TargetVersion to: $target"
}

if ($All) {
    Install-AutoDimensionPlugin -TargetVersion "2025" -SourceRoot ""
    Install-AutoDimensionPlugin -TargetVersion "2026" -SourceRoot ""
}
else {
    Install-AutoDimensionPlugin -TargetVersion $Version -SourceRoot $SourceDirectory
}
