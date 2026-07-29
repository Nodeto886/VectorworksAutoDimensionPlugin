param(
    [ValidateSet("2025", "2026")]
    [string[]]$Version = @("2025", "2026"),

    [switch]$Package,

    [string]$SdkRoot,

    [string]$OutputRoot,

    [string]$MsBuildPath = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($SdkRoot)) {
    $SdkRoot = Join-Path $repoRoot "SDKLib"
}
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $repoRoot "build\windows"
}
$sdkRoot = (Resolve-Path -LiteralPath $SdkRoot).Path
if (Test-Path -LiteralPath (Join-Path $sdkRoot "Include")) {
    $sdkRoot = (Resolve-Path -LiteralPath (Join-Path $sdkRoot ".." )).Path
}
if (!(Test-Path -LiteralPath (Join-Path $sdkRoot "SDKLib\Include"))) {
    throw "Invalid Vectorworks SDK root: $sdkRoot"
}

if ([string]::IsNullOrWhiteSpace($MsBuildPath)) {
    $msbuildCommand = Get-Command msbuild.exe -ErrorAction SilentlyContinue
    if ($msbuildCommand) {
        $MsBuildPath = $msbuildCommand.Source
    }
}
$msbuild = $MsBuildPath
$pluginBaseName = "KeeplAutoDimTest"

if ([string]::IsNullOrWhiteSpace($msbuild) -or !(Test-Path -LiteralPath $msbuild)) {
    throw "MSBuild not found: $msbuild"
}

foreach ($targetVersion in $Version) {
    $project = Join-Path $repoRoot "sdk-projects\$targetVersion\AutoDimensionPlugin\AutoDimensionPlugin.vcxproj"
    if (!(Test-Path -LiteralPath $project)) {
        throw "SDK project not found: $project"
    }

    $buildOutput = Join-Path $OutputRoot $targetVersion
    $buildIntermediate = Join-Path $OutputRoot "build\$targetVersion"
    New-Item -ItemType Directory -Force -Path $buildOutput, $buildIntermediate | Out-Null

    & $msbuild $project /p:Configuration=Release /p:Platform=x64 /p:VectorworksSDKRoot="$sdkRoot" /p:BranchPath="$sdkRoot" /p:PlatformToolset=v143 /p:VCToolsVersion= /p:OutDir="$buildOutput\" /p:IntDir="$buildIntermediate\" /m
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed for Vectorworks $targetVersion"
    }

    $dist = Join-Path $repoRoot "dist\$targetVersion"
    New-Item -ItemType Directory -Force -Path $dist | Out-Null

    Copy-Item -LiteralPath (Join-Path $buildOutput "$pluginBaseName.vlb") -Destination (Join-Path $dist "$pluginBaseName.vlb") -Force
    Copy-Item -LiteralPath (Join-Path $buildOutput "$pluginBaseName.vwr") -Destination (Join-Path $dist "$pluginBaseName.vwr") -Force

    if ($Package) {
        $zipPath = Join-Path $repoRoot "dist\Keepl-Auto-Dimension-Vectorworks-$targetVersion-Windows.zip"
        if (Test-Path -LiteralPath $zipPath) {
            Remove-Item -LiteralPath $zipPath -Force
        }
        Compress-Archive -LiteralPath (Join-Path $dist "$pluginBaseName.vlb"), (Join-Path $dist "$pluginBaseName.vwr") -DestinationPath $zipPath
    }

    Write-Host "Built Vectorworks $targetVersion release into: $dist"
}
