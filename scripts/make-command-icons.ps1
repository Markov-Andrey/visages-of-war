# Optional authoring tool for the two remaining procedural icons. Never runs during the build.
param(
    [ValidateSet('idle-worker', 'rally')]
    [string[]]$Names = @('idle-worker', 'rally')
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$iconDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) 'assets/ui/commands'
[void][IO.Directory]::CreateDirectory($iconDirectory)
function IconLine([float]$x1, [float]$y1, [float]$x2, [float]$y2) { $canvas.DrawLine($stroke, $x1, $y1, $x2, $y2) }
function IconPath([int[]]$coordinates) {
    $points = [Drawing.PointF[]]::new($coordinates.Length / 2)
    for ($i = 0; $i -lt $points.Length; ++$i) { $points[$i] = [Drawing.PointF]::new($coordinates[$i * 2], $coordinates[$i * 2 + 1]) }
    $canvas.DrawLines($stroke, $points)
}
foreach ($name in $Names) {
    $destination = Join-Path $iconDirectory $name
    [void][IO.Directory]::CreateDirectory($destination)
    $bitmap = [Drawing.Bitmap]::new(128, 128, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $canvas = [Drawing.Graphics]::FromImage($bitmap)
    $stroke = [Drawing.Pen]::new([Drawing.Color]::FromArgb(255, 230, 210, 161), 3)
    try {
        $canvas.Clear([Drawing.Color]::Transparent)
        $canvas.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $canvas.ScaleTransform(2, 2)
        $stroke.StartCap = $stroke.EndCap = [Drawing.Drawing2D.LineCap]::Round
        $stroke.LineJoin = [Drawing.Drawing2D.LineJoin]::Round
        switch ($name) {
            'idle-worker' { $canvas.DrawEllipse($stroke, 22, 10, 20, 23); IconLine 18 17 46 17; IconPath @(13,54,16,43,25,38,39,38,48,43,51,54,13,54) }
            'rally' { IconLine 19 9 19 55; IconPath @(19,12,49,12,42,23,49,34,19,34); IconLine 12 55 27 55 }
        }
        $bitmap.Save((Join-Path $destination 'icon.png'), [Drawing.Imaging.ImageFormat]::Png)
    } finally { $stroke.Dispose(); $canvas.Dispose(); $bitmap.Dispose() }
}
