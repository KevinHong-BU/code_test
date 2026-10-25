using System.Runtime.InteropServices;

namespace FastFind;

/// <summary>확장자별 Windows 아이콘을 한 번만 읽어 ImageList에 모아 둡니다.</summary>
internal sealed class ShellIcons
{
    private const uint SHGFI_ICON = 0x100;
    private const uint SHGFI_SMALLICON = 0x1;
    private const uint SHGFI_USEFILEATTRIBUTES = 0x10;
    private const uint FILE_ATTRIBUTE_NORMAL = 0x80;

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct SHFILEINFOW
    {
        public IntPtr hIcon;
        public int iIcon;
        public uint dwAttributes;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)] public string szDisplayName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 80)] public string szTypeName;
    }

    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr SHGetFileInfoW(string pszPath, uint dwFileAttributes, ref SHFILEINFOW psfi,
        uint cbFileInfo, uint uFlags);

    [DllImport("user32.dll")]
    private static extern bool DestroyIcon(IntPtr hIcon);

    private readonly Dictionary<string, int> _byExt = new(StringComparer.OrdinalIgnoreCase);

    public ImageList List { get; }

    public ShellIcons(Size size)
    {
        List = new ImageList { ColorDepth = ColorDepth.Depth32Bit, ImageSize = size };
    }

    public int Get(string name, bool isDirectory)
    {
        string key;
        if (isDirectory) key = "<dir>";
        else
        {
            int dot = name.LastIndexOf('.');
            key = dot < 0 ? "<none>" : name[dot..];
        }
        if (_byExt.TryGetValue(key, out int i)) return i;

        int index = -1;
        var info = new SHFILEINFOW();
        string probe = isDirectory ? "folder" : "file" + (key.StartsWith('.') ? key : "");
        uint attr = isDirectory ? NativeMethods.FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        if (SHGetFileInfoW(probe, attr, ref info, (uint)Marshal.SizeOf<SHFILEINFOW>(),
                SHGFI_ICON | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES) != IntPtr.Zero && info.hIcon != IntPtr.Zero)
        {
            using (var icon = Icon.FromHandle(info.hIcon))
                List.Images.Add(icon.ToBitmap());
            DestroyIcon(info.hIcon);
            index = List.Images.Count - 1;
        }
        _byExt[key] = index;
        return index;
    }
}
