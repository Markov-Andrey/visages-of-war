# Deterministic crop of the owner's command sheet; never runs during a build.
param([string]$SourceImage = (Join-Path (Split-Path -Parent $PSScriptRoot) 'assets/ui/commands/source/ui-buttons.png'))
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$destination = Join-Path (Split-Path -Parent $PSScriptRoot) 'assets/ui/commands'
$source = [Drawing.Bitmap]::new((Resolve-Path -LiteralPath $SourceImage).Path)
try {
    if ($source.Width -ne 1536 -or $source.Height -ne 1024) { throw 'Expected the 1536x1024 command sheet.' }
    $crops = @(
        @{ Name = 'move'; X = 18; Y = 170 },
        @{ Name = 'stop'; X = 318; Y = 170 },
        @{ Name = 'attributes'; X = 618; Y = 170 },
        @{ Name = 'attack'; X = 918; Y = 170 },
        @{ Name = 'patrol'; X = 1218; Y = 170 },
        @{ Name = 'hold'; X = 18; Y = 494 },
        @{ Name = 'cancel'; X = 318; Y = 494 },
        @{ Name = 'attack-ground'; X = 618; Y = 494 },
        @{ Name = 'gather'; X = 918; Y = 494 },
        @{ Name = 'build'; X = 1218; Y = 494 }
    )
    # Trim 5 pixels from each side of every original 300x300 cell.
    $trim = 5
    $size = 300 - 2 * $trim
    foreach ($crop in $crops) {
        $iconDirectory = Join-Path $destination $crop.Name
        [void][IO.Directory]::CreateDirectory($iconDirectory)
        $rectangle = [Drawing.Rectangle]::new($crop.X + $trim, $crop.Y + $trim, $size, $size)
        $image = $source.Clone($rectangle, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        try { $image.Save((Join-Path $iconDirectory 'icon.png'), [Drawing.Imaging.ImageFormat]::Png) }
        finally { $image.Dispose() }
    }
} finally { $source.Dispose() }
