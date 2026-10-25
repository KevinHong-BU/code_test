using System.Drawing.Imaging;

namespace ScreenPen;

/// <summary>
/// 프로그램 전체 흐름을 관리합니다.
///  - 판서 모드: 현재 화면을 캡처(정지)해서 전체 화면으로 띄우고 그 위에 판서
///  - 대기 모드: 판서 화면을 닫고, 화면 위에 작은 "판서" 버튼만 표시
///  - 종료: 프로그램 완전 종료
/// </summary>
public sealed class AppController : ApplicationContext
{
    public const string HotKeyText = "Ctrl+Shift+D";

    private readonly SynchronizationContext uiContext;
    private readonly Icon appIcon;
    private readonly FloatingButtonForm floatingButton;
    private readonly NotifyIcon trayIcon;
    private readonly GlobalHotKey hotKey;
    private readonly ToolStripMenuItem trayStartItem;
    private readonly ToolStripMenuItem trayStandbyItem;

    private OverlayForm? overlay;
    private bool isStarting;
    private bool isExiting;

    public AppController(bool startInStandby)
    {
        appIcon = IconFactory.CreateAppIcon();

        floatingButton = new FloatingButtonForm(this, appIcon);
        _ = floatingButton.Handle; // 창 핸들을 미리 만들어 둠 (WinForms 동기화 컨텍스트 설치)
        uiContext = SynchronizationContext.Current ?? new WindowsFormsSynchronizationContext();

        // 시스템 트레이 아이콘
        trayStartItem = new ToolStripMenuItem($"판서 시작 ({HotKeyText})", null, (_, _) => StartAnnotation());
        trayStandbyItem = new ToolStripMenuItem("판서 끄기 (대기)", null, (_, _) => RequestStandby());
        var trayMenu = new ContextMenuStrip();
        trayMenu.Items.Add(trayStartItem);
        trayMenu.Items.Add(trayStandbyItem);
        trayMenu.Items.Add(new ToolStripSeparator());
        trayMenu.Items.Add(new ToolStripMenuItem("종료", null, (_, _) => RequestExit()));

        trayIcon = new NotifyIcon
        {
            Icon = appIcon,
            Text = "화면 판서",
            ContextMenuStrip = trayMenu,
            Visible = true
        };
        trayIcon.MouseClick += (_, e) =>
        {
            if (e.Button == MouseButtons.Left && overlay == null) StartAnnotation();
        };

        // 전역 단축키: 판서 시작 ↔ 대기 전환
        hotKey = new GlobalHotKey(NativeMethods.MOD_CONTROL | NativeMethods.MOD_SHIFT, Keys.D);
        hotKey.Pressed += (_, _) =>
        {
            if (overlay == null) StartAnnotation();
            else RequestStandby();
        };

        // 메시지 루프가 시작된 뒤에 첫 화면을 띄움
        var startTimer = new System.Windows.Forms.Timer { Interval = 100 };
        startTimer.Tick += (_, _) =>
        {
            startTimer.Stop();
            startTimer.Dispose();
            if (startInStandby) ShowStandby();
            else StartAnnotation();
        };
        startTimer.Start();
    }

    public bool HotKeyAvailable => hotKey.IsRegistered;

    /// <summary>현재 화면을 정지(캡처)하고 판서 모드를 시작합니다.</summary>
    public async void StartAnnotation()
    {
        if (overlay != null || isStarting || isExiting) return;
        isStarting = true;
        try
        {
            // 대기 버튼이 캡처 화면에 찍히지 않도록 먼저 숨김
            floatingButton.Hide();
            await Task.Delay(200);

            Screen screen = Screen.FromPoint(Cursor.Position);
            Bitmap shot = CaptureScreen(screen.Bounds);

            overlay = new OverlayForm(this, screen.Bounds, shot);
            overlay.Show();
            overlay.Activate();
        }
        catch (Exception ex)
        {
            overlay = null;
            MessageBox.Show("화면을 캡처하지 못했습니다.\n\n" + ex.Message, "화면 판서",
                MessageBoxButtons.OK, MessageBoxIcon.Error);
            ShowStandby();
        }
        finally
        {
            isStarting = false;
            UpdateTrayMenu();
        }
    }

    /// <summary>판서 기능만 끄고 대기 버튼을 표시합니다. (UI 이벤트 처리 후 실행)</summary>
    public void RequestStandby() => uiContext.Post(_ => EnterStandby(), null);

    /// <summary>프로그램을 완전히 종료합니다. (UI 이벤트 처리 후 실행)</summary>
    public void RequestExit() => uiContext.Post(_ => ExitApplication(), null);

    public void ShowNotification(string title, string text)
    {
        trayIcon.ShowBalloonTip(2500, title, text, ToolTipIcon.Info);
    }

    private void EnterStandby()
    {
        if (isExiting) return;
        if (overlay != null)
        {
            if (!overlay.ConfirmDiscard()) return;
            CloseOverlay();
        }
        ShowStandby();
    }

    private void ShowStandby()
    {
        floatingButton.Show();
        floatingButton.BringToFront();
        UpdateTrayMenu();
    }

    private void ExitApplication()
    {
        if (isExiting) return;
        if (overlay != null && !overlay.ConfirmDiscard()) return;

        isExiting = true;
        CloseOverlay();
        floatingButton.Close();
        floatingButton.Dispose();
        trayIcon.Visible = false;
        trayIcon.Dispose();
        hotKey.Dispose();
        appIcon.Dispose();
        ExitThread();
    }

    private void CloseOverlay()
    {
        var o = overlay;
        overlay = null;
        if (o != null)
        {
            o.CloseOverlay();
            o.Dispose();
        }
        UpdateTrayMenu();
    }

    private void UpdateTrayMenu()
    {
        if (isExiting) return;
        trayStartItem.Enabled = overlay == null;
        trayStandbyItem.Enabled = overlay != null;
    }

    private static Bitmap CaptureScreen(Rectangle bounds)
    {
        var bmp = new Bitmap(bounds.Width, bounds.Height, PixelFormat.Format32bppPArgb);
        using var g = Graphics.FromImage(bmp);
        g.CopyFromScreen(bounds.Location, Point.Empty, bounds.Size, CopyPixelOperation.SourceCopy);
        return bmp;
    }
}
