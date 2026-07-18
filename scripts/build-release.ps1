param(
    [ValidateSet("2025", "2026")]
    [string[]]$Version = @("2025", "2026"),

    [switch]$Package
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$msbuild = "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe"
$sharedOutput = "C:\Users\keepl\Downloads\Output\Plug-Ins\Release"
$pluginBaseName = "KeeplAutoDimTest"

if (!(Test-Path -LiteralPath $msbuild)) {
    throw "MSBuild not found: $msbuild"
}

foreach ($targetVersion in $Version) {
    $project = Join-Path $repoRoot "sdk-projects\$targetVersion\AutoDimensionPlugin\AutoDimensionPlugin.vcxproj"
    if (!(Test-Path -LiteralPath $project)) {
        throw "SDK project not found: $project"
    }

    & $msbuild $project /p:Configuration=Release /p:Platform=x64 /m
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed for Vectorworks $targetVersion"
    }

    $dist = Join-Path $repoRoot "dist\$targetVersion"
    New-Item -ItemType Directory -Force -Path $dist | Out-Null

    Copy-Item -LiteralPath (Join-Path $sharedOutput "$pluginBaseName.vlb") -Destination (Join-Path $dist "$pluginBaseName.vlb") -Force
    Copy-Item -LiteralPath (Join-Path $sharedOutput "$pluginBaseName.vwr") -Destination (Join-Path $dist "$pluginBaseName.vwr") -Force

    if ($Package) {
        $zipPath = Join-Path $repoRoot "dist\$pluginBaseName-Vectorworks-$targetVersion.zip"
        if (Test-Path -LiteralPath $zipPath) {
            Remove-Item -LiteralPath $zipPath -Force
        }
        Compress-Archive -LiteralPath (Join-Path $dist "$pluginBaseName.vlb"), (Join-Path $dist "$pluginBaseName.vwr") -DestinationPath $zipPath
    }

    Write-Host "Built Vectorworks $targetVersion release into: $dist"
}
