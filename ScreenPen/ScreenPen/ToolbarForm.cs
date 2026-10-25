namespace ScreenPen;

/// <summary>
/// 판서 화면 위에 떠 있는 제어 도구 모음.
/// 펜/형광펜, 색상, 굵기, 지우기(부분/영역/전체), 실행 취소, 저장, 판서 끄기, 종료 버튼을 제공합니다.
/// </summary>
public sealed class ToolbarForm : Form
{
    private static readonly Color BarColor = Color.FromArgb(40, 40, 44);
    private static readonly Color ButtonColor = Color.FromArgb(62, 62, 68);
    private static readonly Color HoverColor = Color.FromArgb(85, 85, 92);
    private static readonly Color SelectedColor = IconFactory.AccentColor;

    private readonly OverlayForm overlay;
    private readonly FlowLayoutPanel panel;
    private readonly ToolTip tip = new() { InitialDelay = 300 };
    private readonly Dictionary<DrawTool, Button> toolButtons = new();
    private readonly List<(Button Button, Color Color)> colorButtons = new();
    private readonly Button[] sizeButtons = new Button[OverlayForm.SizeNames.Length];
    private readonly List<Control> collapsible = new();
    private readonly Button collapseButton;
    private readonly Button customColorButton;
    private readonly Button undoButton;
    private readonly Button redoButton;
    private bool collapsed;
    private Rectangle screenBounds;

    public ToolbarForm(OverlayForm overlay, bool hotKeyAvailable)
    {
        this.overlay = overlay;

        Text = "판서 도구";
        FormBorderStyle = FormBorderStyle.None;
        StartPosition = FormStartPosition.Manual;
        ShowInTaskbar = false;
        TopMost = true;
        KeyPreview = true;
        BackColor = BarColor;
        AutoSize = true;
        AutoSizeMode = AutoSizeMode.GrowAndShrink;
        Padding = new Padding(Dpi(3));

        panel = new FlowLayoutPanel
        {
            AutoSize = true,
            AutoSizeMode = AutoSizeMode.GrowAndShrink,
            WrapContents = true,
            FlowDirection = FlowDirection.LeftToRight,
            BackColor = BarColor,
            Margin = Padding.Empty,
            Padding = Padding.Empty
        };
        Controls.Add(panel);

        // 이동 손잡이
        var grip = new Label
        {
            Text = "∷",
            ForeColor = Color.Silver,
            AutoSize = false,
            Size = new Size(Dpi(18), Dpi(32)),
            TextAlign = ContentAlignment.MiddleCenter,
            Cursor = Cursors.SizeAll,
            Margin = new Padding(Dpi(2))
        };
        grip.MouseDown += (_, e) => { if (e.Button == MouseButtons.Left) NativeMethods.BeginWindowDrag(this); };
        tip.SetToolTip(grip, "끌어서 도구 모음 이동");
        panel.Controls.Add(grip);

        collapseButton = AddButton("◀", "도구 모음 접기 / 펴기", ToggleCollapse, isCollapsible: false);

        // 펜 종류
        AddSeparator();
        toolButtons[DrawTool.Pen] = AddButton("펜", "펜 (P)", () => overlay.SetTool(DrawTool.Pen));
        toolButtons[DrawTool.Highlighter] = AddButton("형광펜", "형광펜 (H)", () => overlay.SetTool(DrawTool.Highlighter));

        // 색상
        AddSeparator();
        for (int i = 0; i < OverlayForm.Palette.Length; i++)
            AddColorButton(OverlayForm.Palette[i], $"색상 {i + 1}  (단축키 {i + 1})");
        customColorButton = AddButton("+", "다른 색 고르기", PickCustomColor);
        customColorButton.AutoSize = false;
        customColorButton.Size = new Size(Dpi(30), Dpi(30));

        // 굵기
        AddSeparator();
        for (int i = 0; i < sizeButtons.Length; i++)
        {
            int index = i;
            sizeButtons[i] = AddButton(OverlayForm.SizeNames[i],
                $"굵기: {OverlayForm.SizeNames[i]}  ( [ / ] 키로 조절)\n지우개 선택 시에는 지우개 크기",
                () => overlay.SetSizeIndex(index));
        }

        // 지우기
        AddSeparator();
        toolButtons[DrawTool.Eraser] = AddButton("지우개", "부분 지우개 (E)\n문지른 부분만 지웁니다.\n펜 사용 중 마우스 오른쪽 버튼으로 끌어도 지워집니다.",
            () => overlay.SetTool(DrawTool.Eraser));
        toolButtons[DrawTool.AreaEraser] = AddButton("영역 지우기", "영역 지우기 (A)\n사각형을 끌어서 그 안의 판서를 지웁니다.",
            () => overlay.SetTool(DrawTool.AreaEraser));
        AddButton("전체 지우기", "판서 전체 지우기 (Delete)", overlay.ClearAll);

        // 실행 취소
        AddSeparator();
        undoButton = AddButton("실행취소", "실행 취소 (Ctrl+Z)", overlay.Undo);
        redoButton = AddButton("다시실행", "다시 실행 (Ctrl+Y)", overlay.Redo);

        // 저장
        AddSeparator();
        AddButton("저장", "판서한 화면을 그림 파일로 저장 (Ctrl+S)", () => overlay.SaveImage());
        AddButton("복사", "판서한 화면을 클립보드에 복사 (Ctrl+C)", overlay.CopyToClipboard);

        // 끝내기
        AddSeparator();
        string hotKeyHint = hotKeyAvailable ? $"\n다시 시작: 화면의 [판서] 버튼 또는 {AppController.HotKeyText}" : "\n다시 시작: 화면의 [판서] 버튼";
        var standby = AddButton("판서 끄기", "판서 기능만 끄고 대기 (Esc)" + hotKeyHint, overlay.RequestStandby);
        standby.BackColor = Color.FromArgb(90, 90, 40);
        var exit = AddButton("종료", "프로그램 완전히 종료", overlay.RequestExit);
        exit.BackColor = Color.FromArgb(170, 45, 45);
        exit.FlatAppearance.MouseOverBackColor = Color.FromArgb(200, 60, 60);

        overlay.StateChanged += (_, _) => UpdateState();
        UpdateState();
    }

    private int Dpi(int value) => (int)Math.Round(value * DeviceDpi / 96.0);

    protected override CreateParams CreateParams
    {
        get
        {
            var cp = base.CreateParams;
            cp.ExStyle |= NativeMethods.WS_EX_TOOLWINDOW;
            return cp;
        }
    }

    /// <summary>화면 위쪽 가운데에 배치합니다. 화면이 좁으면 버튼이 여러 줄로 나뉩니다.</summary>
    public void PlaceAtTop(Rectangle screen)
    {
        screenBounds = screen;
        panel.MaximumSize = new Size(screen.Width - Dpi(40), 0);
        PerformLayout();
        Location = new Point(screen.Left + (screen.Width - Width) / 2, screen.Top + Dpi(8));
    }

    protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
    {
        // 도구 모음에 포커스가 있어도 판서 단축키가 동작하도록 전달
        if (overlay.HandleShortcut(keyData)) return true;
        return base.ProcessCmdKey(ref msg, keyData);
    }

    private Button AddButton(string text, string tooltip, Action onClick, bool isCollapsible = true)
    {
        var b = new Button
        {
            Text = text,
            FlatStyle = FlatStyle.Flat,
            ForeColor = Color.White,
            BackColor = ButtonColor,
            AutoSize = true,
            AutoSizeMode = AutoSizeMode.GrowAndShrink,
            MinimumSize = new Size(Dpi(34), Dpi(32)),
            Padding = new Padding(Dpi(4), 0, Dpi(4), 0),
            Margin = new Padding(Dpi(2)),
            TabStop = false,
            UseVisualStyleBackColor = false,
            Cursor = Cursors.Hand
        };
        b.FlatAppearance.BorderSize = 0;
        b.FlatAppearance.MouseOverBackColor = HoverColor;
        b.Click += (_, _) => onClick();
        tip.SetToolTip(b, tooltip);
        AddControl(b, isCollapsible);
        return b;
    }

    private void AddColorButton(Color color, string tooltip)
    {
        var b = new Button
        {
            FlatStyle = FlatStyle.Flat,
            BackColor = color,
            ForeColor = IsLight(color) ? Color.Black : Color.White,
            Size = new Size(Dpi(30), Dpi(30)),
            Margin = new Padding(Dpi(2), Dpi(3), Dpi(2), Dpi(3)),
            TabStop = false,
            UseVisualStyleBackColor = false,
            Cursor = Cursors.Hand
        };
        b.FlatAppearance.BorderColor = Color.Gray;
        b.FlatAppearance.MouseOverBackColor = color;
        b.FlatAppearance.MouseDownBackColor = color;
        b.Click += (_, _) => overlay.SetColor(color);
        tip.SetToolTip(b, tooltip);
        AddControl(b, true);
        colorButtons.Add((b, color));
    }

    private void AddSeparator()
    {
        var sep = new Label
        {
            AutoSize = false,
            Size = new Size(1, Dpi(26)),
            BackColor = Color.FromArgb(90, 90, 96),
            Margin = new Padding(Dpi(5), Dpi(5), Dpi(5), Dpi(5))
        };
        AddControl(sep, true);
    }

    private void AddControl(Control c, bool isCollapsible)
    {
        panel.Controls.Add(c);
        if (isCollapsible) collapsible.Add(c);
    }

    private void ToggleCollapse()
    {
        collapsed = !collapsed;
        panel.SuspendLayout();
        foreach (var c in collapsible) c.Visible = !collapsed;
        panel.ResumeLayout();
        collapseButton.Text = collapsed ? "▶" : "◀";

        // 펼쳤을 때 화면 밖으로 나가지 않도록 위치 보정
        if (!collapsed && !screenBounds.IsEmpty && Right > screenBounds.Right)
            Left = Math.Max(screenBounds.Left, screenBounds.Right - Width);
    }

    private void PickCustomColor()
    {
        using var dlg = new ColorDialog { Color = overlay.PenColor, FullOpen = true, AnyColor = true };
        if (dlg.ShowDialog(this) == DialogResult.OK) overlay.SetColor(dlg.Color);
    }

    private void UpdateState()
    {
        foreach (var (tool, button) in toolButtons)
            button.BackColor = tool == overlay.Tool ? SelectedColor : ButtonColor;

        bool isDrawing = overlay.Tool is DrawTool.Pen or DrawTool.Highlighter;
        bool paletteColor = false;
        foreach (var (button, color) in colorButtons)
        {
            bool selected = color.ToArgb() == overlay.PenColor.ToArgb();
            paletteColor |= selected;
            button.Text = selected && isDrawing ? "●" : "";
            button.FlatAppearance.BorderSize = selected ? Dpi(2) : 1;
            button.FlatAppearance.BorderColor = selected ? Color.DeepSkyBlue : Color.Gray;
        }

        if (paletteColor)
        {
            customColorButton.BackColor = ButtonColor;
            customColorButton.ForeColor = Color.White;
            customColorButton.FlatAppearance.MouseOverBackColor = HoverColor;
            customColorButton.FlatAppearance.BorderSize = 0;
        }
        else
        {
            customColorButton.BackColor = overlay.PenColor;
            customColorButton.ForeColor = IsLight(overlay.PenColor) ? Color.Black : Color.White;
            customColorButton.FlatAppearance.MouseOverBackColor = overlay.PenColor;
            customColorButton.FlatAppearance.BorderSize = Dpi(2);
            customColorButton.FlatAppearance.BorderColor = Color.DeepSkyBlue;
        }

        bool sizeApplies = overlay.Tool != DrawTool.AreaEraser;
        for (int i = 0; i < sizeButtons.Length; i++)
        {
            sizeButtons[i].Enabled = sizeApplies;
            sizeButtons[i].BackColor = sizeApplies && i == overlay.SizeIndex ? SelectedColor : ButtonColor;
        }

        undoButton.Enabled = overlay.CanUndo;
        redoButton.Enabled = overlay.CanRedo;
    }

    private static bool IsLight(Color c) => c.R * 0.299 + c.G * 0.587 + c.B * 0.114 > 160;
}
