using System.Text;

namespace FastFind;

/// <summary>
/// 드라이브별 색인 만들기, 실시간 감시, 이름순 순위 갱신을 관리합니다.
/// </summary>
internal sealed class IndexManager : IDisposable
{
    private sealed class Slot
    {
        public required string Root;
        public int Number;
        public CancellationTokenSource? Monitor;
        public string State = "대기";
        public bool Building;
        public bool Selected;     // 사용자가 검색 대상으로 고름
        public bool Present;      // 지금 연결되어 있음 (USB를 뽑으면 false)
        public DriveType Type;
        public string Label = "";
    }

    /// <summary>드라이브 선택 메뉴에 보여 줄 정보.</summary>
    public sealed record DriveItem(string Root, string Label, DriveType Type, bool Selected, bool Present, string State);

    private HashSet<string>? _saved; // 저장된 선택 (없으면 고정 디스크 전체)

    private readonly List<Slot> _slots = new();
    private readonly object _gate = new();
    private readonly System.Threading.Timer _rankTimer;

    public SearchEngine Engine { get; } = new();

    /// <summary>색인 내용이 바뀜 (백그라운드 스레드에서 호출됨).</summary>
    public event Action? IndexChanged;

    /// <summary>색인 상태 문구가 바뀜 (백그라운드 스레드에서 호출됨).</summary>
    public event Action? StatusChanged;

    public IndexManager()
    {
        _rankTimer = new System.Threading.Timer(_ =>
        {
            try { Engine.RebuildRanks(); } catch { /* 다음 기회에 다시 */ }
        });
    }

    /// <summary>
    /// 연결된 드라이브를 찾아 선택된 것만 색인합니다.
    /// 처음 실행할 때는 고정 디스크(내장 HDD/SSD)를 모두 선택하고, 이후에는 저장된 선택을 따릅니다.
    /// </summary>
    public void Start()
    {
        _saved = DriveSettings.Load();
        RefreshDrives();
    }

    /// <summary>
    /// 드라이브 목록을 다시 확인합니다 (USB를 꽂거나 뺐을 때).
    /// 새로 연결된 드라이브가 선택 상태면 색인하고, 빠진 드라이브는 검색에서 뺍니다.
    /// </summary>
    public void RefreshDrives()
    {
        var found = new List<(string Root, DriveType Type, string Label)>();
        foreach (var d in DriveInfo.GetDrives())
        {
            try
            {
                if (d.DriveType != DriveType.Fixed && d.DriveType != DriveType.Removable) continue;
                if (!d.IsReady) continue;
                found.Add((d.Name, d.DriveType, MakeLabel(d)));
            }
            catch { /* 카드 리더 빈 슬롯 등 */ }
        }

        var toBuild = new List<Slot>();
        lock (_gate)
        {
            foreach (var (root, type, label) in found)
            {
                var slot = _slots.Find(x => string.Equals(x.Root, root, StringComparison.OrdinalIgnoreCase));
                if (slot == null)
                {
                    slot = new Slot
                    {
                        Root = root,
                        Number = Engine.Reserve(),
                        Selected = _saved != null ? _saved.Contains(root.ToUpperInvariant()) : type == DriveType.Fixed,
                        State = "선택 안 함",
                    };
                    _slots.Add(slot);
                }
                bool cameBack = !slot.Present;
                slot.Present = true;
                slot.Type = type;
                slot.Label = label;
                if (slot.Selected && (cameBack || Engine.Volumes[slot.Number] == null)) toBuild.Add(slot);
            }

            foreach (var slot in _slots)
            {
                if (!slot.Present || found.Exists(f => string.Equals(f.Root, slot.Root, StringComparison.OrdinalIgnoreCase)))
                    continue;
                // 연결이 끊긴 드라이브: 색인을 버리고 감시를 멈춤
                slot.Present = false;
                slot.Monitor?.Cancel();
                slot.Monitor = null;
                slot.State = "연결 안 됨";
                Engine.Set(slot.Number, null);
            }
            _slots.Sort((a, b) => string.Compare(a.Root, b.Root, StringComparison.OrdinalIgnoreCase));
        }

        foreach (var s in toBuild) Build(s);
        IndexChanged?.Invoke();
        StatusChanged?.Invoke();
    }

    private static string MakeLabel(DriveInfo d)
    {
        string name = "";
        try { name = d.VolumeLabel; } catch { }
        if (string.IsNullOrWhiteSpace(name)) name = d.DriveType == DriveType.Removable ? "이동식 디스크" : "로컬 디스크";
        string extra = "";
        try { extra = $"{d.DriveFormat}, {FormatSize(d.TotalSize)}"; } catch { }
        return extra.Length > 0 ? $"{name} ({extra})" : name;
    }

    private static string FormatSize(long bytes)
    {
        double gb = bytes / (1024.0 * 1024 * 1024);
        return gb >= 1000 ? $"{gb / 1024:0.#} TB" : $"{gb:0} GB";
    }

    public IReadOnlyList<DriveItem> GetDrives()
    {
        lock (_gate)
            return _slots.Where(s => s.Present)
                .Select(s => new DriveItem(s.Root, s.Label, s.Type, s.Selected, s.Present, s.State)).ToList();
    }

    /// <summary>드라이브를 검색 대상에 넣거나 뺍니다. 처음 넣는 드라이브는 색인을 시작합니다.</summary>
    public void SetSelected(string root, bool selected) => SetSelected(new[] { root }, selected);

    public void SetSelected(IEnumerable<string> roots, bool selected)
    {
        var toBuild = new List<Slot>();
        lock (_gate)
        {
            foreach (string root in roots)
            {
                var slot = _slots.Find(x => string.Equals(x.Root, root, StringComparison.OrdinalIgnoreCase));
                if (slot == null || slot.Selected == selected) continue;
                slot.Selected = selected;
                if (selected && slot.Present && Engine.Volumes[slot.Number] == null) toBuild.Add(slot);
            }
            // 빠져 있는 USB의 선택 상태도 그대로 기억해 둡니다
            _saved = _slots.Where(x => x.Selected).Select(x => x.Root.ToUpperInvariant()).ToHashSet();
            DriveSettings.Save(_saved);
        }
        foreach (var s in toBuild) Build(s);
        IndexChanged?.Invoke();
        StatusChanged?.Invoke();
    }

    /// <summary>드라이브 루트("C:\")의 번호. 목록에 없으면 -1.</summary>
    public int SlotOf(string root)
    {
        lock (_gate)
            return _slots.Find(x => string.Equals(x.Root, root, StringComparison.OrdinalIgnoreCase))?.Number ?? -1;
    }

    public bool IsSelected(string root)
    {
        lock (_gate)
            return _slots.Find(x => string.Equals(x.Root, root, StringComparison.OrdinalIgnoreCase))?.Selected ?? false;
    }

    /// <summary>검색할 드라이브 번호 표 (SearchEngine.Search에 넘김).</summary>
    public bool[] GetSearchFilter()
    {
        lock (_gate)
        {
            var include = new bool[Engine.Volumes.Length];
            foreach (var s in _slots)
                if (s.Selected && s.Present && s.Number < include.Length) include[s.Number] = true;
            return include;
        }
    }

    /// <summary>모든 드라이브를 처음부터 다시 색인합니다. (색인이 끝날 때까지 기존 색인으로 검색 가능)</summary>
    public void RebuildAll()
    {
        List<Slot> slots;
        lock (_gate) slots = _slots.Where(s => s.Selected && s.Present).ToList();
        foreach (var s in slots) Build(s);
    }

    private void Build(Slot slot)
    {
        lock (_gate)
        {
            if (slot.Building) return;
            slot.Building = true;
            slot.State = "색인 중…";
        }
        StatusChanged?.Invoke();

        Task.Run(() =>
        {
            VolumeIndex? v = null;
            NtfsIndexer.JournalState journal = default;
            string? ntfsError = null;
            try
            {
                string? fs = NativeMethods.GetFileSystemName(slot.Root);
                if (string.Equals(fs, "NTFS", StringComparison.OrdinalIgnoreCase))
                    v = NtfsIndexer.TryBuild(slot.Root, out journal, out ntfsError);
                else
                    ntfsError = fs ?? "알 수 없는 파일 시스템";

                v ??= DirectoryScanner.Build(slot.Root, CancellationToken.None);
            }
            catch (Exception ex)
            {
                lock (_gate) { slot.Building = false; slot.State = "오류: " + ex.Message; }
                StatusChanged?.Invoke();
                return;
            }

            var oldMonitor = slot.Monitor;
            oldMonitor?.Cancel();
            slot.Monitor = null;
            lock (_gate)
            {
                if (!slot.Present)
                {
                    // 색인하는 동안 드라이브가 빠짐
                    slot.Building = false;
                    StatusChanged?.Invoke();
                    return;
                }
            }
            Engine.Set(slot.Number, v);

            if (v.Method == "MFT")
            {
                var cts = new CancellationTokenSource();
                slot.Monitor = cts;
                v.IsLive = true;
                NtfsIndexer.StartMonitor(v, journal, cts.Token,
                    changed: _ => OnVolumeChanged(),
                    lost: _ => Build(slot)); // 저널이 끊기면 다시 색인
            }

            lock (_gate)
            {
                slot.Building = false;
                slot.State = ntfsError != null && v.Method != "MFT" ? $"{v.Method} ({ntfsError})" : v.Method;
            }
            _rankTimer.Change(200, Timeout.Infinite);
            IndexChanged?.Invoke();
            StatusChanged?.Invoke();
        });
    }

    private void OnVolumeChanged()
    {
        // 바뀐 파일의 이름순 순위는 3초 뒤 한꺼번에 다시 계산 (그 전에는 문자열 비교로 정렬)
        _rankTimer.Change(3000, Timeout.Infinite);
        IndexChanged?.Invoke();
    }

    public string GetStatusText()
    {
        var vols = Engine.Volumes;
        var sb = new StringBuilder();
        lock (_gate)
        {
            foreach (var s in _slots)
            {
                if (!s.Selected || !s.Present) continue;
                if (sb.Length > 0) sb.Append("   ");
                sb.Append(s.Root.TrimEnd('\\')).Append(' ');
                var v = s.Number < vols.Length ? vols[s.Number] : null;
                if (v != null)
                {
                    sb.Append($"{v.LiveCount:N0}개 · {s.State} {v.BuildTime.TotalSeconds:0.0}초");
                    if (v.IsLive) sb.Append(" · 실시간");
                    if (s.Building) sb.Append(" · 다시 색인 중…");
                }
                else sb.Append(s.State);
            }
        }
        return sb.Length == 0 ? "선택한 드라이브가 없습니다 (드라이브 버튼에서 고르세요)" : sb.ToString();
    }

    public void Dispose()
    {
        lock (_gate)
            foreach (var s in _slots) s.Monitor?.Cancel();
        _rankTimer.Dispose();
    }
}

/// <summary>선택한 드라이브 목록을 %LocalAppData%\FastFind\drives.txt 에 저장합니다.</summary>
internal static class DriveSettings
{
    private static string FilePath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "FastFind", "drives.txt");

    /// <summary>저장된 선택. 한 번도 저장한 적이 없으면 null.</summary>
    public static HashSet<string>? Load()
    {
        try
        {
            if (!File.Exists(FilePath)) return null;
            return File.ReadAllLines(FilePath).Select(l => l.Trim().ToUpperInvariant())
                .Where(l => l.Length > 0).ToHashSet();
        }
        catch { return null; }
    }

    public static void Save(IEnumerable<string> roots)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(FilePath)!);
            File.WriteAllLines(FilePath, roots.OrderBy(r => r));
        }
        catch { /* 저장 실패는 무시 (다음 실행 때 기본값) */ }
    }
}
