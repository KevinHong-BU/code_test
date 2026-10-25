using System.Drawing.Drawing2D;
using System.Drawing.Imaging;

namespace ScreenPen;

public enum DrawTool
{
    Pen,
    Highlighter,
    Eraser,      // 부분 지우개 (지나간 곳만 지움)
    AreaEraser   // 사각형 영역 지우기
}

/// <summary>
/// 정지된(캡처된) 화면을 전체 화면으로 보여주고 그 위에 판서하는 창.
/// </summary>
public sealed class OverlayForm : Form
{
    /// <summary>기본 펜 색상 (단축키 1~6)</summary>
    public static readonly Color[] Palette =
    {
        Color.FromArgb(230, 30, 30),   // 빨강
        Color.FromArgb(0, 90, 230),    // 파랑
        Color.Black,                   // 검정
        Color.FromArgb(0, 160, 60),    // 초록
        Color.FromArgb(255, 200, 0),   // 노랑
        Color.White                    // 흰색
    };

    public static readonly string[] SizeNames = { "얇게", "보통", "굵게" };
    private static readonly float[] PenWidths = { 2f, 4f, 8f };
    private static readonly float[] HighlighterWidths = { 12f, 20f, 32f };
    private static readonly float[] EraserRadii = { 8f, 20f, 45f };

    private readonly AppController app;
    private readonly Rectangle screenBounds;
    private readonly Bitmap background;   // 정지된 화면
    private readonly Bitmap inkLayer;     // 완성된 획들을 미리 그려두는 투명 레이어
    private readonly ToolbarForm toolbar;

    // 획 목록은 "바꾸지 않는 리스트"로 다룹니다. 변경 시 새 리스트를 만들고
    // 이전 리스트를 실행 취소 스택에 넣습니다.
    private List<InkStroke> strokes = new();
    private readonly Stack<List<InkStroke>> undoStack = new();
    private readonly Stack<List<InkStroke>> redoStack = new();

    private readonly int[] sizeIndex = { 1, 1, 1, 1 };   // 도구별 굵기 선택
    private float dpiScale = 1f;

    // 드래그 상태
    private DrawTool? dragTool;
    private MouseButtons dragButton;
    private InkStroke? currentStroke;
    private List<InkStroke>? strokesBeforeErase;
    private bool erasedDuringDrag;
    private PointF lastErasePoint;
    private Point areaStart;
    private Rectangle areaRect;

    private Point mousePos;
    private bool mouseInside;
    private bool hasUnsavedChanges;
    private bool allowClose;

    public event EventHandler? StateChanged;

    public DrawTool Tool { get; private set; } = DrawTool.Pen;
    public Color PenColor { get; private set; } = Palette[0];
    public int SizeIndex => sizeIndex[(int)Tool];
    public bool CanUndo => undoStack.Count > 0;
    public bool CanRedo => redoStack.Count > 0;
    public bool HasInk => strokes.Count > 0;

    public OverlayForm(AppController app, Rectangle bounds, Bitmap screenshot)
    {
        this.app = app;
        screenBounds = bounds;
        background = screenshot;
        inkLayer = new Bitmap(bounds.Width, bounds.Height, PixelFormat.Format32bppPArgb);

        Text = "화면 판서";
        FormBorderStyle = FormBorderStyle.None;
        StartPosition = FormStartPosition.Manual;
        AutoScaleMode = AutoScaleMode.None;
        Bounds = bounds;
        TopMost = true;
        ShowInTaskbar = false;
        KeyPreview = true;
        Cursor = Cursors.Cross;

        SetStyle(ControlStyles.AllPaintingInWmPaint | ControlStyles.OptimizedDoubleBuffer |
                 ControlStyles.UserPaint | ControlStyles.Opaque, true);

        toolbar = new ToolbarForm(this, app.HotKeyAvailable);
    }

    #region 창 수명 관리

    protected override void OnHandleCreated(EventArgs e)
    {
        base.OnHandleCreated(e);
        dpiScale = DeviceDpi / 96f;
    }

    protected override void OnShown(EventArgs e)
    {
        base.OnShown(e);
        Bounds = screenBounds;   // DPI 처리 후에도 화면을 정확히 덮도록 다시 지정
        toolbar.PlaceAtTop(screenBounds);
        toolbar.Show(this);      // 소유자를 지정하면 도구 모음이 항상 판서 화면 위에 표시됨
        toolbar.PlaceAtTop(screenBounds);   // 창이 만들어진 뒤 실제 크기로 한 번 더 맞춤
        Activate();
    }

    protected override void OnFormClosing(FormClosingEventArgs e)
    {
        // Alt+F4 등으로 닫으려 하면 "종료" 버튼과 같은 절차를 거침
        if (!allowClose && e.CloseReason == CloseReason.UserClosing)
        {
            e.Cancel = true;
            app.RequestExit();
            return;
        }
        base.OnFormClosing(e);
    }

    /// <summary>AppController 에서 판서 화면을 실제로 닫을 때 사용합니다.</summary>
    public void CloseOverlay()
    {
        allowClose = true;
        toolbar.Close();
        Close();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            toolbar.Dispose();
            background.Dispose();
            inkLayer.Dispose();
        }
        base.Dispose(disposing);
    }

    public void RequestStandby() => app.RequestStandby();
    public void RequestExit() => app.RequestExit();

    /// <summary>
    /// 저장하지 않은 판서가 있으면 저장 여부를 묻습니다.
    /// 계속 진행해도 되면 true, 취소하면 false.
    /// </summary>
    public bool ConfirmDiscard()
    {
        if (!hasUnsavedChanges || !HasInk) return true;

        var result = MessageBox.Show(this,
            "저장하지 않은 판서 내용이 있습니다.\n화면을 저장하시겠습니까?",
            "화면 판서", MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question);

        return result switch
        {
            DialogResult.Yes => SaveImage(),
            DialogResult.No => true,
            _ => false
        };
    }

    #endregion

    #region 도구 / 색 / 굵기

    public void SetTool(DrawTool tool)
    {
        Tool = tool;
        Invalidate();
        OnStateChanged();
    }

    public void SetColor(Color color)
    {
        PenColor = color;
        if (Tool is DrawTool.Eraser or DrawTool.AreaEraser) Tool = DrawTool.Pen;
        Invalidate();
        OnStateChanged();
    }

    public void SetSizeIndex(int index)
    {
        sizeIndex[(int)Tool] = Math.Clamp(index, 0, SizeNames.Length - 1);
        Invalidate();
        OnStateChanged();
    }

    private float WidthFor(DrawTool tool) => tool switch
    {
        DrawTool.Highlighter => HighlighterWidths[sizeIndex[(int)DrawTool.Highlighter]] * dpiScale,
        _ => PenWidths[sizeIndex[(int)DrawTool.Pen]] * dpiScale
    };

    private float EraserRadius => EraserRadii[sizeIndex[(int)DrawTool.Eraser]] * dpiScale;

    private bool ShowEraserCursor => mouseInside && (Tool == DrawTool.Eraser || dragTool == DrawTool.Eraser);

    private void OnStateChanged() => StateChanged?.Invoke(this, EventArgs.Empty);

    #endregion

    #region 실행 취소 / 지우기

    private void Commit(List<InkStroke> newStrokes, bool rebuild = true)
    {
        undoStack.Push(strokes);
        redoStack.Clear();
        strokes = newStrokes;
        hasUnsavedChanges = true;
        if (rebuild)
        {
            RebuildInkLayer();
            Invalidate();
        }
        OnStateChanged();
    }

    public void Undo()
    {
        if (undoStack.Count == 0 || dragTool != null) return;
        redoStack.Push(strokes);
        strokes = undoStack.Pop();
        hasUnsavedChanges = true;
        RebuildInkLayer();
        Invalidate();
        OnStateChanged();
    }

    public void Redo()
    {
        if (redoStack.Count == 0 || dragTool != null) return;
        undoStack.Push(strokes);
        strokes = redoStack.Pop();
        hasUnsavedChanges = true;
        RebuildInkLayer();
        Invalidate();
        OnStateChanged();
    }

    /// <summary>전체 지우기 (실행 취소 가능)</summary>
    public void ClearAll()
    {
        if (strokes.Count == 0 || dragTool != null) return;
        Commit(new List<InkStroke>());
    }

    /// <summary>
    /// hit 조건에 맞는 점들을 지운 새 획 목록을 만듭니다. 변경이 없으면 null.
    /// </summary>
    private List<InkStroke>? EraseWhere(RectangleF hitBounds, Func<InkStroke, PointF, bool> hit)
    {
        List<InkStroke>? result = null;
        for (int i = 0; i < strokes.Count; i++)
        {
            var s = strokes[i];
            var bb = s.Bounds;
            bb.Inflate(s.Width / 2 + 1, s.Width / 2 + 1);

            List<InkStroke>? pieces = bb.IntersectsWith(hitBounds) ? s.SplitWhere(p => hit(s, p)) : null;
            if (pieces == null)
            {
                result?.Add(s);
                continue;
            }

            // 처음으로 바뀐 획을 만나면 앞부분을 복사해 둠
            result ??= new List<InkStroke>(strokes.GetRange(0, i));
            result.AddRange(pieces);
        }
        return result;
    }

    /// <summary>부분 지우개: 지우개가 a → b 로 지나간 자리의 판서를 지웁니다.</summary>
    private void EraseAlong(PointF a, PointF b)
    {
        float r = EraserRadius;
        var hitBounds = RectangleF.FromLTRB(Math.Min(a.X, b.X) - r, Math.Min(a.Y, b.Y) - r,
                                            Math.Max(a.X, b.X) + r, Math.Max(a.Y, b.Y) + r);

        var result = EraseWhere(hitBounds, (s, p) =>
        {
            float limit = r + s.Width / 2;
            return DistanceToSegmentSquared(p, a, b) <= limit * limit;
        });

        if (result != null)
        {
            strokes = result;
            erasedDuringDrag = true;
            RebuildInkLayer();
            Invalidate();
        }
    }

    /// <summary>영역 지우기: 사각형 안의 판서를 지웁니다.</summary>
    private void EraseRect(Rectangle rect)
    {
        var result = EraseWhere(rect, (_, p) => rect.Contains((int)p.X, (int)p.Y));
        if (result != null) Commit(result);
    }

    private static float DistanceToSegmentSquared(PointF p, PointF a, PointF b)
    {
        float dx = b.X - a.X, dy = b.Y - a.Y;
        float lenSq = dx * dx + dy * dy;
        float t = lenSq < 0.0001f ? 0 : Math.Clamp(((p.X - a.X) * dx + (p.Y - a.Y) * dy) / lenSq, 0f, 1f);
        float cx = a.X + t * dx - p.X, cy = a.Y + t * dy - p.Y;
        return cx * cx + cy * cy;
    }

    #endregion

    #region 저장 / 복사

    private Bitmap ComposeImage()
    {
        var img = new Bitmap(background.Width, background.Height, PixelFormat.Format24bppRgb);
        using var g = Graphics.FromImage(img);
        var rect = new Rectangle(0, 0, img.Width, img.Height);
        g.DrawImage(background, rect, rect, GraphicsUnit.Pixel);
        g.DrawImage(inkLayer, rect, rect, GraphicsUnit.Pixel);
        return img;
    }

    /// <summary>판서한 화면을 이미지 파일로 저장합니다. 저장했으면 true.</summary>
    public bool SaveImage()
    {
        if (dragTool != null) return false;

        string folder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyPictures), "화면판서");
        try { Directory.CreateDirectory(folder); } catch { /* 폴더를 못 만들면 기본 위치 사용 */ }

        using var dlg = new SaveFileDialog
        {
            Title = "판서 화면 저장",
            Filter = "PNG 이미지 (*.png)|*.png|JPEG 이미지 (*.jpg)|*.jpg|BMP 이미지 (*.bmp)|*.bmp",
            FileName = $"판서_{DateTime.Now:yyyyMMdd_HHmmss}.png",
            InitialDirectory = folder,
            AddExtension = true,
            OverwritePrompt = true
        };

        toolbar.Enabled = false;
        try
        {
            if (dlg.ShowDialog(this) != DialogResult.OK) return false;

            using var img = ComposeImage();
            ImageFormat format = Path.GetExtension(dlg.FileName).ToLowerInvariant() switch
            {
                ".jpg" or ".jpeg" => ImageFormat.Jpeg,
                ".bmp" => ImageFormat.Bmp,
                _ => ImageFormat.Png
            };
            img.Save(dlg.FileName, format);

            hasUnsavedChanges = false;
            app.ShowNotification("저장 완료", dlg.FileName);
            return true;
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, "저장하지 못했습니다.\n\n" + ex.Message, "화면 판서",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
            return false;
        }
        finally
        {
            toolbar.Enabled = true;
            Activate();
        }
    }

    public void CopyToClipboard()
    {
        if (dragTool != null) return;
        try
        {
            using var img = ComposeImage();
            Clipboard.SetImage(img);
            app.ShowNotification("복사 완료", "판서 화면을 클립보드에 복사했습니다. (Ctrl+V로 붙여넣기)");
        }
        catch (Exception ex)
        {
            MessageBox.Show(this, "클립보드에 복사하지 못했습니다.\n\n" + ex.Message, "화면 판서",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
    }

    #endregion

    #region 마우스 입력

    protected override void OnMouseDown(MouseEventArgs e)
    {
        base.OnMouseDown(e);
        if (dragTool != null) return;

        if (e.Button == MouseButtons.Left) dragTool = Tool;
        else if (e.Button == MouseButtons.Right) dragTool = DrawTool.Eraser;   // 우클릭 드래그 = 지우개
        else return;
        dragButton = e.Button;
        mousePos = e.Location;

        switch (dragTool)
        {
            case DrawTool.Pen:
            case DrawTool.Highlighter:
                currentStroke = new InkStroke(PenColor, WidthFor(dragTool.Value), dragTool == DrawTool.Highlighter);
                currentStroke.AddPoint(e.Location);
                InvalidateSegment(e.Location, e.Location, currentStroke.Width);
                break;

            case DrawTool.Eraser:
                strokesBeforeErase = strokes;
                erasedDuringDrag = false;
                lastErasePoint = e.Location;
                EraseAlong(e.Location, e.Location);
                InvalidateEraserCursor(e.Location);
                break;

            case DrawTool.AreaEraser:
                areaStart = e.Location;
                areaRect = Rectangle.Empty;
                break;
        }
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        base.OnMouseMove(e);
        Point oldPos = mousePos;
        mousePos = e.Location;
        mouseInside = true;

        switch (dragTool)
        {
            case DrawTool.Pen or DrawTool.Highlighter when currentStroke != null:
                var last = currentStroke.LastPoint;
                if (Math.Abs(last.X - e.X) + Math.Abs(last.Y - e.Y) < 1) break;
                currentStroke.AddPoint(e.Location);
                InvalidateSegment(last, e.Location, currentStroke.Width);
                break;

            case DrawTool.Eraser:
                EraseAlong(lastErasePoint, e.Location);
                lastErasePoint = e.Location;
                break;

            case DrawTool.AreaEraser:
                var oldRect = areaRect;
                areaRect = RectFromPoints(areaStart, e.Location);
                var dirty = Rectangle.Union(oldRect, areaRect);
                dirty.Inflate(4, 4);
                Invalidate(dirty);
                break;
        }

        if (ShowEraserCursor)
        {
            InvalidateEraserCursor(oldPos);
            InvalidateEraserCursor(mousePos);
        }
    }

    protected override void OnMouseUp(MouseEventArgs e)
    {
        base.OnMouseUp(e);
        if (dragTool != null && e.Button == dragButton) EndDrag(e.Location);
    }

    protected override void OnMouseCaptureChanged(EventArgs e)
    {
        base.OnMouseCaptureChanged(e);
        // 대화상자 등으로 마우스 입력을 잃었을 때 그리던 내용을 마무리
        if (dragTool != null && !Capture) EndDrag(mousePos);
    }

    private void EndDrag(Point p)
    {
        var tool = dragTool;
        dragTool = null;

        switch (tool)
        {
            case DrawTool.Pen:
            case DrawTool.Highlighter:
                var s = currentStroke;
                currentStroke = null;
                if (s == null) break;
                s.Finish();
                using (var g = Graphics.FromImage(inkLayer))
                {
                    g.SmoothingMode = SmoothingMode.AntiAlias;
                    s.Draw(g);
                }
                Commit(new List<InkStroke>(strokes) { s }, rebuild: false);
                var r = Rectangle.Ceiling(s.Bounds);
                r.Inflate((int)s.Width + 2, (int)s.Width + 2);
                Invalidate(r);
                break;

            case DrawTool.Eraser:
                if (erasedDuringDrag && strokesBeforeErase != null)
                {
                    undoStack.Push(strokesBeforeErase);
                    redoStack.Clear();
                    hasUnsavedChanges = true;
                    OnStateChanged();
                }
                strokesBeforeErase = null;
                Invalidate();
                break;

            case DrawTool.AreaEraser:
                var rect = RectFromPoints(areaStart, p);
                areaRect = Rectangle.Empty;
                Invalidate();
                if (rect.Width > 2 && rect.Height > 2) EraseRect(rect);
                break;
        }
    }

    protected override void OnMouseLeave(EventArgs e)
    {
        base.OnMouseLeave(e);
        mouseInside = false;
        InvalidateEraserCursor(mousePos);
    }

    private static Rectangle RectFromPoints(Point a, Point b) =>
        Rectangle.FromLTRB(Math.Min(a.X, b.X), Math.Min(a.Y, b.Y), Math.Max(a.X, b.X), Math.Max(a.Y, b.Y));

    private void InvalidateSegment(PointF a, PointF b, float width)
    {
        int pad = (int)Math.Ceiling(width) + 2;
        var r = Rectangle.FromLTRB((int)Math.Min(a.X, b.X) - pad, (int)Math.Min(a.Y, b.Y) - pad,
                                   (int)Math.Max(a.X, b.X) + pad, (int)Math.Max(a.Y, b.Y) + pad);
        Invalidate(r);
    }

    private void InvalidateEraserCursor(Point center)
    {
        int r = (int)Math.Ceiling(EraserRadius) + 3;
        Invalidate(new Rectangle(center.X - r, center.Y - r, r * 2, r * 2));
    }

    #endregion

    #region 키보드 단축키

    protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
    {
        if (HandleShortcut(keyData)) return true;
        return base.ProcessCmdKey(ref msg, keyData);
    }

    /// <summary>도구 모음 창에서도 같은 단축키를 쓰기 위해 public 으로 둡니다.</summary>
    public bool HandleShortcut(Keys keyData)
    {
        switch (keyData)
        {
            case Keys.Control | Keys.Z: Undo(); return true;
            case Keys.Control | Keys.Y:
            case Keys.Control | Keys.Shift | Keys.Z: Redo(); return true;
            case Keys.Control | Keys.S: SaveImage(); return true;
            case Keys.Control | Keys.C: CopyToClipboard(); return true;
            case Keys.P: SetTool(DrawTool.Pen); return true;
            case Keys.H: SetTool(DrawTool.Highlighter); return true;
            case Keys.E: SetTool(DrawTool.Eraser); return true;
            case Keys.A: SetTool(DrawTool.AreaEraser); return true;
            case Keys.Delete: ClearAll(); return true;
            case Keys.OemOpenBrackets: SetSizeIndex(SizeIndex - 1); return true;
            case Keys.OemCloseBrackets: SetSizeIndex(SizeIndex + 1); return true;
            case Keys.Escape: RequestStandby(); return true;
        }

        int colorIndex = keyData switch
        {
            >= Keys.D1 and <= Keys.D9 => keyData - Keys.D1,
            >= Keys.NumPad1 and <= Keys.NumPad9 => keyData - Keys.NumPad1,
            _ => -1
        };
        if (colorIndex >= 0 && colorIndex < Palette.Length)
        {
            SetColor(Palette[colorIndex]);
            return true;
        }
        return false;
    }

    #endregion

    #region 그리기

    protected override void OnPaintBackground(PaintEventArgs e)
    {
        // OnPaint 에서 전체를 그리므로 배경 지우기 생략 (깜박임 방지)
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        var g = e.Graphics;
        var clip = e.ClipRectangle;

        // 1) 정지된 화면 + 2) 판서 레이어 (바뀐 부분만 다시 그림)
        g.CompositingMode = CompositingMode.SourceCopy;
        g.InterpolationMode = InterpolationMode.NearestNeighbor;
        g.DrawImage(background, clip, clip, GraphicsUnit.Pixel);
        g.CompositingMode = CompositingMode.SourceOver;
        g.DrawImage(inkLayer, clip, clip, GraphicsUnit.Pixel);

        g.SmoothingMode = SmoothingMode.AntiAlias;

        // 3) 지금 그리고 있는 획
        currentStroke?.Draw(g);

        // 4) 영역 지우기 사각형
        if (dragTool == DrawTool.AreaEraser && areaRect.Width > 0 && areaRect.Height > 0)
        {
            using var fill = new SolidBrush(Color.FromArgb(50, 0, 120, 215));
            using var border = new Pen(Color.FromArgb(0, 120, 215), 2f) { DashStyle = DashStyle.Dash };
            g.FillRectangle(fill, areaRect);
            g.DrawRectangle(border, areaRect);
        }

        // 5) 지우개 모양 (원)
        if (ShowEraserCursor)
        {
            float r = EraserRadius;
            var rect = new RectangleF(mousePos.X - r, mousePos.Y - r, r * 2, r * 2);
            using var fill = new SolidBrush(Color.FromArgb(60, Color.White));
            using var outer = new Pen(Color.Black, 1.5f);
            using var inner = new Pen(Color.White, 1f) { DashStyle = DashStyle.Dash };
            g.FillEllipse(fill, rect);
            g.DrawEllipse(outer, rect);
            g.DrawEllipse(inner, rect);
        }
    }

    private void RebuildInkLayer()
    {
        using var g = Graphics.FromImage(inkLayer);
        g.Clear(Color.Transparent);
        g.SmoothingMode = SmoothingMode.AntiAlias;
        foreach (var s in strokes) s.Draw(g);
    }

    #endregion
}
