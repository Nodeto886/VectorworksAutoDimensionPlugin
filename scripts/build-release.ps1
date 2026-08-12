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
$pluginBaseName = "KeeplAutoDimTest"
$legacyPluginBaseName = "AutoDimensionPlugin"

function Convert-ToAbsolutePath {
    param([Parameter(Mandatory)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return (Join-Path $repoRoot $Path)
}

function Test-VectorworksWindowsSdkRoot {
    param([Parameter(Mandatory)][string]$Root)

    $requiredFiles = @(
        "SDKLib\Include\OnlyWin\NNA_PluginBuild_RELEASE.props",
        "SDKLib\LibWin\Release\VWSDK.lib",
        "SDKLib\ToolsWin\BuildVWR\buildvwr.exe"
    )
    foreach ($relativePath in $requiredFiles) {
        if (!(Test-Path -LiteralPath (Join-Path $Root $relativePath) -PathType Leaf)) {
            return $false
        }
    }
    return $true
}

function Find-VectorworksWindowsSdkRoot {
    param(
        [string]$RequestedRoot,
        [Parameter(Mandatory)][string]$TargetVersion
    )

    $candidateRoots = @()
    if (![string]::IsNullOrWhiteSpace($RequestedRoot)) {
        $candidateRoots += (Convert-ToAbsolutePath $RequestedRoot)
    }
    elseif (![string]::IsNullOrWhiteSpace($env:VECTORWORKS_SDK_ROOT)) {
        $candidateRoots += (Convert-ToAbsolutePath $env:VECTORWORKS_SDK_ROOT)
    }
    else {
        $downloadsRoot = Split-Path -Parent $repoRoot
        $candidateRoots += (Join-Path $repoRoot "SDKLib")
        $candidateRoots += (Join-Path $downloadsRoot "VectorworksSDK\$TargetVersion")
        $candidateRoots += (Join-Path $downloadsRoot "VectorworksSDK")
    }

    foreach ($candidate in ($candidateRoots | Select-Object -Unique)) {
        if (!(Test-Path -LiteralPath $candidate -PathType Container)) {
            continue
        }

        $candidatePath = (Resolve-Path -LiteralPath $candidate).Path
        $targetPathPattern = "(?i)[\\/]$([regex]::Escape($TargetVersion))(?:[\\/]|$)"
        $candidateRequiresTargetVersion = $candidatePath -match "(?i)[\\/]VectorworksSDK(?:[\\/]|$)" -and $candidatePath -notmatch $targetPathPattern
        $possibleRoots = @($candidatePath)
        if (Test-Path -LiteralPath (Join-Path $candidatePath "Include") -PathType Container) {
            $possibleRoots += (Split-Path -Parent $candidatePath)
        }

        $nestedSdkLibs = Get-ChildItem -LiteralPath $candidatePath -Directory -Recurse -Filter SDKLib -ErrorAction SilentlyContinue
        if ($candidateRequiresTargetVersion) {
            $nestedSdkLibs = $nestedSdkLibs | Where-Object { $_.FullName -match $targetPathPattern }
        }
        foreach ($sdkLib in $nestedSdkLibs) {
            $possibleRoots += (Split-Path -Parent $sdkLib.FullName)
        }

        foreach ($possibleRoot in ($possibleRoots | Select-Object -Unique)) {
            if ($candidateRequiresTargetVersion -and $possibleRoot -notmatch $targetPathPattern) {
                continue
            }
            if (Test-VectorworksWindowsSdkRoot $possibleRoot) {
                return (Resolve-Path -LiteralPath $possibleRoot).Path
            }
        }
    }

    return $null
}

function Find-MSBuildPath {
    param([string]$RequestedPath)

    if (![string]::IsNullOrWhiteSpace($RequestedPath)) {
        $path = Convert-ToAbsolutePath $RequestedPath
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            return (Resolve-Path -LiteralPath $path).Path
        }
        throw "MSBuild not found: $RequestedPath"
    }

    $msbuildCommand = Get-Command msbuild.exe -CommandType Application -ErrorAction SilentlyContinue
    if ($msbuildCommand) {
        return $msbuildCommand.Source
    }

    $programFilesX86 = ${env:ProgramFiles(x86)}
    $programFiles = $env:ProgramFiles
    $knownPaths = @(
        (Join-Path $programFilesX86 "Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"),
        (Join-Path $programFilesX86 "Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"),
        (Join-Path $programFiles "Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe")
    )
    foreach ($knownPath in $knownPaths) {
        if (Test-Path -LiteralPath $knownPath -PathType Leaf) {
            return (Resolve-Path -LiteralPath $knownPath).Path
        }
    }

    $vswherePaths = @(
        (Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"),
        (Join-Path $programFiles "Microsoft Visual Studio\Installer\vswhere.exe")
    )
    foreach ($vswherePath in ($vswherePaths | Select-Object -Unique)) {
        if (!(Test-Path -LiteralPath $vswherePath -PathType Leaf)) {
            continue
        }
        $foundPath = & $vswherePath -latest -products '*' -requires Microsoft.Component.MSBuild -find "MSBuild\Current\Bin\MSBuild.exe" 2>$null | Select-Object -First 1
        if ($foundPath -and (Test-Path -LiteralPath $foundPath -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $foundPath).Path
        }
    }

    return $null
}

$outputRootInput = if ([string]::IsNullOrWhiteSpace($OutputRoot)) { Join-Path $repoRoot "build\windows" } else { Convert-ToAbsolutePath $OutputRoot }
New-Item -ItemType Directory -Force -Path $outputRootInput | Out-Null
$outputRoot = (Resolve-Path -LiteralPath $outputRootInput).Path
$msbuild = Find-MSBuildPath $MsBuildPath
if ([string]::IsNullOrWhiteSpace($msbuild)) {
    throw "MSBuild not found. Add MSBuild to PATH or pass -MsBuildPath."
}

foreach ($targetVersion in $Version) {
    $resolvedSdkRoot = Find-VectorworksWindowsSdkRoot $SdkRoot $targetVersion
    if ([string]::IsNullOrWhiteSpace($resolvedSdkRoot)) {
        throw "Vectorworks Windows SDK $targetVersion not found. Expected SDKLib\\Include\\OnlyWin\\NNA_PluginBuild_RELEASE.props, SDKLib\\LibWin\\Release\\VWSDK.lib, and SDKLib\\ToolsWin\\BuildVWR\\buildvwr.exe. Pass -SdkRoot or set VECTORWORKS_SDK_ROOT."
    }
    Write-Host "Using Vectorworks $targetVersion Windows SDK: $resolvedSdkRoot"

    $project = Join-Path $repoRoot "sdk-projects\$targetVersion\AutoDimensionPlugin\AutoDimensionPlugin.vcxproj"
    if (!(Test-Path -LiteralPath $project)) {
        throw "SDK project not found: $project"
    }

    $buildOutput = Join-Path $OutputRoot $targetVersion
    $buildIntermediate = Join-Path $OutputRoot "build\$targetVersion"
    New-Item -ItemType Directory -Force -Path $buildOutput, $buildIntermediate | Out-Null

    & $msbuild $project /p:Configuration=Release /p:Platform=x64 /p:VectorworksSDKRoot="$resolvedSdkRoot" /p:BranchPath="$resolvedSdkRoot" /p:PlatformToolset=v143 /p:OutDir="$buildOutput\" /p:IntDir="$buildIntermediate\" /m
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed for Vectorworks $targetVersion"
    }

    $dist = Join-Path $repoRoot "dist\$targetVersion"
    New-Item -ItemType Directory -Force -Path $dist | Out-Null

    # Historical artifacts remain in the repository but are never package inputs.
    $legacyArtifacts = Get-ChildItem -LiteralPath $dist -File -Filter "$legacyPluginBaseName.*" -ErrorAction SilentlyContinue
    if ($legacyArtifacts) {
        $legacyNames = $legacyArtifacts.Name -join ', '
        Write-Warning "Legacy artifacts are rejected from release packages: $legacyNames"
    }

    Copy-Item -LiteralPath (Join-Path $buildOutput "$pluginBaseName.vlb") -Destination (Join-Path $dist "$pluginBaseName.vlb") -Force
    Copy-Item -LiteralPath (Join-Path $buildOutput "$pluginBaseName.vwr") -Destination (Join-Path $dist "$pluginBaseName.vwr") -Force

    if ($Package) {
        $zipPath = Join-Path $repoRoot "dist\Keepl-Auto-Dimension-Vectorworks-$targetVersion-Windows.zip"
        if (Test-Path -LiteralPath $zipPath) {
            Remove-Item -LiteralPath $zipPath -Force
        }
        $packageRoot = Join-Path $buildOutput ("package-" + [guid]::NewGuid().ToString("N"))
        New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
        try {
            $packageFiles = @("$pluginBaseName.vlb", "$pluginBaseName.vwr")
            foreach ($packageFile in $packageFiles) {
                Copy-Item -LiteralPath (Join-Path $dist $packageFile) -Destination (Join-Path $packageRoot $packageFile) -Force
            }
            Compress-Archive -LiteralPath (Join-Path $packageRoot "$pluginBaseName.vlb"), (Join-Path $packageRoot "$pluginBaseName.vwr") -DestinationPath $zipPath

            $archiveEntries = @(Expand-Archive -LiteralPath $zipPath -DestinationPath (Join-Path $packageRoot "verified") -PassThru |
                ForEach-Object { $_.Name } |
                Sort-Object)
            if (@(Compare-Object -ReferenceObject ($packageFiles | Sort-Object) -DifferenceObject $archiveEntries).Count -ne 0) {
                throw "Invalid release package contents: $zipPath"
            }
        }
        finally {
            Remove-Item -LiteralPath $packageRoot -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    Write-Host "Built Vectorworks $targetVersion release into: $dist"
}
