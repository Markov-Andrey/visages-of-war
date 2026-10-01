# Optional authoring tool for the initial placeholders. Never runs during the build.
param(
    [ValidateSet('move', 'stop', 'attack', 'hold', 'patrol', 'gather', 'attack-ground', 'build', 'back', 'idle-worker', 'rally', 'cancel')]
    [string[]]$Names = @('move', 'stop', 'attack', 'hold', 'patrol', 'gather', 'attack-ground', 'build', 'back', 'idle-worker', 'rally', 'cancel')
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$iconDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) 'assets/ui/commands'
[void][IO.Directory]::CreateDirectory($iconDirectory)
function IconLine([float]$x1, [float]$y1, [float]$x2, [float]$y2) { $canvas.DrawLine($stroke, $x1, $y1, $x2, $y2) }
function IconPath([int[]]$coordinates, [bool]$filled = $false) {
    $points = [Drawing.PointF[]]::new($coordinates.Length / 2)
    for ($i = 0; $i -lt $points.Length; ++$i) { $points[$i] = [Drawing.PointF]::new($coordinates[$i * 2], $coordinates[$i * 2 + 1]) }
    if ($filled) { $canvas.FillPolygon($ink, $points) } else { $canvas.DrawLines($stroke, $points) }
}
foreach ($name in $Names) {
    $bitmap = [Drawing.Bitmap]::new(128, 128, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $canvas = [Drawing.Graphics]::FromImage($bitmap)
    $stroke = [Drawing.Pen]::new([Drawing.Color]::FromArgb(255, 230, 210, 161), 3)
    $ink = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(255, 230, 210, 161))
    try {
        $canvas.Clear([Drawing.Color]::Transparent)
        $canvas.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $canvas.ScaleTransform(2, 2)
        $stroke.StartCap = $stroke.EndCap = [Drawing.Drawing2D.LineCap]::Round
        $stroke.LineJoin = [Drawing.Drawing2D.LineJoin]::Round
        switch ($name) {
            'move' { IconLine 14 49 49 14; IconPath @(29,14,49,14,49,34) }
            'stop' { $canvas.FillRectangle($ink, 17, 17, 30, 30) }
            'attack' {
                IconPath @(12,51,43,12,52,9,51,20,20,51) $true
                IconLine 12 39 26 52
                IconPath @(12,12,18,14,49,47,46,51,14,20,12,12)
                IconLine 39 50 52 37
            }
            'hold' { IconPath @(14,12,50,12,48,37,32,53,16,37,14,12); IconLine 32 20 32 42; IconLine 23 28 41 28 }
            'patrol' { IconLine 13 22 51 22; IconPath @(41,12,51,22,41,32); IconLine 51 43 13 43; IconPath @(23,33,13,43,23,53) }
            'gather' { IconPath @(32,9,49,26,42,52,22,52,15,26,32,9); IconPath @(32,9,27,29,32,52,38,29,32,9); IconLine 15 26 49 26 }
            'attack-ground' { $canvas.DrawEllipse($stroke, 14, 14, 36, 36); $canvas.DrawEllipse($stroke, 25, 25, 14, 14); IconLine 32 6 32 19; IconLine 32 45 32 58; IconLine 6 32 19 32; IconLine 45 32 58 32 }
            'build' { IconLine 16 53 39 24; IconPath @(28,14,37,6,56,25,47,34,28,14) $true; IconLine 11 50 21 57 }
            'back' { IconLine 50 32 14 32; IconPath @(27,18,13,32,27,46) }
            'idle-worker' { $canvas.DrawEllipse($stroke, 22, 10, 20, 23); IconLine 18 17 46 17; IconPath @(13,54,16,43,25,38,39,38,48,43,51,54,13,54) }
            'rally' { IconLine 19 9 19 55; IconPath @(19,12,49,12,42,23,49,34,19,34); IconLine 12 55 27 55 }
            'cancel' { IconLine 17 17 47 47; IconLine 17 47 47 17 }
        }
        $bitmap.Save((Join-Path $iconDirectory ($name + '.png')), [Drawing.Imaging.ImageFormat]::Png)
    } finally { $ink.Dispose(); $stroke.Dispose(); $canvas.Dispose(); $bitmap.Dispose() }
}
