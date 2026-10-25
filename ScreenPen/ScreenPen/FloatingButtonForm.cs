using System.Drawing.Drawing2D;

namespace ScreenPen;

/// <summary>
/// 대기 모드에서 화면 위에 항상 떠 있는 동그란 "판서" 버튼.
///  - 클릭: 판서 시작
///  - 드래그: 위치 이동
///  - 우클릭: 메뉴 (판서 시작 / 종료)
/// </summary>
public sealed class FloatingButtonForm : Form
{
    private readonly AppController app;
    private Point dragStartCursor;
    private Point dragStartLocation;
    private bool mouseIsDown;
    private bool moved;
    private bool hover;

    public FloatingButtonForm(AppController app, Icon icon)
    {
        this.app = app;

        Text = "화면 판서";
        Icon = icon;
        FormBorderStyle = FormBorderStyle.None;
        StartPosition = FormStartPosition.Manual;
        ShowInTaskbar = false;
        TopMost = true;
        DoubleBuffered = true;
        BackColor = IconFactory.AccentColor;
        Opacity = 0.92;
        Cursor = Cursors.Hand;

        int d = Dpi(64);
        Size = new Size(d, d);
        Rectangle wa = Screen.PrimaryScreen?.WorkingArea ?? new Rectangle(0, 0, 1280, 720);
        Location = new Point(wa.Right - d - Dpi(24), wa.Bottom - d - Dpi(24));

        var menu = new ContextMenuStrip();
        menu.Items.Add($"판서 시작 ({AppController.HotKeyText})", null, (_, _) => app.StartAnnotation());
        menu.Items.Add(new ToolStripSeparator());
        menu.Items.Add("프로그램 종료", null, (_, _) => app.RequestExit());
        ContextMenuStrip = menu;

        var tip = new ToolTip();
        tip.SetToolTip(this, "클릭: 판서 시작\n드래그: 버튼 이동\n우클릭: 메뉴");
    }

    private int Dpi(int value) => (int)Math.Round(value * DeviceDpi / 96.0);

    protected override bool ShowWithoutActivation => true;

    protected override CreateParams CreateParams
    {
        get
        {
            var cp = base.CreateParams;
            cp.ExStyle |= NativeMethods.WS_EX_TOOLWINDOW | NativeMethods.WS_EX_NOACTIVATE;
            return cp;
        }
    }

    protected override void OnSizeChanged(EventArgs e)
    {
        base.OnSizeChanged(e);
        using var path = new GraphicsPath();
        path.AddEllipse(0, 0, Width, Height);
        Region = new Region(path);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        g.TextRenderingHint = System.Drawing.Text.TextRenderingHint.AntiAliasGridFit;

        Color fill = hover ? Color.FromArgb(30, 145, 235) : IconFactory.AccentColor;
        using (var brush = new SolidBrush(fill))
            g.FillEllipse(brush, 0, 0, Width - 1, Height - 1);
        using (var border = new Pen(Color.FromArgb(200, Color.White), Math.Max(1.5f, Width / 32f)))
            g.DrawEllipse(border, 2, 2, Width - 5, Height - 5);

        float s = Width;
        IconFactory.DrawPenGlyph(g, new RectangleF(s * 0.30f, s * 0.14f, s * 0.40f, s * 0.40f), Color.White);

        using var font = new Font("Malgun Gothic", 8.5f, FontStyle.Bold);
        var textRect = new RectangleF(0, s * 0.56f, s, s * 0.30f);
        using var fmt = new StringFormat { Alignment = StringAlignment.Center, LineAlignment = StringAlignment.Center };
        g.DrawString("판서", font, Brushes.White, textRect, fmt);
    }

    protected override void OnMouseEnter(EventArgs e)
    {
        base.OnMouseEnter(e);
        hover = true;
        Invalidate();
    }

    protected override void OnMouseLeave(EventArgs e)
    {
        base.OnMouseLeave(e);
        hover = false;
        Invalidate();
    }

    protected override void OnMouseDown(MouseEventArgs e)
    {
        base.OnMouseDown(e);
        if (e.Button != MouseButtons.Left) return;
        mouseIsDown = true;
        moved = false;
        dragStartCursor = Cursor.Position;
        dragStartLocation = Location;
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        base.OnMouseMove(e);
        if (!mouseIsDown) return;
        Point c = Cursor.Position;
        int dx = c.X - dragStartCursor.X, dy = c.Y - dragStartCursor.Y;
        if (!moved && Math.Abs(dx) + Math.Abs(dy) > Dpi(4)) moved = true;
        if (moved) Location = new Point(dragStartLocation.X + dx, dragStartLocation.Y + dy);
    }

    protected override void OnMouseUp(MouseEventArgs e)
    {
        base.OnMouseUp(e);
        if (e.Button != MouseButtons.Left || !mouseIsDown) return;
        mouseIsDown = false;
        if (!moved) app.StartAnnotation();
    }
}
