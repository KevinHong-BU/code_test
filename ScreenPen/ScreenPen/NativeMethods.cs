using System.Runtime.InteropServices;

namespace ScreenPen;

internal static class NativeMethods
{
    public const int WM_NCLBUTTONDOWN = 0x00A1;
    public const int HTCAPTION = 2;
    public const int WM_HOTKEY = 0x0312;

    public const int WS_EX_TOOLWINDOW = 0x00000080;   // Alt+Tab 목록에 표시하지 않음
    public const int WS_EX_NOACTIVATE = 0x08000000;   // 클릭해도 포커스를 빼앗지 않음

    public const uint MOD_ALT = 0x0001;
    public const uint MOD_CONTROL = 0x0002;
    public const uint MOD_SHIFT = 0x0004;
    public const uint MOD_NOREPEAT = 0x4000;

    [DllImport("user32.dll")]
    public static extern bool ReleaseCapture();

    [DllImport("user32.dll")]
    public static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool RegisterHotKey(IntPtr hWnd, int id, uint fsModifiers, uint vk);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool UnregisterHotKey(IntPtr hWnd, int id);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool DestroyIcon(IntPtr hIcon);

    /// <summary>테두리 없는 창을 마우스로 끌어서 이동시킵니다.</summary>
    public static void BeginWindowDrag(Form form)
    {
        ReleaseCapture();
        SendMessage(form.Handle, WM_NCLBUTTONDOWN, (IntPtr)HTCAPTION, IntPtr.Zero);
    }
}

/// <summary>프로그램이 다른 창 뒤에 있어도 동작하는 전역 단축키.</summary>
internal sealed class GlobalHotKey : NativeWindow, IDisposable
{
    private const int HotKeyId = 0x5150;
    private static readonly IntPtr HWND_MESSAGE = new(-3);

    public event EventHandler? Pressed;
    public bool IsRegistered { get; }

    public GlobalHotKey(uint modifiers, Keys key)
    {
        CreateHandle(new CreateParams { Parent = HWND_MESSAGE });
        IsRegistered = NativeMethods.RegisterHotKey(Handle, HotKeyId,
            modifiers | NativeMethods.MOD_NOREPEAT, (uint)key);
    }

    protected override void WndProc(ref Message m)
    {
        if (m.Msg == NativeMethods.WM_HOTKEY && m.WParam == (IntPtr)HotKeyId)
            Pressed?.Invoke(this, EventArgs.Empty);
        base.WndProc(ref m);
    }

    public void Dispose()
    {
        if (Handle != IntPtr.Zero)
        {
            if (IsRegistered) NativeMethods.UnregisterHotKey(Handle, HotKeyId);
            DestroyHandle();
        }
    }
}
