$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$roots = @(
    (Join-Path $PSScriptRoot "..\sdk-projects\2025\AutoDimensionPlugin\KeeplAutoDimTest.vwr\Images"),
    (Join-Path $PSScriptRoot "..\sdk-projects\2026\AutoDimensionPlugin\KeeplAutoDimTest.vwr\Images")
)
$names = @(
    "ModeAuto", "ModeContinuous", "ModeLine", "ModeManualBlock", "ModeIntersection",
    "ModeSelection", "ModeCenters", "ModeBoundaries", "ModeClosedSpace", "ModeEnhanced",
    "ModeEditNone", "ModeConvert", "ModeTrim", "ModeAlign", "ModeSplitExtend",
    "ModeTextDirection", "ModeDimensionPoints", "ModeMerge", "ModeAvoidText", "ModeResetText",
    "ModeResetTextPosition", "ModeQuickChain"
)

function Draw-Line($g, $pen, [int]$x1, [int]$y1, [int]$x2, [int]$y2) {
    $g.DrawLine($pen, $x1, $y1, $x2, $y2)
}

function Draw-Arrow($g, $pen, [int]$x1, [int]$y1, [int]$x2, [int]$y2) {
    Draw-Line $g $pen $x1 $y1 $x2 $y2
    $dx = $x2 - $x1
    $dy = $y2 - $y1
    $length = [Math]::Max(1, [Math]::Sqrt($dx * $dx + $dy * $dy))
    $ux = $dx / $length
    $uy = $dy / $length
    $px = -$uy
    $py = $ux
    Draw-Line $g $pen $x2 $y2 ([int]($x2 - $ux * 4 + $px * 2)) ([int]($y2 - $uy * 4 + $py * 2))
    Draw-Line $g $pen $x2 $y2 ([int]($x2 - $ux * 4 - $px * 2)) ([int]($y2 - $uy * 4 - $py * 2))
}

function Draw-Icon($name, $path, [int]$scale) {
    $bitmap = New-Object System.Drawing.Bitmap (26 * $scale), (20 * $scale), ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.Clear([System.Drawing.Color]::Transparent)
    $graphics.ScaleTransform($scale, $scale)
    $main = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(245, 255, 255, 255)), 1.35
    $accent = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(245, 145, 210, 240)), 1.15
    $accentBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(245, 145, 210, 240))
    $main.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $main.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $accent.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $accent.EndCap = [System.Drawing.Drawing2D.LineCap]::Round

    switch ($name) {
        "ModeAuto" { $graphics.DrawEllipse($main, 5, 4, 10, 10); Draw-Line $graphics $accent 10 1 10 17; Draw-Line $graphics $accent 2 9 18 9; Draw-Arrow $graphics $accent 16 15 23 15 }
        "ModeContinuous" { Draw-Line $graphics $main 2 15 8 9; Draw-Line $graphics $main 8 9 14 13; Draw-Line $graphics $main 14 13 23 4; foreach ($point in @(@(0,13),@(6,7),@(12,11),@(21,2))) { $graphics.FillEllipse($accentBrush, [System.Drawing.Rectangle]::new($point[0], $point[1], 4, 4)) } }
        "ModeLine" { Draw-Line $graphics $main 3 16 22 4; $graphics.DrawEllipse($accent, 1, 14, 4, 4); $graphics.DrawEllipse($accent, 20, 2, 4, 4); Draw-Line $graphics $accent 5 16 5 19; Draw-Line $graphics $accent 20 4 23 4 }
        "ModeManualBlock" { $graphics.DrawRectangle($main, 3, 3, 13, 13); Draw-Arrow $graphics $accent 17 13 23 19 }
        "ModeIntersection" { Draw-Line $graphics $main 2 16 22 3; Draw-Line $graphics $main 2 3 22 16; $graphics.DrawEllipse($accent, 9, 8, 6, 6); Draw-Line $graphics $accent 12 1 12 7 }
        "ModeSelection" { $graphics.DrawRectangle($main, 2, 3, 7, 6); $graphics.DrawRectangle($main, 12, 3, 7, 6); $graphics.DrawRectangle($main, 7, 12, 7, 6); Draw-Arrow $graphics $accent 17 14 23 11 }
        "ModeCenters" { $graphics.DrawEllipse($main, 2, 5, 7, 7); $graphics.DrawEllipse($main, 17, 5, 7, 7); Draw-Arrow $graphics $accent 5 8 20 8; Draw-Line $graphics $accent 5 5 5 11; Draw-Line $graphics $accent 20 5 20 11 }
        "ModeBoundaries" { $graphics.DrawRectangle($main, 4, 3, 17, 13); Draw-Line $graphics $accent 1 3 4 3; Draw-Line $graphics $accent 1 16 4 16; Draw-Line $graphics $accent 1 3 1 16; Draw-Line $graphics $accent 21 3 24 3; Draw-Line $graphics $accent 21 16 24 16; Draw-Line $graphics $accent 24 3 24 16 }
        "ModeClosedSpace" { $graphics.DrawRectangle($main, 3, 3, 19, 14); foreach ($x in @(8,13,18)) { Draw-Line $graphics $accent $x 3 $x 17 }; foreach ($y in @(8,13)) { Draw-Line $graphics $accent 3 $y 22 $y } }
        "ModeEnhanced" { $graphics.DrawArc($main, 3, 3, 15, 15, 285, 180); Draw-Arrow $graphics $accent 11 11 22 4; Draw-Line $graphics $accent 11 11 7 18 }
        "ModeEditNone" { Draw-Line $graphics $main 4 3 17 15; Draw-Line $graphics $main 4 3 4 9; Draw-Line $graphics $main 4 3 10 3; Draw-Arrow $graphics $accent 16 14 23 14 }
        "ModeConvert" { Draw-Line $graphics $main 2 6 16 6; Draw-Line $graphics $main 2 14 16 14; Draw-Arrow $graphics $accent 13 3 22 10; Draw-Arrow $graphics $accent 13 17 22 10 }
        "ModeTrim" { Draw-Line $graphics $main 3 5 23 15; Draw-Line $graphics $main 3 15 23 5; Draw-Line $graphics $accent 9 2 9 18; Draw-Line $graphics $accent 7 10 11 10 }
        "ModeAlign" { foreach ($y in @(5,10,15)) { Draw-Line $graphics $main 3 $y 22 $y }; Draw-Line $graphics $accent 3 18 22 18 }
        "ModeSplitExtend" { Draw-Line $graphics $main 2 10 9 10; Draw-Line $graphics $main 16 10 23 10; Draw-Line $graphics $accent 9 7 9 13; Draw-Line $graphics $accent 16 7 16 13; Draw-Arrow $graphics $main 2 10 5 7; Draw-Arrow $graphics $main 23 10 20 7 }
        "ModeTextDirection" { $graphics.DrawRectangle($accent, 2, 4, 7, 11); Draw-Arrow $graphics $accent 15 16 15 3 }
        "ModeDimensionPoints" { Draw-Line $graphics $main 4 10 22 10; $graphics.FillEllipse($accentBrush, [System.Drawing.Rectangle]::new(2, 8, 4, 4)); $graphics.FillEllipse($accentBrush, [System.Drawing.Rectangle]::new(20, 8, 4, 4)); Draw-Line $graphics $accent 4 4 4 16; Draw-Line $graphics $accent 22 4 22 16 }
        "ModeMerge" { Draw-Line $graphics $main 2 5 12 10; Draw-Line $graphics $main 2 15 12 10; Draw-Arrow $graphics $accent 12 10 23 10 }
        "ModeAvoidText" { $graphics.DrawRectangle($main, 2, 5, 7, 9); Draw-Arrow $graphics $accent 13 10 23 10; Draw-Line $graphics $accent 13 4 13 16 }
        "ModeResetText" { $graphics.DrawRectangle($main, 2, 5, 7, 9); $graphics.DrawArc($accent, 12, 4, 10, 10, 45, 270); Draw-Line $graphics $accent 13 5 16 5; Draw-Line $graphics $accent 13 5 13 8 }
        "ModeResetTextPosition" { $graphics.DrawRectangle($main, 2, 5, 7, 9); Draw-Line $graphics $accent 14 4 14 16; Draw-Line $graphics $accent 11 7 17 7; Draw-Line $graphics $accent 11 13 17 13; Draw-Arrow $graphics $accent 14 4 12 6; Draw-Arrow $graphics $accent 14 16 12 14 }
        "ModeQuickChain" { Draw-Arrow $graphics $main 3 12 7 12; Draw-Arrow $graphics $main 15 12 11 12; Draw-Arrow $graphics $accent 15 12 23 12; Draw-Line $graphics $accent 15 6 15 18 }
    }

    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $accentBrush.Dispose(); $main.Dispose(); $accent.Dispose(); $graphics.Dispose(); $bitmap.Dispose()
}

foreach ($root in $roots) {
    New-Item -ItemType Directory -Force -Path $root | Out-Null
    foreach ($name in $names) {
        Draw-Icon $name (Join-Path $root "$name.png") 1
        Draw-Icon $name (Join-Path $root "${name}@2x.png") 2
    }
}

Write-Host "Generated $($names.Count * 2) icon files in each resource bundle."
