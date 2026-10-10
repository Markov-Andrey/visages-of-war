# Optional authoring tool. Never runs during a build; source PNGs are never modified.
# Regions belong to the current Peacemaker Corps artwork. Recheck them after replacing it.
param([switch]$Overwrite)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;

public static class CorpsAccentMask {
    static byte[] Read(Bitmap bitmap) {
        var data = bitmap.LockBits(new Rectangle(0, 0, bitmap.Width, bitmap.Height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        try {
            var pixels = new byte[bitmap.Width * bitmap.Height * 4];
            for (int y = 0; y < bitmap.Height; ++y)
                Marshal.Copy(IntPtr.Add(data.Scan0, y * data.Stride), pixels, y * bitmap.Width * 4, bitmap.Width * 4);
            return pixels;
        } finally { bitmap.UnlockBits(data); }
    }
    public static void Save(string source, string destination, int expectedWidth, int expectedHeight, string[] polygons) {
        using (var image = new Bitmap(source))
        using (var mask = new Bitmap(image.Width, image.Height, PixelFormat.Format32bppArgb))
        using (var region = new GraphicsPath(FillMode.Winding)) {
            int w = image.Width, h = image.Height;
            if (w != expectedWidth || h != expectedHeight) throw new InvalidOperationException("Artwork dimensions changed: " + source);
            foreach (var polygon in polygons) {
                string[] pairs = polygon.Split(' ');
                var points = new Point[pairs.Length];
                for (int i = 0; i < points.Length; ++i) {
                    string[] xy = pairs[i].Split(',');
                    points[i] = new Point(int.Parse(xy[0]), int.Parse(xy[1]));
                }
                region.AddPolygon(points);
            }
            byte[] areas;
            using (var allowed = new Bitmap(w, h, PixelFormat.Format32bppArgb)) {
                using (var g = Graphics.FromImage(allowed)) g.FillPath(Brushes.White, region);
                areas = Read(allowed);
            }
            var sourcePixels = Read(image);
            var selected = new bool[w * h];
            var edge = new bool[w * h];
            for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
                int pixel = (y * w + x) * 4;
                if (sourcePixels[pixel + 3] == 0 || areas[pixel + 3] == 0) continue;
                var c = Color.FromArgb(sourcePixels[pixel + 3], sourcePixels[pixel + 2], sourcePixels[pixel + 1], sourcePixels[pixel]);
                int rg = c.R - c.G, gb = c.G - c.B, other = Math.Max(c.G, c.B);
                // Small portrait accents sit beside skin, leather and wooden weapons.
                double hueLimit = h == 1512 && y > 650 ? .25 : .45;
                // Red hue, including warm roof highlights; reject ochre wood/stone.
                selected[y * w + x] = c.R >= 20 && rg >= 7 && c.R >= other * 1.6 && gb <= rg * hueLimit + 2;
                // Permit a small overlap into dark outlines, but protect pale masonry.
                edge[y * w + x] = c.R < 45 && other < 30 ||
                    rg > 3 && c.R >= other * 1.3 && gb <= rg * hueLimit + 3;
            }
            var output = new byte[w * h * 4];
            for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
                int index = y * w + x, alpha = selected[index] ? 255 : 0;
                if (alpha == 0 && edge[index]) {
                    int nearest = 6;
                    for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
                        int sx = x + dx, sy = y + dy, distance = dx * dx + dy * dy;
                        if (distance >= nearest || sx < 0 || sy < 0 || sx >= w || sy >= h) continue;
                        if (selected[sy * w + sx]) nearest = distance;
                    }
                    alpha = nearest <= 1 ? 255 : nearest <= 2 ? 192 : nearest <= 4 ? 96 : 0;
                }
                // Coverage is independent of source alpha: the renderer preserves it.
                output[index * 4] = output[index * 4 + 1] = output[index * 4 + 2] = 255;
                output[index * 4 + 3] = (byte)alpha;
            }
            var data = mask.LockBits(new Rectangle(0, 0, w, h), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            try {
                for (int y = 0; y < h; ++y) Marshal.Copy(output, y * w * 4, IntPtr.Add(data.Scan0, y * data.Stride), w * 4);
            } finally { mask.UnlockBits(data); }
            mask.Save(destination, ImageFormat.Png);
        }
    }
}
'@
$artRoot = Join-Path (Split-Path -Parent $PSScriptRoot) 'assets/sprites/buildings/valeri/peacemaker-corps'
$jobs = @(
    @{ Name = 'peacemaker-corps'; Width = 1536; Height = 1024; Regions = @(
        '531,146 634,32 729,40 1355,273 1358,305 1264,290 1169,399',
        '173,397 393,290 451,331 268,441 176,413',
        '764,353 795,337 830,340 897,380 877,401 838,373 770,366',
        '1230,470 1276,443 1307,476 1260,505',
        '1329,380 1369,398 1330,421',
        '611,416 637,431 668,443 668,512 637,501 610,485',
        '397,633 446,645 441,793 397,775',
        '571,708 618,726 618,878 571,856'
    ) },
    @{ Name = 'peacemaker-corps-icon'; Width = 1254; Height = 1254; Regions = @(
        '40,109 538,200 559,250 612,299 751,333 754,252 1155,340 1229,444 1229,547 1194,542 37,352',
        '360,543 773,403 1035,628 1021,659 764,447 373,580'
    ) },
    @{ Name = 'peacemaker-corps-portrait'; Width = 930; Height = 1512; Regions = @(
        '436,263 575,96 929,160 929,440',
        '0,548 281,460 359,520 352,542 0,650',
        '629,481 795,410 900,506 899,536 838,554 729,451 633,495',
        '202,117 291,193 356,290 397,348 491,405 524,427 531,456 539,516 498,478 477,440 460,479 456,515 483,578 445,534 386,501 345,458 321,410 266,386 203,347',
        '383,315 421,329 423,374 387,359',
        '905,488 929,504 929,759 902,771',
        '247,1150 344,1176 344,1460 247,1410',
        '0,875 93,875 105,1115 0,1122',
        '194,790 246,790 246,904 194,904',
        '327,741 396,741 396,850 327,850',
        '750,959 831,959 846,1084 745,1084',
        '799,1135 920,1135 928,1302 816,1302',
        '550,1015 610,1015 610,1154 550,1154',
        '515,684 590,684 590,793 515,793',
        '395,1280 468,1280 468,1440 395,1440',
        '472,1378 546,1378 546,1502 472,1502',
        '895,824 929,824 929,929 895,929'
    ) }
)
foreach ($job in $jobs) {
    $source = Join-Path $artRoot ($job.Name + '.png')
    $mask = Join-Path $artRoot ($job.Name + '-mask.png')
    if (-not (Test-Path -LiteralPath $source)) { throw "Missing source: $source" }
    if ((Test-Path -LiteralPath $mask) -and -not $Overwrite) { throw "Mask exists: $mask. Use -Overwrite to replace edited masks." }
}
foreach ($job in $jobs) {
    [CorpsAccentMask]::Save((Join-Path $artRoot ($job.Name + '.png')), (Join-Path $artRoot ($job.Name + '-mask.png')),
        $job.Width, $job.Height, [string[]]$job.Regions)
    Write-Output ($job.Name + '-mask.png')
}
