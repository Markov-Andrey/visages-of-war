// Offline mask authoring; no game-specific asset coordinates belong here.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;

public static class TeamMaskAuthoring {
    static void Protect(bool[] selected, bool[] edge, int width, int height, string polygon) {
        var pairs = polygon.Split(' ');
        var points = new Point[pairs.Length];
        for (int i = 0; i < pairs.Length; ++i) {
            var xy = pairs[i].Split(','); points[i] = new Point(int.Parse(xy[0]), int.Parse(xy[1]));
        }
        using (var path = new GraphicsPath()) {
            path.AddPolygon(points);
            var bounds = Rectangle.Ceiling(path.GetBounds());
            for (int y = Math.Max(0,bounds.Top); y < Math.Min(height,bounds.Bottom); ++y)
                for (int x = Math.Max(0,bounds.Left); x < Math.Min(width,bounds.Right); ++x)
                    if (path.IsVisible(x,y)) { selected[y*width+x] = false; edge[y*width+x] = false; }
        }
    }
    static byte[] Read(Bitmap image) {
        var data = image.LockBits(new Rectangle(0, 0, image.Width, image.Height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        try {
            var pixels = new byte[image.Width * image.Height * 4];
            for (int y = 0; y < image.Height; ++y)
                Marshal.Copy(IntPtr.Add(data.Scan0, y * data.Stride), pixels, y * image.Width * 4, image.Width * 4);
            return pixels;
        } finally { image.UnlockBits(data); }
    }
    public static void Save(string source, string destination, int width, int height, double[] color, double[] edgeFilter, string[] protectedRegions, int[] backgroundRects) {
        if (color.Length != 5 || edgeFilter.Length != 6) throw new ArgumentException("Invalid color thresholds");
        if (backgroundRects.Length % 4 != 0) throw new ArgumentException("Background seeds require x,y,width,height");
        for (int i = 0; i < backgroundRects.Length; i += 4)
            if (backgroundRects[i] < 0 || backgroundRects[i+1] < 0 || backgroundRects[i+2] <= 0 || backgroundRects[i+3] <= 0 ||
                (long)backgroundRects[i]+backgroundRects[i+2] > width || (long)backgroundRects[i+1]+backgroundRects[i+3] > height)
                throw new ArgumentException("Background seed rectangle is outside the source canvas");
        using (var image = new Bitmap(source))
        using (var mask = new Bitmap(width, height, PixelFormat.Format32bppArgb)) {
            if (image.Width != width || image.Height != height) throw new InvalidOperationException("Artwork dimensions changed: " + source);
            var pixels = Read(image);
            var selected = new bool[width * height];
            var edge = new bool[selected.Length];
            for (int i = 0; i < selected.Length; ++i) {
                int b = pixels[i*4], g = pixels[i*4+1], r = pixels[i*4+2], a = pixels[i*4+3];
                if (a == 0) continue;
                int rg = r-g, other = Math.Max(g,b);
                // Narrow red hue protects skin, brown hair/boots and gold lanterns.
                selected[i] = r >= color[0] && rg >= color[1] && r >= other*color[2] && g-b <= rg*color[3]+color[4];
                edge[i] = r < edgeFilter[0] && other < edgeFilter[1] || rg > edgeFilter[2] && r >= other*edgeFilter[3] && g-b <= rg*edgeFilter[4]+edgeFilter[5];
            }
            if (backgroundRects.Length != 0) {
                // Remove selected components connected to explicitly authored background seeds.
                var pending = new Queue<int>();
                for (int i = 0; i < backgroundRects.Length; i += 4)
                    for (int y = backgroundRects[i+1]; y < backgroundRects[i+1]+backgroundRects[i+3]; ++y)
                        for (int x = backgroundRects[i]; x < backgroundRects[i]+backgroundRects[i+2]; ++x)
                            pending.Enqueue(y*width+x);
                while (pending.Count != 0) {
                    int p = pending.Dequeue();
                    if (!selected[p]) continue;
                    selected[p] = false;
                    edge[p] = false;
                    int x = p%width, y = p/width;
                    if (x > 0) pending.Enqueue(p-1);
                    if (x+1 < width) pending.Enqueue(p+1);
                    if (y > 0) pending.Enqueue(p-width);
                    if (y+1 < height) pending.Enqueue(p+width);
                }
            }
            foreach (var polygon in protectedRegions) Protect(selected,edge,width,height,polygon);
            var output = new byte[pixels.Length];
            for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                int i = y*width+x, alpha = selected[i] ? 255 : 0;
                if (alpha == 0 && edge[i]) {
                    int nearest = 6;
                    for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
                        int sx = x+dx, sy = y+dy, distance = dx*dx+dy*dy;
                        if (distance >= nearest || sx < 0 || sy < 0 || sx >= width || sy >= height) continue;
                        if (selected[sy*width+sx]) nearest = distance;
                    }
                    alpha = nearest <= 1 ? 255 : nearest <= 2 ? 192 : nearest <= 4 ? 96 : 0;
                }
                output[i*4] = output[i*4+1] = output[i*4+2] = 255;
                output[i*4+3] = (byte)alpha;
            }
            var data = mask.LockBits(new Rectangle(0,0,width,height), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
            try {
                for (int y = 0; y < height; ++y) Marshal.Copy(output,y*width*4,IntPtr.Add(data.Scan0,y*data.Stride),width*4);
            } finally { mask.UnlockBits(data); }
            mask.Save(destination, ImageFormat.Png);
        }
    }

    // Same Color blend and neutral curves as include/rts/TeamColor.hpp.
    // Previews are authoring artifacts; the game paints the original PNG at load time.
    public static void Preview(string source, string maskFile, string output, int team) {
        using (var image = new Bitmap(source)) using (var mask = new Bitmap(maskFile))
        using (var result = new Bitmap(image.Width,image.Height,PixelFormat.Format32bppArgb)) {
            if (image.Width != mask.Width || image.Height != mask.Height) throw new ArgumentException("Mask size mismatch");
            byte[] pixels = Read(image), coverage = Read(mask);
            double[] chroma = { (team&255)/255.0, ((team>>8)&255)/255.0, ((team>>16)&255)/255.0 };
            double teamLum = .11*chroma[0]+.59*chroma[1]+.30*chroma[2];
            for (int c = 0; c < 3; ++c) chroma[c] -= teamLum;
            double low = -Math.Min(chroma[0],Math.Min(chroma[1],chroma[2]));
            double high = Math.Max(chroma[0],Math.Max(chroma[1],chroma[2]));
            for (int i = 0; i < pixels.Length; i += 4) {
                if (pixels[i+3] == 0 || coverage[i+3] == 0) continue;
                double opacity = coverage[i+3]/255.0;
                double lum = (.11*pixels[i]+.59*pixels[i+1]+.30*pixels[i+2])/255.0;
                if (team == 0xffffff) lum *= 2-lum; else if (team == 0) lum *= lum;
                double scale = lum < low ? lum/low : lum > 1-high ? (1-lum)/high : 1;
                for (int c = 0; c < 3; ++c)
                    pixels[i+c] = (byte)Math.Round(Math.Max(0,Math.Min(255,pixels[i+c]*(1-opacity)+(lum+chroma[c]*scale)*255*opacity)),MidpointRounding.AwayFromZero);
            }
            var data = result.LockBits(new Rectangle(0,0,image.Width,image.Height),ImageLockMode.WriteOnly,PixelFormat.Format32bppArgb);
            try {
                for (int y = 0; y < image.Height; ++y) Marshal.Copy(pixels,y*image.Width*4,IntPtr.Add(data.Scan0,y*data.Stride),image.Width*4);
            } finally { result.UnlockBits(data); }
            result.Save(output,ImageFormat.Png);
        }
    }
}
