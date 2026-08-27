param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "build\algo-test"
$results = [System.Collections.Generic.List[hashtable]]::new()

function Add-Result($name, $pass, $detail) {
    $results.Add(@{
        Name = $name
        Pass = $pass
        Detail = $detail
    })
}

function Invoke-Capture($exe, $args) {
    $output = & $exe @args 2>&1
    $exitCode = $LASTEXITCODE
    return @{ Output = ($output | Out-String); ExitCode = $exitCode }
}

function Run-Test($name, $exe, $args) {
    Write-Host -NoNewline "`n>>> $name ... "
    try {
        $captured = Invoke-Capture $exe $args
        if ($captured.ExitCode -eq 0) {
            Write-Host "PASS" -ForegroundColor Green
        }
        else {
            Write-Host "FAIL (exit=$($captured.ExitCode))" -ForegroundColor Red
        }
        $detail = $captured.Output.Trim()
        if ($detail.Length -gt 400) { $detail = $detail.Substring(0, 400) + "..." }
        Add-Result $name ($captured.ExitCode -eq 0) $detail
    }
    catch {
        Write-Host "ERROR" -ForegroundColor Red
        Add-Result $name $false $_.Exception.Message
    }
}

# ------ 1. CTest (geometry + algorithm + property) ------
if (!(Test-Path (Join-Path $buildDir "CTestTestfile.cmake"))) {
    Write-Host "Configuring CTest build..." -ForegroundColor Cyan
    $cfg = Invoke-Capture "cmake" @("-S", $repoRoot, "-B", $buildDir, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_CXX_COMPILER=g++", "-DBUILD_TESTING=ON")
    if ($cfg.ExitCode -ne 0) {
        Add-Result "CTest configure" $false $cfg.Output
    }
    $build = Invoke-Capture "cmake" @("--build", $buildDir)
    if ($build.ExitCode -ne 0) {
        Add-Result "CTest build" $false $build.Output
    }
}
Run-Test "CTest (geometry+algorithm+property)" "ctest" @("--test-dir", $buildDir, "--output-on-failure")

# ------ 2. Python numerical regression ------
Run-Test "Python numerical regression" "python" @("$repoRoot\tools\test_autodim_geometry.py")

# ------ 3. SDK source invariant gate ------
$src2025 = "$repoRoot\sdk-projects\2025\AutoDimensionPlugin\Source\AutoDimensionObj.cpp"
$src2026 = "$repoRoot\sdk-projects\2026\AutoDimensionPlugin\Source\AutoDimensionObj.cpp"
Run-Test "SDK source invariants" "python" @("$repoRoot\tests\test_sdk_source_invariants.py", "--source-2025", "$src2025", "--source-2026", "$src2026")

# ------ 4. g++ syntax check (2026, both C++17 and C++20) ------
$stagedSdk = Join-Path $repoRoot ".sdk-stage-20260729-complete\2026\SDKLib\Include"
$prefixInc = Join-Path $repoRoot "sdk-projects\2026\AutoDimensionPlugin\Source\Prefix"
$srcInc = Join-Path $repoRoot "sdk-projects\2026\AutoDimensionPlugin\Source"

# The staged VW SDK is the Mac layout; under MinGW the vendored SDK headers emit
# several known, inherent errors (DebugBase.h DebugBreak, VWVariant.h
# GS_DisposePtr/GS_NewPtr, TypesBase.h lambda deduction) that cascade a spurious
# Point2 error into SDKComplexGeometry.h. C++20 additionally trips a MinGW
# libstdc++ optional header. None of these occur in the real MSVC build. The gate
# therefore FAILS only on errors inside OUR source files (AutoDimensionObj.cpp
# and include/vwad/*); the known SDK/toolchain noise is allowed and counted
# separately.

function Test-SyntaxCheck($std) {
    $out = & g++ "-std=$std" -fsyntax-only "-I$prefixInc" "-I$srcInc" "-I$stagedSdk" "-I$stagedSdk\VWFC" "-I$repoRoot" `
        -D_WINDOWS -DWIN32 -D_USRDLL -D_WIN_EXTERNAL_ -DRELEASE_BLD -D_WINDLL -D_CRT_SECURE_NO_WARNINGS -DNDEBUG "$src2026" 2>&1
    $exitCode = $LASTEXITCODE
    $errorLines = @($out | Select-String -Pattern "error:")
    $realErrors = @()
    foreach ($line in $errorLines) {
        $text = $line.ToString()
        # Noise = vendored SDK headers, MinGW libstdc++, or the documented
        # cascade into SDKComplexGeometry.h (Point2 brace-init) that vanishes
        # once the SDK header errors are resolved.
        $isCascadeNoise = ($text -match "SDKComplexGeometry\.h" -and $text -match "Point2")
        $isProjectError = $isCascadeNoise -or
            $text -match "AutoDimensionObj\.cpp" -or
            $text -match "include[/\\]vwad[/\\]" -or
            $text -match "tests[/\\]"
        if (!$isProjectError) { continue }
        if (!$isCascadeNoise) { $realErrors += $text }
    }
    $detail = "std=$std; exit=$exitCode"
    if ($realErrors.Count -gt 0) {
        $detail += "; REAL ERRORS: " + (($realErrors | Select-Object -First 5) -join " | ")
    }
    return @{ Pass = ($realErrors.Count -eq 0); Detail = $detail }
}

if (Test-Path $stagedSdk) {
    $cxx17 = Test-SyntaxCheck "c++17"
    $cxx20 = Test-SyntaxCheck "c++20"
    Add-Result "g++ syntax check (C++17)" $cxx17.Pass $cxx17.Detail
    Add-Result "g++ syntax check (C++20)" $cxx20.Pass $cxx20.Detail
}
else {
    Add-Result "g++ syntax check (C++17)" $false "SDK staging directory not found"
    Add-Result "g++ syntax check (C++20)" $false "SDK staging directory not found"
}

# ------ 5. Optional real plugin MSBuild (2025/2026) ------
if (!$SkipBuild) {
    $buildOut = Join-Path $repoRoot "build\windows"
    Run-Test "MSBuild plugin (2025)" "powershell" @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "$repoRoot\scripts\build-release.ps1", "-Version", "2025", "-OutputRoot", $buildOut)
    Run-Test "MSBuild plugin (2026)" "powershell" @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "$repoRoot\scripts\build-release.ps1", "-Version", "2026", "-OutputRoot", $buildOut)
}

# ------ Summary ------
Write-Host "`n`n=============================== TEST SUMMARY ==============================="
$passed = 0
$failed = 0
foreach ($r in $results) {
    $status = if ($r.Pass) { "PASS" } else { "FAIL" }
    $color = if ($r.Pass) { "Green" } else { "Red" }
    Write-Host "  [$status] $($r.Name)" -ForegroundColor $color
    if ($r.Pass) { ++$passed } else { ++$failed }
}
Write-Host "==========================================================================="
Write-Host "  $passed passed, $failed failed" -ForegroundColor $(if ($failed -eq 0) { "Green" } else { "Red" })
Write-Host "==========================================================================="
exit $(if ($failed -gt 0) { 1 } else { 0 })