param(
    [ValidateSet("2025", "2026")]
    [string]$Version = "2025",

    [string]$SourceDirectory = "",

    [switch]$All
)

$ErrorActionPreference = "Stop"

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

    $sourceVlb = Join-Path $SourceRoot "AutoDimensionPlugin.vlb"
    $sourceVwr = Join-Path $SourceRoot "AutoDimensionPlugin.vwr"

    if (!(Test-Path -LiteralPath $sourceVlb)) {
        throw "Missing plugin binary: $sourceVlb"
    }

    if (!(Test-Path -LiteralPath $sourceVwr)) {
        throw "Missing plugin resources: $sourceVwr"
    }

    $target = Join-Path $env:APPDATA "Nemetschek\Vectorworks\$TargetVersion\Plug-ins\AutoDimensionPlugin"
    New-Item -ItemType Directory -Force -Path $target | Out-Null

    Copy-Item -LiteralPath $sourceVlb -Destination (Join-Path $target "AutoDimensionPlugin.vlb") -Force
    Copy-Item -LiteralPath $sourceVwr -Destination (Join-Path $target "AutoDimensionPlugin.vwr") -Force

    Write-Host "Installed AutoDimensionPlugin $TargetVersion to: $target"
}

if ($All) {
    Install-AutoDimensionPlugin -TargetVersion "2025" -SourceRoot ""
    Install-AutoDimensionPlugin -TargetVersion "2026" -SourceRoot ""
}
else {
    Install-AutoDimensionPlugin -TargetVersion $Version -SourceRoot $SourceDirectory
}
