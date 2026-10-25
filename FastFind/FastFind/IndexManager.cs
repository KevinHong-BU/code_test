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
    }

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

    /// <summary>고정 디스크(내장 HDD/SSD)를 모두 색인합니다.</summary>
    public void Start()
    {
        foreach (var d in DriveInfo.GetDrives())
        {
            try
            {
                if (d.DriveType != DriveType.Fixed || !d.IsReady) continue;
            }
            catch { continue; }
            var slot = new Slot { Root = d.Name, Number = Engine.Reserve() };
            lock (_gate) _slots.Add(slot);
            Build(slot);
        }
        StatusChanged?.Invoke();
    }

    /// <summary>모든 드라이브를 처음부터 다시 색인합니다. (색인이 끝날 때까지 기존 색인으로 검색 가능)</summary>
    public void RebuildAll()
    {
        List<Slot> slots;
        lock (_gate) slots = _slots.ToList();
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
        return sb.Length == 0 ? "색인할 드라이브가 없습니다" : sb.ToString();
    }

    public void Dispose()
    {
        lock (_gate)
            foreach (var s in _slots) s.Monitor?.Cancel();
        _rankTimer.Dispose();
    }
}
