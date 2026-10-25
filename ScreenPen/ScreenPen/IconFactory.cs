using System.Drawing.Drawing2D;

namespace ScreenPen;

/// <summary>별도 리소스 파일 없이 코드로 아이콘/펜 그림을 그립니다.</summary>
internal static class IconFactory
{
    public static readonly Color AccentColor = Color.FromArgb(0, 120, 215);

    /// <summary>r 영역 안에 왼쪽 아래를 향한 펜 모양을 그립니다.</summary>
    public static void DrawPenGlyph(Graphics g, RectangleF r, Color color)
    {
        float size = Math.Min(r.Width, r.Height);
        float length = size * 1.1f;
        float w = size * 0.22f;
        float tip = length * 0.28f;
        float top = -length / 2, bottom = length / 2;

        var state = g.Save();
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.TranslateTransform(r.X + r.Width / 2, r.Y + r.Height / 2);
        g.RotateTransform(45);   // 세로 펜을 시계 방향 45° 회전 → 펜촉이 왼쪽 아래

        using var brush = new SolidBrush(color);
        // 펜 몸통 (윗부분에 작은 틈을 두어 뚜껑처럼 보이게)
        float capH = length * 0.14f;
        g.FillRectangle(brush, -w / 2, top, w, capH);
        g.FillRectangle(brush, -w / 2, top + capH + size * 0.05f, w, bottom - tip - (top + capH + size * 0.05f));
        // 펜촉
        g.FillPolygon(brush, new[]
        {
            new PointF(-w / 2, bottom - tip + size * 0.04f),
            new PointF(w / 2, bottom - tip + size * 0.04f),
            new PointF(0, bottom)
        });

        g.Restore(state);
    }

    public static Icon CreateAppIcon()
    {
        using var bmp = new Bitmap(32, 32);
        using (var g = Graphics.FromImage(bmp))
        {
            g.SmoothingMode = SmoothingMode.AntiAlias;
            g.Clear(Color.Transparent);
            using var brush = new SolidBrush(AccentColor);
            g.FillEllipse(brush, 0, 0, 31, 31);
            DrawPenGlyph(g, new RectangleF(7, 7, 18, 18), Color.White);
        }

        IntPtr hIcon = bmp.GetHicon();
        try
        {
            using var temp = Icon.FromHandle(hIcon);
            return (Icon)temp.Clone();
        }
        finally
        {
            NativeMethods.DestroyIcon(hIcon);
        }
    }
}
