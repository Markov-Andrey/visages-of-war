# Optional authoring tool: selects red accents from the current icons without redrawing them.
# Run manually; never runs during a build or icon import. -Overwrite replaces edited masks.
param([switch]$Overwrite)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;

public static class CommandAccentMask {
    public static void Save(string imagePath, string maskPath) {
        using (var image = new Bitmap(imagePath))
        using (var mask = new Bitmap(image.Width, image.Height, PixelFormat.Format32bppArgb)) {
            int width = image.Width, height = image.Height;
            var red = new bool[width * height];
            var protectedDetail = new bool[red.Length];
            for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                var color = image.GetPixel(x, y);
                int other = Math.Max(color.G, color.B), index = y * width + x;
                red[index] = color.A > 0 && color.R >= 20 && color.R - other >= 8 && color.R >= other * 1.5;
                // Protect cream/white elements, including shaded and antialiased edges.
                protectedDetail[index] = other >= 10 && color.R < other * 1.5;
            }
            for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                int index = y * width + x, alpha = 0;
                if (!protectedDetail[index]) {
                    // Full coverage extends 2 pixels into black gaps; the last pixel is feathered.
                    int distanceSquared = 10;
                    for (int dy = -3; dy <= 3; ++dy) for (int dx = -3; dx <= 3; ++dx) {
                        int sx = x + dx, sy = y + dy, distance = dx * dx + dy * dy;
                        if (distance >= distanceSquared || sx < 0 || sy < 0 || sx >= width || sy >= height) continue;
                        if (red[sy * width + sx]) distanceSquared = distance;
                    }
                    if (distanceSquared <= 4) alpha = 255;
                    else if (distanceSquared <= 9) alpha = (int)Math.Round(255 * (3.5 - Math.Sqrt(distanceSquared)) / 1.5);
                }
                mask.SetPixel(x, y, Color.FromArgb(alpha, 255, 255, 255));
            }
            mask.Save(maskPath, ImageFormat.Png);
        }
    }
}
'@
$iconRoot = Join-Path (Split-Path -Parent $PSScriptRoot) 'assets/ui/commands'
$names = @('move', 'stop', 'attributes', 'attack', 'patrol', 'hold', 'cancel', 'attack-ground', 'gather', 'build')
foreach ($name in $names) {
    $directory = Join-Path $iconRoot $name
    $image = Join-Path $directory 'icon.png'
    $mask = Join-Path $directory 'mask.png'
    if (-not (Test-Path -LiteralPath $image)) { throw "Missing command image: $image" }
    if ((Test-Path -LiteralPath $mask) -and -not $Overwrite) { throw "Mask exists: $mask. Use -Overwrite to replace edited masks." }
}
foreach ($name in $names) {
    $directory = Join-Path $iconRoot $name
    [CommandAccentMask]::Save((Join-Path $directory 'icon.png'), (Join-Path $directory 'mask.png'))
}
