using System.Buffers;
using System.Diagnostics;

namespace FastFind;

internal enum SortColumn { Name, Path, Size, Modified, Extension }

/// <param name="FoldersFirst">폴더 내용을 볼 때처럼 폴더를 파일보다 앞에 둠</param>
internal readonly record struct SortSpec(SortColumn Column, bool Descending, bool FoldersFirst = false);

internal sealed class SearchResult
{
    public static readonly SearchResult Empty = new(Array.Empty<long>(), TimeSpan.Zero, null, Array.Empty<VolumeIndex?>());

    /// <summary>(드라이브 번호 &lt;&lt; 32) | 항목 번호</summary>
    public long[] Items { get; }
    public TimeSpan Elapsed { get; }
    public string? Note { get; }
    /// <summary>검색할 때 쓴 드라이브 목록 (다시 색인해도 이 결과는 옛 색인을 계속 가리킴)</summary>
    public VolumeIndex?[] Volumes { get; }

    public SearchResult(long[] items, TimeSpan elapsed, string? note, VolumeIndex?[] volumes)
    {
        Volumes = volumes;
        Items = items;
        Elapsed = elapsed;
        Note = note;
    }

    public static int Volume(long key) => (int)(key >> 32);
    public static int Index(long key) => (int)key;
    public static long Key(int vol, int idx) => ((long)vol << 32) | (uint)idx;
}

/// <summary>
/// 여러 드라이브의 색인을 들고 있고, 검색과 정렬을 합니다.
/// </summary>
internal sealed class SearchEngine
{
    /// <summary>크기/날짜 정렬은 파일마다 정보를 읽어야 하므로 이 개수까지만 합니다.</summary>
    public const int MetaSortLimit = 200_000;

    private VolumeIndex?[] _volumes = Array.Empty<VolumeIndex?>();
    private readonly object _gate = new();
    private int _rankBusy;

    // 전체 항목을 이름순으로 늘어놓은 배열과, 그것을 만들 때의 드라이브 목록
    private volatile RankOrder? _order;
    private sealed record RankOrder(long[] Keys, VolumeIndex?[] Volumes, int[]?[] Ranks);

    public VolumeIndex?[] Volumes => _volumes;

    /// <summary>드라이브 자리를 하나 잡고 번호를 돌려줍니다.</summary>
    public int Reserve()
    {
        lock (_gate)
        {
            var arr = _volumes;
            Array.Resize(ref arr, arr.Length + 1);
            _volumes = arr;
            return arr.Length - 1;
        }
    }

    public void Set(int slot, VolumeIndex? v)
    {
        lock (_gate)
        {
            var arr = (VolumeIndex?[])_volumes.Clone();
            arr[slot] = v;
            _volumes = arr;
        }
    }

    // ───────────────────────── 검색 ─────────────────────────

    /// <param name="include">드라이브 번호별 검색 여부. null이면 모든 드라이브.</param>
    public SearchResult Search(Query q, SortSpec sort, CancellationToken ct, bool[]? include = null)
    {
        var sw = Stopwatch.StartNew();
        var vols = _volumes;
        bool Included(int vi) => include == null || (vi < include.Length && include[vi]);
        var held = new List<VolumeIndex>(vols.Length);
        long[] items;
        string? note = null;
        string[]? metaPaths = null;
        var col = sort.Column;
        try
        {
            for (int vi = 0; vi < vols.Length; vi++)
            {
                var v = vols[vi];
                if (v == null || !Included(vi)) continue;
                v.Lock.EnterReadLock();
                held.Add(v);
            }

            var parts = new HitBuffer[vols.Length][];
            try
            {
                Parallel.For(0, vols.Length, new ParallelOptions { CancellationToken = ct }, vi =>
                {
                    var v = vols[vi];
                    parts[vi] = v == null || !Included(vi) ? Array.Empty<HitBuffer>() : SearchVolume(v, vi, q, ct);
                });
                ct.ThrowIfCancellationRequested();

                // 결과 배열은 정확한 크기로 한 번만 만듭니다
                int total = 0;
                foreach (var p in parts) foreach (var h in p) total += h.Count;
                items = new long[total];
                int pos = 0;
                foreach (var p in parts)
                    foreach (var h in p)
                    {
                        Array.Copy(h.Items, 0, items, pos, h.Count);
                        pos += h.Count;
                    }
            }
            finally
            {
                foreach (var p in parts)
                    if (p != null) foreach (var h in p) h?.Release();
            }

            if ((col == SortColumn.Size || col == SortColumn.Modified) && items.Length > MetaSortLimit)
            {
                note = $"결과가 {MetaSortLimit:N0}개를 넘어 이름순으로 정렬했습니다";
                col = SortColumn.Name;
            }
            switch (col)
            {
                case SortColumn.Name: SortByName(items, vols); break;
                case SortColumn.Path: SortByPath(items, vols, ct); break;
                case SortColumn.Extension: SortByExtension(items, vols); break;
                default:
                    // 크기/날짜: 경로만 잠금 안에서 만들고, 디스크에서 정보 읽기는 잠금 밖에서
                    SortByName(items, vols);
                    metaPaths = new string[items.Length];
                    var mp = metaPaths;
                    var it = items;
                    Parallel.For(0, it.Length, new ParallelOptions { CancellationToken = ct }, i =>
                    {
                        var v = vols[SearchResult.Volume(it[i])]!;
                        mp[i] = v.GetFullPath(SearchResult.Index(it[i]));
                    });
                    break;
            }
        }
        finally
        {
            foreach (var v in held) v.Lock.ExitReadLock();
        }

        if (metaPaths != null) SortByMeta(items, metaPaths, col == SortColumn.Size, ct);
        if (sort.Descending) Array.Reverse(items);
        if (sort.FoldersFirst) FoldersFirst(items, vols);
        return new SearchResult(items, sw.Elapsed, note, vols);
    }

    private static HitBuffer[] SearchVolume(VolumeIndex v, int vi, Query q, CancellationToken ct)
    {
        string? lit = q.Prefilter;
        // 한두 글자 검색어는 거의 모든 이름에 들어 있어서, 일치 위치를 하나씩 찾아 건너뛰는 것보다
        // 항목을 차례로 검사하는 편이 빠릅니다 (측정: 300만 개 중 167만 개 일치 시 약 2배)
        if (lit != null && lit.Length <= 2) lit = null;
        int chunks = Math.Max(1, Math.Min(Environment.ProcessorCount * 4, (lit != null ? v.NamesLength : v.Count) / 32768));
        var locals = new HitBuffer[chunks];

        if (lit != null)
        {
            // ① 이름 버퍼를 기록 경계에 맞춰 조각내고 조각마다 동시에 훑습니다
            var buf = v.Folded;
            int len = v.NamesLength;
            var bounds = new int[chunks + 1];
            bounds[chunks] = len;
            for (int c = 1; c < chunks; c++)
                bounds[c] = Math.Max(bounds[c - 1], AlignToRecord(buf, len, (int)((long)len * c / chunks)));

            Parallel.For(0, chunks, new ParallelOptions { CancellationToken = ct }, c =>
            {
                var list = new HitBuffer(256);
                locals[c] = list;
                ScanChunk(v, vi, q, lit, buf, bounds[c], bounds[c + 1], list, ct);
            });
        }
        else
        {
            // ② 고정 문자열이 없으면 (빈 검색어, 정규식, !제외만 있는 경우 등) 항목을 하나씩 검사
            int count = v.Count;
            Parallel.For(0, chunks, new ParallelOptions { CancellationToken = ct }, c =>
            {
                int from = (int)((long)count * c / chunks), to = (int)((long)count * (c + 1) / chunks);
                var list = new HitBuffer(q.IsEmpty ? to - from : 256);
                locals[c] = list;
                for (int i = from; i < to; i++)
                {
                    if ((i & 0x1FFF) == 0 && ct.IsCancellationRequested) return;
                    if (q.Matches(v, i)) list.Add(SearchResult.Key(vi, i));
                }
            });
        }

        return locals;
    }

    /// <summary>pos 이후 처음 시작하는 기록의 위치. (\0 뒤 두 글자는 항목 번호)</summary>
    private static int AlignToRecord(char[] buf, int len, int pos)
    {
        int from = Math.Max(0, pos - VolumeIndex.RecordOverhead);
        int z = buf.AsSpan(from, len - from).IndexOf('\0');
        return z < 0 ? len : Math.Min(len, from + z + VolumeIndex.RecordOverhead);
    }

    private static void ScanChunk(VolumeIndex v, int vi, Query q, string lit, char[] buf, int from, int to,
        HitBuffer into, CancellationToken ct)
    {
        var needle = lit.AsSpan();
        int pos = from;
        int checkCounter = 0;
        while (pos < to)
        {
            // SIMD로 벡터화된 부분 문자열 찾기 (.NET 8: 첫/끝 글자를 128~512비트 단위로 동시 비교)
            int hit = buf.AsSpan(pos, to - pos).IndexOf(needle);
            if (hit < 0) return;
            hit += pos;

            int z = buf.AsSpan(hit, to - hit).IndexOf('\0');
            if (z < 0) return;
            z += hit;
            int idx = v.OwnerAtTerminator(buf, z);
            if (idx >= 0 && q.Matches(v, idx)) into.Add(SearchResult.Key(vi, idx));
            pos = z + VolumeIndex.RecordOverhead; // 같은 이름 안의 다음 일치는 건너뜀

            if (++checkCounter == 4096) { checkCounter = 0; if (ct.IsCancellationRequested) return; }
        }
    }

    // ───────────────────────── 정렬 ─────────────────────────

    private void SortByName(long[] items, VolumeIndex?[] vols)
    {
        // 순위 배열과 이름순 배열은 같은 시점에 만든 한 벌을 씁니다
        var order = _order;
        bool useOrder = order != null && SameVolumes(order.Volumes, vols);

        // 미리 계산한 순위가 모두 있으면 문자열 비교 없이 정렬합니다
        var keys = ArrayPool<int>.Shared.Rent(items.Length);
        try
        {
            for (int i = 0; i < items.Length; i++)
            {
                int vi = SearchResult.Volume(items[i]);
                int idx = SearchResult.Index(items[i]);
                var rank = useOrder ? order!.Ranks[vi] : vols[vi]!.Rank;
                int r = rank != null && idx < rank.Length ? rank[idx] : -1;
                if (r < 0)
                {
                    // 최근에 생기거나 이름이 바뀐 파일이 있음 → 문자열 비교 정렬 (몇 초 뒤 순위가 다시 계산됨)
                    Array.Sort(items, (a, b) => CompareName(vols, a, b));
                    return;
                }
                keys[i] = r;
            }

            if (useOrder && items.Length > order!.Keys.Length / 64)
            {
                // 결과가 많을 때: 순위 자리에 비트를 켜고, 켜진 비트를 앞에서부터 읽으면 곧 정렬 결과.
                // O(n log n) 비교 정렬 대신 O(전체 개수 / 64) — 수백만 개도 수 ms.
                int words = (order.Keys.Length + 63) >> 6;
                var bits = ArrayPool<ulong>.Shared.Rent(words);
                Array.Clear(bits, 0, words);
                for (int i = 0; i < items.Length; i++)
                {
                    int r = keys[i];
                    bits[r >> 6] |= 1UL << r;
                }
                int n = 0;
                for (int w = 0; w < words; w++)
                {
                    ulong word = bits[w];
                    while (word != 0)
                    {
                        int bit = System.Numerics.BitOperations.TrailingZeroCount(word);
                        items[n++] = order.Keys[(w << 6) | bit];
                        word &= word - 1;
                    }
                }
                ArrayPool<ulong>.Shared.Return(bits);
                return;
            }
            Array.Sort(keys, items, 0, items.Length);
        }
        finally
        {
            ArrayPool<int>.Shared.Return(keys);
        }
    }

    /// <summary>순서를 유지한 채 폴더를 앞으로 모읍니다.</summary>
    private static void FoldersFirst(long[] items, VolumeIndex?[] vols)
    {
        var files = new List<long>();
        int n = 0;
        foreach (long k in items)
        {
            var v = vols[SearchResult.Volume(k)];
            if (v != null && v.Entries[SearchResult.Index(k)].IsDirectory) items[n++] = k;
            else files.Add(k);
        }
        files.CopyTo(items, n);
    }

    private static bool SameVolumes(VolumeIndex?[] a, VolumeIndex?[] b)
    {
        if (a.Length > b.Length) return false;
        for (int i = 0; i < a.Length; i++) if (!ReferenceEquals(a[i], b[i])) return false;
        for (int i = a.Length; i < b.Length; i++) if (b[i] != null) return false;
        return true;
    }

    private static int CompareName(VolumeIndex?[] vols, long a, long b)
    {
        var va = vols[SearchResult.Volume(a)]!;
        var vb = vols[SearchResult.Volume(b)]!;
        int c = va.GetFoldedName(SearchResult.Index(a)).SequenceCompareTo(vb.GetFoldedName(SearchResult.Index(b)));
        return c != 0 ? c : a.CompareTo(b);
    }

    private void SortByPath(long[] items, VolumeIndex?[] vols, CancellationToken ct)
    {
        // 이름순으로 먼저 정렬한 뒤 폴더 경로로 안정 정렬 → 같은 폴더 안에서는 이름순
        SortByName(items, vols);
        var paths = new string[items.Length];
        Parallel.For(0, items.Length, new ParallelOptions { CancellationToken = ct }, i =>
        {
            var v = vols[SearchResult.Volume(items[i])]!;
            paths[i] = v.GetFoldedDirectoryPath(v.Entries[SearchResult.Index(items[i])].Parent);
        });
        StableReorder(items, (x, y) => string.CompareOrdinal(paths[x], paths[y]));
    }

    /// <summary>items의 현재 순서를 보조 키로 삼아 cmp(위치 x, 위치 y)로 안정 정렬합니다.</summary>
    private static void StableReorder(long[] items, Comparison<int> cmp)
    {
        var order = new int[items.Length];
        for (int i = 0; i < order.Length; i++) order[i] = i;
        Array.Sort(order, (x, y) =>
        {
            int c = cmp(x, y);
            return c != 0 ? c : x.CompareTo(y);
        });
        var copy = (long[])items.Clone();
        for (int i = 0; i < order.Length; i++) items[i] = copy[order[i]];
    }

    private void SortByExtension(long[] items, VolumeIndex?[] vols)
    {
        SortByName(items, vols);
        var exts = new string[items.Length];
        for (int i = 0; i < items.Length; i++)
        {
            var v = vols[SearchResult.Volume(items[i])]!;
            int idx = SearchResult.Index(items[i]);
            if (v.Entries[idx].IsDirectory) { exts[i] = ""; continue; }
            var name = v.GetFoldedName(idx);
            int dot = name.LastIndexOf('.');
            exts[i] = dot < 0 ? "" : name[(dot + 1)..].ToString();
        }
        StableReorder(items, (x, y) => string.CompareOrdinal(exts[x], exts[y]));
    }

    private static void SortByMeta(long[] items, string[] paths, bool bySize, CancellationToken ct)
    {
        var keys = new long[items.Length];
        Parallel.For(0, items.Length, new ParallelOptions { CancellationToken = ct, MaxDegreeOfParallelism = 16 }, i =>
        {
            NativeMethods.TryGetFileMeta(paths[i], out long size, out var mod);
            keys[i] = bySize ? size : mod.Ticks;
        });
        StableReorder(items, (x, y) => keys[x].CompareTo(keys[y]));
    }

    // ───────────────────────── 이름순 순위 미리 계산 ─────────────────────────

    /// <summary>
    /// 모든 드라이브의 항목을 이름순으로 한 번 정렬해 순위를 저장해 둡니다.
    /// 이후 검색 결과는 문자열 비교 없이 정수 순위로 정렬됩니다.
    /// </summary>
    public void RebuildRanks()
    {
        if (Interlocked.Exchange(ref _rankBusy, 1) == 1) return;
        try
        {
            var vols = _volumes;
            // 잠깐만 읽기 잠금을 잡고 스냅샷을 만듭니다. 이름 버퍼는 뒤에 덧붙이기만 하거나
            // (압축 시) 새 배열로 바뀌므로, 잡아 둔 배열의 기존 내용은 바뀌지 않습니다.
            var snapEntries = new Entry[vols.Length][];
            var snapNames = new char[vols.Length][];
            for (int vi = 0; vi < vols.Length; vi++)
            {
                var v = vols[vi];
                if (v == null) continue;
                v.Lock.EnterReadLock();
                try
                {
                    snapEntries[vi] = v.Entries.AsSpan(0, v.Count).ToArray();
                    snapNames[vi] = v.Folded;
                }
                finally { v.Lock.ExitReadLock(); }
            }

            int total = 0;
            foreach (var e in snapEntries) total += e?.Length ?? 0;
            var all = new long[total];
            int n = 0;
            for (int vi = 0; vi < vols.Length; vi++)
            {
                var es = snapEntries[vi];
                if (es == null) continue;
                for (int i = 0; i < es.Length; i++)
                    if (!es[i].IsDeleted) all[n++] = SearchResult.Key(vi, i);
            }
            Array.Resize(ref all, n);
            ParallelSort(all, (a, b) =>
            {
                int va = SearchResult.Volume(a), vb = SearchResult.Volume(b);
                ref readonly Entry ea = ref snapEntries[va][SearchResult.Index(a)];
                ref readonly Entry eb = ref snapEntries[vb][SearchResult.Index(b)];
                int c = snapNames[va].AsSpan(ea.NameStart, ea.NameLen)
                    .SequenceCompareTo(snapNames[vb].AsSpan(eb.NameStart, eb.NameLen));
                return c != 0 ? c : a.CompareTo(b);
            });

            var ranks = new int[vols.Length][];
            for (int vi = 0; vi < vols.Length; vi++)
            {
                if (snapEntries[vi] == null) continue;
                ranks[vi] = new int[snapEntries[vi].Length];
                Array.Fill(ranks[vi], -1);
            }
            for (int r = 0; r < all.Length; r++)
                ranks[SearchResult.Volume(all[r])][SearchResult.Index(all[r])] = r;

            for (int vi = 0; vi < vols.Length; vi++)
            {
                var v = vols[vi];
                if (v == null || ranks[vi] == null) continue;
                v.Lock.EnterReadLock();
                try
                {
                    // 스냅샷 이후 이름이 바뀐 항목은 순위를 모름(-1)으로
                    var es = snapEntries[vi];
                    var rk = ranks[vi];
                    for (int i = 0; i < es.Length; i++)
                        if (v.Entries[i].NameStart != es[i].NameStart) rk[i] = -1;
                    v.Rank = rk;
                }
                finally { v.Lock.ExitReadLock(); }
            }
            _order = new RankOrder(all, vols, ranks);
        }
        finally
        {
            Volatile.Write(ref _rankBusy, 0);
        }
    }

    /// <summary>조각별로 동시에 정렬한 뒤 합칩니다 (수백만 개 정렬을 코어 수만큼 빠르게).</summary>
    private static void ParallelSort(long[] a, Comparison<long> cmp)
    {
        int parts = Math.Clamp(Environment.ProcessorCount, 1, 16);
        if (a.Length < 100_000 || parts == 1) { Array.Sort(a, cmp); return; }
        var bounds = new int[parts + 1];
        for (int i = 0; i <= parts; i++) bounds[i] = (int)((long)a.Length * i / parts);
        Parallel.For(0, parts, p => Array.Sort(a, bounds[p], bounds[p + 1] - bounds[p], Comparer<long>.Create(cmp)));

        // 2개씩 합치기를 반복
        var src = a;
        var dst = new long[a.Length];
        var runs = bounds.ToList();
        while (runs.Count > 2)
        {
            var next = new List<int> { 0 };
            var pairs = new List<(int lo, int mid, int hi)>();
            for (int i = 0; i + 1 < runs.Count; i += 2)
            {
                int lo = runs[i], mid = runs[i + 1], hi = i + 2 < runs.Count ? runs[i + 2] : mid;
                pairs.Add((lo, mid, hi));
                next.Add(hi);
            }
            var s = src; var d = dst;
            Parallel.ForEach(pairs, pr =>
            {
                int i = pr.lo, j = pr.mid, k = pr.lo;
                while (i < pr.mid && j < pr.hi) d[k++] = cmp(s[i], s[j]) <= 0 ? s[i++] : s[j++];
                while (i < pr.mid) d[k++] = s[i++];
                while (j < pr.hi) d[k++] = s[j++];
            });
            (src, dst) = (dst, src);
            runs = next;
        }
        if (!ReferenceEquals(src, a)) Array.Copy(src, a, a.Length);
    }
}

/// <summary>
/// 검색 중 일치 항목을 모으는 버퍼. ArrayPool에서 빌려 쓰고 돌려주므로
/// 키 입력마다 큰 배열을 새로 만들지 않습니다 (대형 객체 힙 → 전체 GC 유발 방지).
/// </summary>
internal sealed class HitBuffer
{
    public long[] Items;
    public int Count;

    public HitBuffer(int initial) => Items = ArrayPool<long>.Shared.Rent(Math.Max(16, initial));

    public void Add(long v)
    {
        if (Count == Items.Length) Grow();
        Items[Count++] = v;
    }

    private void Grow()
    {
        var bigger = ArrayPool<long>.Shared.Rent(Items.Length * 2);
        Array.Copy(Items, bigger, Count);
        ArrayPool<long>.Shared.Return(Items);
        Items = bigger;
    }

    public void Release()
    {
        if (Items.Length > 0) ArrayPool<long>.Shared.Return(Items);
        Items = Array.Empty<long>();
        Count = 0;
    }
}
