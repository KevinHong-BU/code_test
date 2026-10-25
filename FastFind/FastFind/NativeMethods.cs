using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace FastFind;

/// <summary>NTFS 볼륨 직접 읽기, 폴더 탐색, 파일 정보 조회에 쓰는 Win32 API.</summary>
internal static class NativeMethods
{
    public const uint GENERIC_READ = 0x80000000;
    public const uint FILE_SHARE_READ = 0x1;
    public const uint FILE_SHARE_WRITE = 0x2;
    public const uint FILE_SHARE_DELETE = 0x4;
    public const uint OPEN_EXISTING = 3;
    public const uint FILE_FLAG_BACKUP_SEMANTICS = 0x02000000;

    public const uint FSCTL_GET_NTFS_VOLUME_DATA = 0x00090064;
    public const uint FSCTL_ENUM_USN_DATA = 0x000900B3;
    public const uint FSCTL_READ_USN_JOURNAL = 0x000900BB;
    public const uint FSCTL_CREATE_USN_JOURNAL = 0x000900E7;
    public const uint FSCTL_QUERY_USN_JOURNAL = 0x000900F4;

    public const int ERROR_ACCESS_DENIED = 5;
    public const int ERROR_HANDLE_EOF = 38;
    public const int ERROR_INVALID_PARAMETER = 87;
    public const int ERROR_JOURNAL_DELETE_IN_PROGRESS = 1178;
    public const int ERROR_JOURNAL_NOT_ACTIVE = 1179;
    public const int ERROR_JOURNAL_ENTRY_DELETED = 1181;

    public const uint FILE_ATTRIBUTE_HIDDEN = 0x2;
    public const uint FILE_ATTRIBUTE_SYSTEM = 0x4;
    public const uint FILE_ATTRIBUTE_DIRECTORY = 0x10;
    public const uint FILE_ATTRIBUTE_REPARSE_POINT = 0x400;

    public const uint USN_REASON_FILE_CREATE = 0x00000100;
    public const uint USN_REASON_FILE_DELETE = 0x00000200;
    public const uint USN_REASON_RENAME_OLD_NAME = 0x00001000;
    public const uint USN_REASON_RENAME_NEW_NAME = 0x00002000;
    public const uint USN_REASON_BASIC_INFO_CHANGE = 0x00008000;

    public const int FIND_FIRST_EX_LARGE_FETCH = 2;

    [StructLayout(LayoutKind.Sequential)]
    public struct USN_JOURNAL_DATA_V0
    {
        public ulong UsnJournalID;
        public long FirstUsn;
        public long NextUsn;
        public long LowestValidUsn;
        public long MaxUsn;
        public ulong MaximumSize;
        public ulong AllocationDelta;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct CREATE_USN_JOURNAL_DATA
    {
        public ulong MaximumSize;
        public ulong AllocationDelta;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct MFT_ENUM_DATA_V0
    {
        public ulong StartFileReferenceNumber;
        public long LowUsn;
        public long HighUsn;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct READ_USN_JOURNAL_DATA_V0
    {
        public long StartUsn;
        public uint ReasonMask;
        public uint ReturnOnlyOnClose;
        public ulong Timeout;
        public ulong BytesToWaitFor;
        public ulong UsnJournalID;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct NTFS_VOLUME_DATA_BUFFER
    {
        public long VolumeSerialNumber;
        public long NumberSectors;
        public long TotalClusters;
        public long FreeClusters;
        public long TotalReserved;
        public uint BytesPerSector;
        public uint BytesPerCluster;
        public uint BytesPerFileRecordSegment;
        public uint ClustersPerFileRecordSegment;
        public long MftValidDataLength;
        public long MftStartLcn;
        public long Mft2StartLcn;
        public long MftZoneStart;
        public long MftZoneEnd;
    }

    // FILETIME은 4바이트 정렬이라 Pack = 4가 없으면 long 앞에 빈칸이 생겨 필드 위치가 어긋납니다
    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    public struct BY_HANDLE_FILE_INFORMATION
    {
        public uint FileAttributes;
        public long CreationTime;
        public long LastAccessTime;
        public long LastWriteTime;
        public uint VolumeSerialNumber;
        public uint FileSizeHigh;
        public uint FileSizeLow;
        public uint NumberOfLinks;
        public uint FileIndexHigh;
        public uint FileIndexLow;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode, Pack = 4)]
    public struct WIN32_FIND_DATAW
    {
        public uint dwFileAttributes;
        public long ftCreationTime;
        public long ftLastAccessTime;
        public long ftLastWriteTime;
        public uint nFileSizeHigh;
        public uint nFileSizeLow;
        public uint dwReserved0;
        public uint dwReserved1;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)]
        public string cFileName;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 14)]
        public string cAlternateFileName;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 4)]
    public struct WIN32_FILE_ATTRIBUTE_DATA
    {
        public uint dwFileAttributes;
        public long ftCreationTime;
        public long ftLastAccessTime;
        public long ftLastWriteTime;
        public uint nFileSizeHigh;
        public uint nFileSizeLow;
    }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern SafeFileHandle CreateFileW(string lpFileName, uint dwDesiredAccess, uint dwShareMode,
        IntPtr lpSecurityAttributes, uint dwCreationDisposition, uint dwFlagsAndAttributes, IntPtr hTemplateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool DeviceIoControl(SafeFileHandle hDevice, uint dwIoControlCode,
        IntPtr lpInBuffer, int nInBufferSize, IntPtr lpOutBuffer, int nOutBufferSize,
        out int lpBytesReturned, IntPtr lpOverlapped);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool GetFileInformationByHandle(SafeFileHandle hFile, out BY_HANDLE_FILE_INFORMATION info);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr FindFirstFileExW(string lpFileName, int fInfoLevelId, out WIN32_FIND_DATAW lpFindFileData,
        int fSearchOp, IntPtr lpSearchFilter, int dwAdditionalFlags);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool FindNextFileW(IntPtr hFindFile, out WIN32_FIND_DATAW lpFindFileData);

    [DllImport("kernel32.dll")]
    public static extern bool FindClose(IntPtr hFindFile);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool GetFileAttributesExW(string lpFileName, int fInfoLevelId, out WIN32_FILE_ATTRIBUTE_DATA data);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool GetVolumeInformationW(string rootPathName, IntPtr volumeNameBuffer, int volumeNameSize,
        out uint volumeSerialNumber, out uint maximumComponentLength, out uint fileSystemFlags,
        [Out] char[] fileSystemNameBuffer, int nFileSystemNameSize);

    public static readonly IntPtr INVALID_HANDLE_VALUE = new(-1);

    /// <summary>구조체 입력/출력을 쓰는 DeviceIoControl 도우미.</summary>
    public static unsafe bool Ioctl<TIn, TOut>(SafeFileHandle h, uint code, ref TIn input, out TOut output)
        where TIn : unmanaged where TOut : unmanaged
    {
        output = default;
        fixed (TIn* pIn = &input)
        fixed (TOut* pOut = &output)
        {
            return DeviceIoControl(h, code, (IntPtr)pIn, sizeof(TIn), (IntPtr)pOut, sizeof(TOut), out _, IntPtr.Zero);
        }
    }

    public static unsafe bool IoctlOut<TOut>(SafeFileHandle h, uint code, out TOut output) where TOut : unmanaged
    {
        output = default;
        fixed (TOut* pOut = &output)
        {
            return DeviceIoControl(h, code, IntPtr.Zero, 0, (IntPtr)pOut, sizeof(TOut), out _, IntPtr.Zero);
        }
    }

    /// <summary>구조체 입력 + 바이트 버퍼 출력. 반환값: 받은 바이트 수, 실패하면 -1.</summary>
    public static unsafe int IoctlBuffer<TIn>(SafeFileHandle h, uint code, ref TIn input, byte[] buffer)
        where TIn : unmanaged
    {
        fixed (TIn* pIn = &input)
        fixed (byte* pOut = buffer)
        {
            return DeviceIoControl(h, code, (IntPtr)pIn, sizeof(TIn), (IntPtr)pOut, buffer.Length, out int got, IntPtr.Zero)
                ? got : -1;
        }
    }

    public static string? GetFileSystemName(string root)
    {
        var fs = new char[64];
        if (!GetVolumeInformationW(root, IntPtr.Zero, 0, out _, out _, out _, fs, fs.Length))
            return null;
        return new string(fs, 0, Array.IndexOf(fs, '\0') is var n and >= 0 ? n : fs.Length);
    }

    /// <summary>파일 크기와 수정한 날짜를 읽습니다. 실패하면 false.</summary>
    public static bool TryGetFileMeta(string path, out long size, out DateTime modified)
    {
        string p = path.Length >= 248 && !path.StartsWith(@"\\?\") ? @"\\?\" + path : path;
        if (GetFileAttributesExW(p, 0, out var d))
        {
            size = (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? -1 : ((long)d.nFileSizeHigh << 32) | d.nFileSizeLow;
            modified = DateTime.FromFileTime(d.ftLastWriteTime);
            return true;
        }
        size = -1;
        modified = DateTime.MinValue;
        return false;
    }
}
