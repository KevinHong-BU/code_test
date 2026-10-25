using System.Buffers.Binary;
using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
using static FastFind.NativeMethods;

namespace FastFind;

/// <summary>
/// NTFS 드라이브를 MFT(마스터 파일 테이블)에서 직접 읽어 색인합니다.
/// 폴더를 하나하나 여는 대신 FSCTL_ENUM_USN_DATA로 MFT 기록을 큰 덩어리로 받아오므로
/// 파일 백만 개 정도가 1~3초 안에 읽힙니다. (관리자 권한 필요)
/// 색인을 만든 뒤에는 USN 변경 저널을 따라가며 생성/삭제/이름 변경을 실시간으로 반영합니다.
/// </summary>
internal sealed class NtfsIndexer
{
    private const int BufferSize = 1 << 20; // 1MB씩 받아옴

    public static VolumeIndex? TryBuild(string root, out JournalState journal, out string? error)
    {
        journal = default;
        error = null;
        var sw = Stopwatch.StartNew();
        using var vol = OpenVolume(root);
        if (vol.IsInvalid)
        {
            int err = Marshal.GetLastWin32Error();
            error = err == ERROR_ACCESS_DENIED ? "관리자 권한 필요" : $"볼륨 열기 실패 ({err})";
            return null;
        }

        if (!QueryOrCreateJournal(vol, out var jd, out error)) return null;

        ulong rootFrn = GetRootFrn(root);
        if (rootFrn == ulong.MaxValue) { error = "루트 폴더 정보를 읽지 못함"; return null; }

        // MFT 크기로 파일 수를 추정해서 배열을 미리 잡아 둡니다 (재할당 최소화)
        int capacity = 1 << 16;
        if (IoctlOut<NTFS_VOLUME_DATA_BUFFER>(vol, FSCTL_GET_NTFS_VOLUME_DATA, out var nv) && nv.BytesPerFileRecordSegment > 0)
            capacity = (int)Math.Clamp(nv.MftValidDataLength / nv.BytesPerFileRecordSegment, 1 << 16, 1 << 27);

        var index = new VolumeIndex(root, capacity) { RootFrn = rootFrn };
        index.EnableFrnTracking(capacity);
        var parents = new ulong[capacity];

        var med = new MFT_ENUM_DATA_V0 { StartFileReferenceNumber = 0, LowUsn = 0, HighUsn = long.MaxValue };
        var buffer = new byte[BufferSize];
        while (true)
        {
            int got = IoctlBuffer(vol, FSCTL_ENUM_USN_DATA, ref med, buffer);
            if (got < 0)
            {
                int err = Marshal.GetLastWin32Error();
                if (err == ERROR_HANDLE_EOF) break;
                error = $"MFT 읽기 실패 ({err})";
                return null;
            }
            if (got <= 8) break;

            var span = buffer.AsSpan(0, got);
            med.StartFileReferenceNumber = BinaryPrimitives.ReadUInt64LittleEndian(span);
            int off = 8;
            while (off + 60 <= got)
            {
                var rec = span[off..];
                int len = (int)BinaryPrimitives.ReadUInt32LittleEndian(rec);
                if (len <= 0) break;
                if (BinaryPrimitives.ReadUInt16LittleEndian(rec[4..]) == 2)
                {
                    ParseV2(rec, out ulong frn, out ulong parentFrn, out uint attrs, out _, out var name);
                    int idx = index.Add(name, -2, attrs);
                    index.SetFrn(idx, frn);
                    if (idx >= parents.Length) Array.Resize(ref parents, parents.Length * 2);
                    parents[idx] = parentFrn;
                }
                off += len;
            }
        }

        index.ResolveParents(parents);
        index.Method = "MFT";
        index.BuildTime = sw.Elapsed;
        journal = new JournalState(jd.UsnJournalID, jd.NextUsn);
        return index;
    }

    private static void ParseV2(ReadOnlySpan<byte> rec, out ulong frn, out ulong parentFrn, out uint attrs,
        out uint reason, out ReadOnlySpan<char> name)
    {
        frn = BinaryPrimitives.ReadUInt64LittleEndian(rec[8..]);
        parentFrn = BinaryPrimitives.ReadUInt64LittleEndian(rec[16..]);
        reason = BinaryPrimitives.ReadUInt32LittleEndian(rec[40..]);
        attrs = BinaryPrimitives.ReadUInt32LittleEndian(rec[52..]);
        int nameLen = BinaryPrimitives.ReadUInt16LittleEndian(rec[56..]);
        int nameOff = BinaryPrimitives.ReadUInt16LittleEndian(rec[58..]);
        name = MemoryMarshal.Cast<byte, char>(rec.Slice(nameOff, nameLen));
    }

    private static SafeFileHandle OpenVolume(string root) =>
        CreateFileW(@"\\.\" + root.TrimEnd('\\'), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero);

    private static bool QueryOrCreateJournal(SafeFileHandle vol, out USN_JOURNAL_DATA_V0 jd, out string? error)
    {
        error = null;
        if (IoctlOut(vol, FSCTL_QUERY_USN_JOURNAL, out jd)) return true;
        int err = Marshal.GetLastWin32Error();
        if (err == ERROR_JOURNAL_NOT_ACTIVE || err == ERROR_JOURNAL_DELETE_IN_PROGRESS)
        {
            // 저널이 꺼져 있으면 켭니다 (Windows가 기본으로 쓰는 크기와 비슷하게)
            var create = new CREATE_USN_JOURNAL_DATA { MaximumSize = 32UL << 20, AllocationDelta = 8UL << 20 };
            Ioctl<CREATE_USN_JOURNAL_DATA, long>(vol, FSCTL_CREATE_USN_JOURNAL, ref create, out _);
            if (IoctlOut(vol, FSCTL_QUERY_USN_JOURNAL, out jd)) return true;
            err = Marshal.GetLastWin32Error();
        }
        error = $"USN 저널을 사용할 수 없음 ({err})";
        return false;
    }

    private static ulong GetRootFrn(string root)
    {
        using var h = CreateFileW(root, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            IntPtr.Zero, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, IntPtr.Zero);
        if (h.IsInvalid || !GetFileInformationByHandle(h, out var info)) return ulong.MaxValue;
        return ((ulong)info.FileIndexHigh << 32) | info.FileIndexLow;
    }

    // ───────────────────────── 실시간 변경 감지 ─────────────────────────

    internal readonly record struct JournalState(ulong JournalId, long NextUsn);

    /// <summary>
    /// USN 저널을 읽는 백그라운드 스레드를 시작합니다.
    /// changed: 색인이 바뀌었을 때, lost: 저널이 끊겨 다시 색인해야 할 때 호출됩니다.
    /// </summary>
    public static void StartMonitor(VolumeIndex index, JournalState journal, CancellationToken ct,
        Action<VolumeIndex> changed, Action<VolumeIndex> lost)
    {
        var t = new Thread(() => MonitorLoop(index, journal, ct, changed, lost))
        {
            IsBackground = true,
            Name = "USN " + index.Root,
            Priority = ThreadPriority.BelowNormal,
        };
        t.Start();
    }

    private static void MonitorLoop(VolumeIndex index, JournalState journal, CancellationToken ct,
        Action<VolumeIndex> changed, Action<VolumeIndex> lost)
    {
        using var vol = OpenVolume(index.Root);
        if (vol.IsInvalid) { index.IsLive = false; return; }

        var rd = new READ_USN_JOURNAL_DATA_V0
        {
            StartUsn = journal.NextUsn,
            ReasonMask = 0xFFFFFFFF,
            ReturnOnlyOnClose = 0,
            Timeout = 0,
            BytesToWaitFor = 1,      // 새 기록이 생길 때까지 기다림 (CPU 사용 없음)
            UsnJournalID = journal.JournalId,
        };
        var buffer = new byte[256 * 1024];

        while (!ct.IsCancellationRequested)
        {
            int got = IoctlBuffer(vol, FSCTL_READ_USN_JOURNAL, ref rd, buffer);
            if (ct.IsCancellationRequested) break;
            if (got < 0)
            {
                int err = Marshal.GetLastWin32Error();
                index.IsLive = false;
                if (err == ERROR_JOURNAL_ENTRY_DELETED || err == ERROR_JOURNAL_DELETE_IN_PROGRESS ||
                    err == ERROR_JOURNAL_NOT_ACTIVE || err == ERROR_INVALID_PARAMETER)
                    lost(index);
                return;
            }
            if (got < 8) continue;
            rd.StartUsn = BinaryPrimitives.ReadInt64LittleEndian(buffer);
            if (got == 8) continue;

            bool any = false;
            index.Lock.EnterWriteLock();
            try
            {
                any = ApplyRecords(index, buffer.AsSpan(0, got));
                if (any) index.CompactIfNeeded();
            }
            finally { index.Lock.ExitWriteLock(); }
            if (any) changed(index);
        }
    }

    private static bool ApplyRecords(VolumeIndex index, ReadOnlySpan<byte> span)
    {
        bool any = false;
        int off = 8;
        while (off + 60 <= span.Length)
        {
            var rec = span[off..];
            int len = (int)BinaryPrimitives.ReadUInt32LittleEndian(rec);
            if (len <= 0) break;
            if (BinaryPrimitives.ReadUInt16LittleEndian(rec[4..]) == 2)
            {
                ParseV2(rec, out ulong frn, out ulong parentFrn, out uint attrs, out uint reason, out var name);
                if ((reason & USN_REASON_FILE_DELETE) != 0)
                    any |= index.Remove(frn);
                else if ((reason & USN_REASON_RENAME_OLD_NAME) != 0)
                { /* 새 이름 기록에서 처리 */ }
                else if ((reason & (USN_REASON_FILE_CREATE | USN_REASON_RENAME_NEW_NAME | USN_REASON_BASIC_INFO_CHANGE)) != 0)
                    any |= index.Upsert(frn, parentFrn, name, attrs);
            }
            off += len;
        }
        return any;
    }
}
