using System.Drawing.Drawing2D;

namespace ScreenPen;

/// <summary>
/// 펜으로 그린 한 획(마우스를 누르고 뗄 때까지)의 데이터.
/// 완성된 획은 바뀌지 않으며, 지우개로 일부를 지우면 남은 부분이 새 획들로 나뉩니다.
/// </summary>
public sealed class InkStroke
{
    /// <summary>획 완성 시 점 사이 최대 간격(px). 지우개가 빈틈 없이 동작하도록 점을 촘촘하게 채웁니다.</summary>
    private const float PointSpacing = 2f;

    private List<PointF> points;
    private PointF[]? pointArray;

    public Color Color { get; }
    public float Width { get; }
    public bool IsHighlighter { get; }
    public RectangleF Bounds { get; private set; }
    public IReadOnlyList<PointF> Points => points;
    public PointF LastPoint => points[^1];

    public InkStroke(Color color, float width, bool isHighlighter, IEnumerable<PointF>? initialPoints = null)
    {
        Color = color;
        Width = width;
        IsHighlighter = isHighlighter;
        points = initialPoints != null ? new List<PointF>(initialPoints) : new List<PointF>();
        UpdateBounds();
    }

    public void AddPoint(PointF p)
    {
        points.Add(p);
        pointArray = null;
        Bounds = points.Count == 1
            ? new RectangleF(p, SizeF.Empty)
            : RectangleF.FromLTRB(Math.Min(Bounds.Left, p.X), Math.Min(Bounds.Top, p.Y),
                                  Math.Max(Bounds.Right, p.X), Math.Max(Bounds.Bottom, p.Y));
    }

    /// <summary>그리기가 끝났을 때 호출. 점 사이를 보간하여 촘촘하게 만듭니다.</summary>
    public void Finish()
    {
        if (points.Count < 2) return;

        var dense = new List<PointF>(points.Count * 2) { points[0] };
        for (int i = 1; i < points.Count; i++)
        {
            PointF a = points[i - 1], b = points[i];
            float dx = b.X - a.X, dy = b.Y - a.Y;
            int steps = (int)(MathF.Sqrt(dx * dx + dy * dy) / PointSpacing);
            for (int k = 1; k < steps; k++)
            {
                float t = k / (float)steps;
                dense.Add(new PointF(a.X + dx * t, a.Y + dy * t));
            }
            dense.Add(b);
        }
        points = dense;
        pointArray = null;
        UpdateBounds();
    }

    /// <summary>
    /// hit 조건에 맞는 점들을 제거하고 남은 조각들을 반환합니다.
    /// 아무 점도 해당하지 않으면 null (변경 없음)을 반환합니다.
    /// </summary>
    public List<InkStroke>? SplitWhere(Func<PointF, bool> hit)
    {
        bool anyHit = false;
        var pieces = new List<InkStroke>();
        var run = new List<PointF>();

        void Flush()
        {
            if (run.Count >= 2) pieces.Add(new InkStroke(Color, Width, IsHighlighter, run));
            run = new List<PointF>();
        }

        foreach (var p in points)
        {
            if (hit(p))
            {
                anyHit = true;
                Flush();
            }
            else
            {
                run.Add(p);
            }
        }

        if (!anyHit) return null;
        Flush();
        return pieces;
    }

    public void Draw(Graphics g)
    {
        if (points.Count == 0) return;

        Color c = IsHighlighter ? Color.FromArgb(110, Color) : Color;

        if (points.Count == 1)
        {
            using var brush = new SolidBrush(c);
            var p = points[0];
            g.FillEllipse(brush, p.X - Width / 2, p.Y - Width / 2, Width, Width);
            return;
        }

        using var pen = new Pen(c, Width)
        {
            StartCap = LineCap.Round,
            EndCap = LineCap.Round,
            LineJoin = LineJoin.Round
        };
        pointArray ??= points.ToArray();
        g.DrawLines(pen, pointArray);
    }

    private void UpdateBounds()
    {
        if (points.Count == 0) { Bounds = RectangleF.Empty; return; }
        float l = float.MaxValue, t = float.MaxValue, r = float.MinValue, b = float.MinValue;
        foreach (var p in points)
        {
            if (p.X < l) l = p.X;
            if (p.Y < t) t = p.Y;
            if (p.X > r) r = p.X;
            if (p.Y > b) b = p.Y;
        }
        Bounds = RectangleF.FromLTRB(l, t, r, b);
    }
}
