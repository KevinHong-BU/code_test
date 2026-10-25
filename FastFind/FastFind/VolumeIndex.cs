using System.Collections.Concurrent;

namespace FastFind;

[Flags]
internal enum EntryFlags : ushort
{
    None = 0,
    Directory = 1,
    Deleted = 2,
    Hidden = 4,
    System = 8,
}

/// <summary>파일/폴더 하나. 16바이트로 작게 유지해서 수백만 개도 캐시에 잘 올라가게 합니다.</summary>
internal struct Entry
{
    public int NameStart;   // 이름 버퍼 안에서 이름이 시작하는 위치
    public int Parent;      // 부모 폴더의 항목 번호, -1 = 드라이브 루트
    public ushort NameLen;
    public EntryFlags Flags;
    public int Reserved;

    public readonly bool IsDirectory => (Flags & EntryFlags.Directory) != 0;
    public readonly bool IsDeleted => (Flags & EntryFlags.Deleted) != 0;
}

/// <summary>
/// 드라이브 하나의 파일 이름 색인.
///
/// 핵심 아이디어: 모든 이름을 큰 char 배열 하나에 이어 붙여 둡니다.
///   [이름][\0][번호 하위 15비트][번호 상위 15비트] [이름][\0]...
/// 검색할 때는 항목을 하나씩 비교하지 않고, 대문자로 바꾼 이름 버퍼 전체를
/// SIMD로 벡터화된 IndexOf 한 번으로 훑습니다. 일치한 위치 바로 뒤의 \0 다음 두 글자에
/// 항목 번호가 들어 있어서, 어떤 파일이 일치했는지 곧바로 알 수 있습니다.
/// 검색어에는 \0이 없으므로 일치 결과가 두 이름에 걸칠 수 없고,
/// 번호 글자는 항상 0x8000 비트가 켜져 있어 \0과 헷갈리지 않습니다.
/// </summary>
internal sealed class VolumeIndex
{
    public const int RecordOverhead = 3; // \0 + 번호 두 글자

    public string Root { get; }          // "C:\"
    public string RootFolded { get; }
    public string Method { get; set; } = "";
    public bool IsLive { get; set; }     // USN 저널로 실시간 갱신 중인지
    public TimeSpan BuildTime { get; set; }

    /// <summary>읽기(검색, 화면 표시)는 동시에 여러 개, 쓰기(변경 반영)는 혼자.</summary>
    public readonly ReaderWriterLockSlim Lock = new(LockRecursionPolicy.NoRecursion);

    internal Entry[] Entries;
    internal int Count;
    internal char[] Names;     // 원래 이름
    internal char[] Folded;    // 대문자로 바꾼 이름 (대소문자 무시 검색용)
    internal int NamesLength;
    private int _garbage;      // 이름 변경/삭제로 버려진 글자 수

    /// <summary>이름순 정렬 순위 (SearchEngine이 계산). -1 = 아직 모름.</summary>
    internal volatile int[] Rank = Array.Empty<int>();

    // NTFS 파일 참조 번호 → 항목 번호 (실시간 갱신용)
    private Dictionary<ulong, int>? _frnMap;
    private ulong[] _frn = Array.Empty<ulong>();
    public ulong RootFrn { get; set; } = ulong.MaxValue;

    private readonly ConcurrentDictionary<int, string> _dirPath = new();
    private readonly ConcurrentDictionary<int, string> _dirPathFolded = new();

    /// <summary>내용이 바뀔 때마다 증가합니다.</summary>
    public long Version;

    public VolumeIndex(string root, int capacity = 1 << 16)
    {
        Root = root;
        RootFolded = root.ToUpperInvariant();
        capacity = Math.Max(capacity, 1024);
        Entries = new Entry[capacity];
        Names = new char[capacity * 12];   // 이름 평균 20자 안팎 → 부족하면 자동으로 늘어남
        Folded = new char[capacity * 12];
    }

    public int LiveCount
    {
        get
        {
            int n = 0;
            for (int i = 0; i < Count; i++) if (!Entries[i].IsDeleted) n++;
            return n;
        }
    }

    public static char Fold(char c) => char.ToUpperInvariant(c);

    public static string Fold(string s)
    {
        return string.Create(s.Length, s, (dst, src) =>
        {
            for (int i = 0; i < src.Length; i++) dst[i] = char.ToUpperInvariant(src[i]);
        });
    }

    // ───────────────────────── 읽기 ─────────────────────────

    public ReadOnlySpan<char> GetName(int i)
    {
        ref readonly Entry e = ref Entries[i];
        return Names.AsSpan(e.NameStart, e.NameLen);
    }

    public ReadOnlySpan<char> GetFoldedName(int i)
    {
        ref readonly Entry e = ref Entries[i];
        return Folded.AsSpan(e.NameStart, e.NameLen);
    }

    /// <summary>폴더의 전체 경로 (끝에 \ 포함). dir = -1이면 드라이브 루트.</summary>
    public string GetDirectoryPath(int dir) => GetDirPath(dir, false, 0);

    public string GetFoldedDirectoryPath(int dir) => GetDirPath(dir, true, 0);

    private string GetDirPath(int dir, bool folded, int depth)
    {
        if (dir < 0 || depth > 1024) return folded ? RootFolded : Root;
        var cache = folded ? _dirPathFolded : _dirPath;
        if (cache.TryGetValue(dir, out var s)) return s;
        ref readonly Entry e = ref Entries[dir];
        var buf = folded ? Folded : Names;
        s = string.Concat(GetDirPath(e.Parent, folded, depth + 1), buf.AsSpan(e.NameStart, e.NameLen), "\\");
        cache[dir] = s;
        return s;
    }

    /// <summary>항목이 들어 있는 폴더 경로 (화면 표시용, 끝의 \ 없음. 루트는 "C:\").</summary>
    public string GetContainingFolder(int i)
    {
        string p = GetDirectoryPath(Entries[i].Parent);
        return p.Length > 3 ? p[..^1] : p;
    }

    public string GetFullPath(int i) => string.Concat(GetDirectoryPath(Entries[i].Parent), GetName(i));

    /// <summary>전체 경로를 dst에 써 넣고 길이를 돌려줍니다. 버퍼가 작으면 -1.</summary>
    public int WriteFullPath(int i, Span<char> dst, bool folded)
    {
        string dir = folded ? GetFoldedDirectoryPath(Entries[i].Parent) : GetDirectoryPath(Entries[i].Parent);
        var name = folded ? GetFoldedName(i) : GetName(i);
        int len = dir.Length + name.Length;
        if (len > dst.Length) return -1;
        dir.AsSpan().CopyTo(dst);
        name.CopyTo(dst[dir.Length..]);
        return len;
    }

    // ───────────────────────── 만들기 (쓰기 잠금 안에서 호출) ─────────────────────────

    public int Add(ReadOnlySpan<char> name, int parent, uint attributes)
    {
        if (Count == Entries.Length) Array.Resize(ref Entries, Entries.Length * 2);
        int idx = Count++;
        ref Entry e = ref Entries[idx];
        e.Parent = parent;
        e.Flags = ToFlags(attributes);
        e.NameLen = (ushort)Math.Min(name.Length, ushort.MaxValue);
        e.NameStart = AppendName(name[..e.NameLen], idx);
        return idx;
    }

    private static EntryFlags ToFlags(uint attributes)
    {
        var f = EntryFlags.None;
        if ((attributes & NativeMethods.FILE_ATTRIBUTE_DIRECTORY) != 0) f |= EntryFlags.Directory;
        if ((attributes & NativeMethods.FILE_ATTRIBUTE_HIDDEN) != 0) f |= EntryFlags.Hidden;
        if ((attributes & NativeMethods.FILE_ATTRIBUTE_SYSTEM) != 0) f |= EntryFlags.System;
        return f;
    }

    private int AppendName(ReadOnlySpan<char> name, int idx)
    {
        int need = NamesLength + name.Length + RecordOverhead;
        if (need > Names.Length)
        {
            int size = Math.Max(need, Names.Length * 2);
            Array.Resize(ref Names, size);
            Array.Resize(ref Folded, size);
        }
        int start = NamesLength;
        var f = Folded.AsSpan(start, name.Length);
        for (int k = 0; k < name.Length; k++)
        {
            char c = name[k];
            if (c == '\0') c = ' ';  // 혹시 모를 \0은 구분자와 겹치지 않게
            Names[start + k] = c;
            f[k] = Fold(c);
        }
        int p = start + name.Length;
        char lo = (char)(0x8000 | (idx & 0x7FFF));
        char hi = (char)(0x8000 | ((idx >> 15) & 0x7FFF));
        Names[p] = '\0'; Folded[p] = '\0';
        Names[p + 1] = lo; Folded[p + 1] = lo;
        Names[p + 2] = hi; Folded[p + 2] = hi;
        NamesLength = p + RecordOverhead;
        return start;
    }

    /// <summary>
    /// 이름 버퍼의 \0 위치(z)에서 항목 번호를 읽습니다.
    /// 이름 변경으로 버려진 옛 기록이면 -1.
    /// </summary>
    public int OwnerAtTerminator(char[] buf, int z)
    {
        int idx = (buf[z + 1] & 0x7FFF) | ((buf[z + 2] & 0x7FFF) << 15);
        if ((uint)idx >= (uint)Count) return -1;
        ref readonly Entry e = ref Entries[idx];
        return e.NameStart + e.NameLen == z ? idx : -1;
    }

    // ───────────────────────── NTFS 참조 번호 ─────────────────────────

    public void EnableFrnTracking(int capacity)
    {
        _frnMap = new Dictionary<ulong, int>(capacity);
        _frn = new ulong[Math.Max(capacity, Entries.Length)];
    }

    public void SetFrn(int idx, ulong frn)
    {
        if (idx >= _frn.Length) Array.Resize(ref _frn, Math.Max(idx + 1, _frn.Length * 2));
        _frn[idx] = frn;
        _frnMap![frn] = idx;
    }

    public bool TryGetIndex(ulong frn, out int idx) => _frnMap!.TryGetValue(frn, out idx);

    public void ForgetFrn(ulong frn) => _frnMap?.Remove(frn);

    public void MarkDeleted(int idx)
    {
        Entries[idx].Flags |= EntryFlags.Deleted;
    }

    // ───────────────────────── 실시간 변경 (쓰기 잠금 안에서 호출) ─────────────────────────

    /// <summary>새로 만들어지거나 이름이 바뀌거나 옮겨진 파일을 반영합니다.</summary>
    public bool Upsert(ulong frn, ulong parentFrn, ReadOnlySpan<char> name, uint attributes)
    {
        if (_frnMap == null) return false;
        int parent;
        if (parentFrn == RootFrn) parent = -1;
        else if (!_frnMap.TryGetValue(parentFrn, out parent)) return false; // 모르는 폴더(시스템 영역 등)

        if (_frnMap.TryGetValue(frn, out int idx))
        {
            ref Entry e = ref Entries[idx];
            bool nameSame = name.SequenceEqual(Names.AsSpan(e.NameStart, e.NameLen));
            var flags = ToFlags(attributes) | (e.Flags & EntryFlags.Deleted);
            if (nameSame && e.Parent == parent && e.Flags == flags) return false;

            if (e.IsDirectory && (!nameSame || e.Parent != parent)) ClearPathCache();
            e.Flags = flags;
            e.Parent = parent;
            if (!nameSame)
            {
                _garbage += e.NameLen + RecordOverhead;
                e.NameLen = (ushort)Math.Min(name.Length, ushort.MaxValue);
                e.NameStart = AppendName(name[..e.NameLen], idx);
                InvalidateRank(idx);
            }
        }
        else
        {
            idx = Add(name, parent, attributes);
            SetFrn(idx, frn);
        }
        Version++;
        return true;
    }

    public bool Remove(ulong frn)
    {
        if (_frnMap == null || !_frnMap.Remove(frn, out int idx)) return false;
        ref Entry e = ref Entries[idx];
        e.Flags |= EntryFlags.Deleted;
        _garbage += e.NameLen + RecordOverhead;
        if (e.IsDirectory) ClearPathCache();
        Version++;
        return true;
    }

    private void InvalidateRank(int idx)
    {
        var r = Rank;
        if (idx < r.Length) r[idx] = -1;
    }

    public void ClearPathCache()
    {
        _dirPath.Clear();
        _dirPathFolded.Clear();
    }

    /// <summary>버려진 글자가 많아지면 이름 버퍼를 새로 채워 메모리를 돌려받습니다.</summary>
    public void CompactIfNeeded()
    {
        if (_garbage < 1_000_000 || _garbage < NamesLength / 3) return;
        var oldNames = Names;
        Names = new char[Math.Max(1024, NamesLength - _garbage + 4096)];
        Folded = new char[Names.Length];
        NamesLength = 0;
        for (int i = 0; i < Count; i++)
        {
            ref Entry e = ref Entries[i];
            if (e.IsDeleted)
            {
                // 삭제된 항목은 이름을 비워 둡니다 (번호는 그대로 유지)
                e.NameStart = AppendName(ReadOnlySpan<char>.Empty, i);
                e.NameLen = 0;
                continue;
            }
            e.NameStart = AppendName(oldNames.AsSpan(e.NameStart, e.NameLen), i);
        }
        _garbage = 0;
        ClearPathCache();
        Version++;
    }

    // ───────────────────────── 부모 연결 정리 ─────────────────────────

    /// <summary>
    /// MFT 열거 직후: 부모 참조 번호를 항목 번호로 바꾸고,
    /// 루트까지 이어지지 않는 항목(시스템 메타 파일 등)은 지웁니다.
    /// </summary>
    public void ResolveParents(ulong[] parentFrn)
    {
        for (int i = 0; i < Count; i++)
        {
            ulong pf = parentFrn[i];
            if (pf == RootFrn) Entries[i].Parent = -1;
            else if (_frnMap!.TryGetValue(pf, out int p) && p != i) Entries[i].Parent = p;
            else Entries[i].Parent = -2; // 끊어짐
        }

        // 0 = 미확인, 1 = 정상, 2 = 끊어짐
        var state = new byte[Count];
        var stack = new List<int>(64);
        for (int i = 0; i < Count; i++)
        {
            if (state[i] != 0) continue;
            stack.Clear();
            int cur = i;
            byte result;
            while (true)
            {
                if (cur == -1) { result = 1; break; }
                if (cur == -2) { result = 2; break; }
                if (state[cur] != 0) { result = state[cur]; break; }
                if (stack.Count > 4096) { result = 2; break; } // 순환 방지
                stack.Add(cur);
                state[cur] = 3; // 방문 중
                cur = Entries[cur].Parent;
                if (cur >= 0 && state[cur] == 3) { result = 2; break; }
            }
            foreach (int k in stack) state[k] = result;
        }

        for (int i = 0; i < Count; i++)
        {
            if (state[i] == 2)
            {
                Entries[i].Flags |= EntryFlags.Deleted;
                Entries[i].Parent = -1;
                _frnMap!.Remove(_frn[i]);
            }
        }
    }
}
