// 검색 엔진 테스트: 무작위 파일 트리를 만들어 엔진 결과를 단순 전수 검사 결과와 비교합니다.
using System.Diagnostics;
using FastFind;
System.Runtime.GCSettings.LatencyMode = System.Runtime.GCLatencyMode.SustainedLowLatency;

var rnd = new Random(42);
string[] words = { "report", "보고서", "image", "IMG", "photo", "사진", "data", "temp", "Backup", "readme", "setup", "config", "note", "log", "video", "music", "프로젝트", "final", "draft", "2024", "2025", "x" };
string[] exts = { ".txt", ".pdf", ".jpg", ".PNG", ".docx", ".exe", ".dll", ".log", "", ".mp4", ".cs" };
string RandName() {
    int n = rnd.Next(1, 4);
    var parts = new List<string>();
    for (int i = 0; i < n; i++) parts.Add(words[rnd.Next(words.Length)] + (rnd.Next(3) == 0 ? rnd.Next(1000).ToString() : ""));
    return string.Join(rnd.Next(2) == 0 ? "_" : " ", parts) + exts[rnd.Next(exts.Length)];
}

VolumeIndex BuildVol(string root, int count, ulong frnBase) {
    var v = new VolumeIndex(root, 1024) { RootFrn = 5 };
    v.EnableFrnTracking(count);
    var dirs = new List<int> { -1 };
    for (int i = 0; i < count; i++) {
        int parent = dirs[rnd.Next(dirs.Count)];
        bool isDir = rnd.Next(8) == 0;
        int idx = v.Add(RandName(), parent, isDir ? 0x10u : 0x20u);
        v.SetFrn(idx, frnBase + (ulong)idx);
        if (isDir) dirs.Add(idx);
    }
    return v;
}

int failures = 0;
void Check(bool ok, string msg) { if (!ok) { failures++; Console.WriteLine("FAIL: " + msg); } }

// Brute-force reference matcher, independent of the engine's buffer scan
List<long> Brute(SearchEngine eng, Query q) {
    var res = new List<long>();
    var vols = eng.Volumes;
    for (int vi = 0; vi < vols.Length; vi++) {
        var v = vols[vi]; if (v == null) continue;
        for (int i = 0; i < v.Count; i++) if (q.Matches(v, i)) res.Add(SearchResult.Key(vi, i));
    }
    return res;
}

// Simple independent check of semantics for literal terms
bool NaiveLiteral(VolumeIndex v, int i, string[] terms, bool matchCase) {
    if (v.Entries[i].IsDeleted) return false;
    string name = v.GetName(i).ToString();
    foreach (var t in terms) {
        if (matchCase ? !name.Contains(t, StringComparison.Ordinal) : !VolumeIndex.Fold(name).Contains(VolumeIndex.Fold(t), StringComparison.Ordinal)) return false;
    }
    return true;
}

var engine = new SearchEngine();
var c = BuildVol(@"C:\", 300_000, 1000);
var d = BuildVol(@"D:\", 120_000, 1000);
engine.Set(engine.Reserve(), c);
engine.Set(engine.Reserve(), d);
engine.RebuildRanks();

string[] queries = { "report", "REPORT", "보고서", "img 2024", "*.jpg", "*.png", "ex?e", "ext:pdf", "ext:jpg;png", "!temp", "!temp data", "\"photo_\"", "regex:^IMG\\d+", "\\보고서", "photo\\*.jpg", "", "x", "rt_im", "e", "zzzz_nomatch", "data !log ext:txt" };
foreach (var mc in new[] { false, true })
foreach (var tk in new[] { TargetKind.All, TargetKind.Files, TargetKind.Folders })
foreach (var qs in queries) {
    var q = Query.Parse(qs, tk, mc);
    var got = engine.Search(q, new SortSpec(SortColumn.Name, false), CancellationToken.None).Items;
    var exp = Brute(engine, q);
    var gs = got.ToHashSet();
    Check(got.Length == gs.Count, $"duplicates in '{qs}' mc={mc} tk={tk}");
    Check(gs.SetEquals(exp), $"mismatch '{qs}' mc={mc} tk={tk}: got {got.Length} exp {exp.Count}");
}

// drive filter: only selected drives are searched
{
    var q = Query.Parse("report", TargetKind.All, false);
    var both = engine.Search(q, new SortSpec(SortColumn.Name, false), CancellationToken.None).Items;
    var onlyC = engine.Search(q, new SortSpec(SortColumn.Name, false), CancellationToken.None, new[] { true, false }).Items;
    var onlyD = engine.Search(q, new SortSpec(SortColumn.Name, false), CancellationToken.None, new[] { false, true }).Items;
    var none = engine.Search(q, new SortSpec(SortColumn.Name, false), CancellationToken.None, new bool[0]).Items;
    Check(onlyC.All(k => SearchResult.Volume(k) == 0) && onlyC.Length > 0, "filter C only");
    Check(onlyD.All(k => SearchResult.Volume(k) == 1) && onlyD.Length > 0, "filter D only");
    Check(onlyC.Length + onlyD.Length == both.Length, "filter sums");
    Check(none.Length == 0, "filter none");
    var ex = Brute(engine, q).Where(k => SearchResult.Volume(k) == 1).ToHashSet();
    Check(ex.SetEquals(onlyD), "filter D brute");
    // 검색어로 드라이브 거르기 (d:\ report)
    var dPath = engine.Search(Query.Parse("d:\\ report", TargetKind.All, false), new SortSpec(SortColumn.Name, false), CancellationToken.None).Items;
    Check(dPath.ToHashSet().SetEquals(onlyD), "d:\\ path filter");
}

// folder scope: browse (children only) and search below a folder
{
    // 자식이 많은 폴더 하나 고르기
    var childCount = new Dictionary<int, int>();
    for (int i = 0; i < c.Count; i++) if (!c.Entries[i].IsDeleted && c.Entries[i].Parent >= 0)
        childCount[c.Entries[i].Parent] = childCount.GetValueOrDefault(c.Entries[i].Parent) + 1;
    int dir = childCount.OrderByDescending(kv => kv.Value).First().Key;

    bool Below(VolumeIndex v, int i, int d) { for (int p = v.Entries[i].Parent, g = 0; p >= 0 && g < 5000; p = v.Entries[p].Parent, g++) if (p == d) return true; return false; }

    // 폴더 내용 (검색어 없음)
    var qb = Query.Parse("", TargetKind.All, false);
    qb.AddScope(new ScopeCondition(c, dir, childrenOnly: true));
    var browse = engine.Search(qb, new SortSpec(SortColumn.Name, false, FoldersFirst: true), CancellationToken.None).Items;
    var expBrowse = Enumerable.Range(0, c.Count).Where(i => !c.Entries[i].IsDeleted && c.Entries[i].Parent == dir).Select(i => SearchResult.Key(0, i)).ToHashSet();
    Check(browse.ToHashSet().SetEquals(expBrowse), $"browse children {browse.Length} vs {expBrowse.Count}");
    bool seenFile = false, foldersFirstOk = true;
    foreach (var k in browse) { bool isDir = c.Entries[SearchResult.Index(k)].IsDirectory; if (!isDir) seenFile = true; else if (seenFile) foldersFirstOk = false; }
    Check(foldersFirstOk, "folders first");

    // 폴더 아래 전체에서 찾기
    var qs = Query.Parse("report", TargetKind.All, false);
    qs.AddScope(new ScopeCondition(c, dir, childrenOnly: false));
    var below = engine.Search(qs, new SortSpec(SortColumn.Name, false), CancellationToken.None).Items;
    var expBelow = Enumerable.Range(0, c.Count).Where(i => !c.Entries[i].IsDeleted && Below(c, i, dir) &&
        VolumeIndex.Fold(c.GetName(i).ToString()).Contains("REPORT")).Select(i => SearchResult.Key(0, i)).ToHashSet();
    Check(below.ToHashSet().SetEquals(expBelow), $"scope below {below.Length} vs {expBelow.Count}");

    // 다른 드라이브 항목은 범위에 들어오지 않음
    var qd = Query.Parse("report", TargetKind.All, false);
    qd.AddScope(new ScopeCondition(c, -1, childrenOnly: false));
    var whole = engine.Search(qd, new SortSpec(SortColumn.Name, false), CancellationToken.None).Items;
    Check(whole.All(k => SearchResult.Volume(k) == 0) && whole.Length > 0, "scope drive root");

    // 경로로 폴더 찾기
    string path = c.GetDirectoryPath(dir);
    Check(c.FindDirectory(path, out bool exact) == dir && exact, "find dir " + path);
    Check(c.FindDirectory(path.ToLowerInvariant().TrimEnd('\\'), out exact) == dir && exact, "find dir case/trailing");
    Check(c.FindDirectory(path + "no_such_folder_zz\\", out exact) == dir && !exact, "find dir nearest");
    Check(c.FindDirectory(@"C:\", out exact) == -1 && exact, "find root");
}

// literal semantics vs naive
foreach (var (qs, mc) in new[] { ("report", false), ("Report", true), ("img 20", false), ("사진 final", false) }) {
    var terms = qs.Split(' ');
    var got = engine.Search(Query.Parse(qs, TargetKind.All, mc), new SortSpec(SortColumn.Name, false), CancellationToken.None).Items.ToHashSet();
    int n = 0;
    foreach (var (vol, vi) in new[] { (c, 0), (d, 1) })
        for (int i = 0; i < vol.Count; i++) {
            bool e = NaiveLiteral(vol, i, terms, mc);
            if (e) n++;
            if (e != got.Contains(SearchResult.Key(vi, i))) { Check(false, $"naive mismatch '{qs}' {vol.GetName(i).ToString()}"); goto next; }
        }
    next: Check(n == got.Count, $"naive count '{qs}' {n} vs {got.Count}");
}

// sort order check
{
    var r = engine.Search(Query.Parse("", TargetKind.All, false), new SortSpec(SortColumn.Name, false), CancellationToken.None);
    bool sorted = true;
    for (int i = 1; i < r.Items.Length && sorted; i++) {
        var a = r.Items[i - 1]; var b = r.Items[i];
        var na = engine.Volumes[SearchResult.Volume(a)]!.GetFoldedName(SearchResult.Index(a));
        var nb = engine.Volumes[SearchResult.Volume(b)]!.GetFoldedName(SearchResult.Index(b));
        if (na.SequenceCompareTo(nb) > 0) sorted = false;
    }
    Check(sorted, "name sort");
    var rp = engine.Search(Query.Parse("report", TargetKind.All, false), new SortSpec(SortColumn.Path, false), CancellationToken.None);
    for (int i = 1; i < rp.Items.Length; i++) {
        var va = engine.Volumes[SearchResult.Volume(rp.Items[i-1])]!; var vb = engine.Volumes[SearchResult.Volume(rp.Items[i])]!;
        var pa = va.GetFoldedDirectoryPath(va.Entries[SearchResult.Index(rp.Items[i-1])].Parent);
        var pb = vb.GetFoldedDirectoryPath(vb.Entries[SearchResult.Index(rp.Items[i])].Parent);
        if (string.CompareOrdinal(pa, pb) > 0) { Check(false, "path sort"); break; }
    }
    var rd = engine.Search(Query.Parse("report", TargetKind.All, false), new SortSpec(SortColumn.Name, true), CancellationToken.None);
    Check(rd.Items.SequenceEqual(engine.Search(Query.Parse("report", TargetKind.All, false), new SortSpec(SortColumn.Name, false), CancellationToken.None).Items.Reverse()), "desc");
    var rx = engine.Search(Query.Parse("", TargetKind.All, false), new SortSpec(SortColumn.Extension, false), CancellationToken.None);
    Check(rx.Items.Length == r.Items.Length, "ext sort count");
}

// live updates: rename, move, delete, create, compaction
{
    // rename some files to a unique token
    var renamed = new List<int>();
    for (int k = 0; k < 2000; k++) {
        int i = rnd.Next(c.Count);
        if (c.Entries[i].IsDeleted || renamed.Contains(i)) continue;
        int parent = c.Entries[i].Parent;
        ulong pf = parent < 0 ? 5UL : 1000UL + (ulong)parent;
        c.Upsert(1000UL + (ulong)i, pf, "UNIQUEZQ_" + k + ".bin", 0x20);
        renamed.Add(i);
    }
    var r = engine.Search(Query.Parse("uniquezq", TargetKind.All, false), new SortSpec(SortColumn.Name, false), CancellationToken.None);
    Check(r.Items.Length == renamed.Count, $"rename count {r.Items.Length} vs {renamed.Count}");
    // ranks invalid for renamed → comparator path; still must be sorted
    // delete half of them
    int deleted = 0;
    foreach (int i in renamed.Take(renamed.Count / 2)) { if (c.Remove(1000UL + (ulong)i)) deleted++; }
    r = engine.Search(Query.Parse("uniquezq", TargetKind.All, false), new SortSpec(SortColumn.Name, false), CancellationToken.None);
    Check(r.Items.Length == renamed.Count - deleted, "delete count");
    // create new files in root
    for (int k = 0; k < 500; k++) c.Upsert(9_000_000UL + (ulong)k, 5, "newfile_QQ" + k + ".txt", 0x20);
    r = engine.Search(Query.Parse("newfile_qq", TargetKind.All, false), new SortSpec(SortColumn.Name, false), CancellationToken.None);
    Check(r.Items.Length == 500, "create count " + r.Items.Length);
    // unknown parent ignored
    Check(!c.Upsert(9_999_999UL, 123456789UL, "orphan", 0x20), "orphan ignored");
    // idempotent upsert
    Check(!c.Upsert(9_000_000UL, 5, "newfile_QQ0.txt", 0x20), "idempotent");
    // move a directory → path changes
    int someDir = -1; for (int i = 0; i < c.Count; i++) if (c.Entries[i].IsDirectory && !c.Entries[i].IsDeleted && c.Entries[i].Parent >= 0) { someDir = i; break; }
    string before = c.GetDirectoryPath(someDir);
    c.Upsert(1000UL + (ulong)someDir, 5, "MovedDirXYZ", 0x10);
    Check(c.GetDirectoryPath(someDir) == @"C:\MovedDirXYZ\", "move dir path: " + c.GetDirectoryPath(someDir) + " was " + before);

    // compaction keeps everything searchable
    for (int k = 0; k < 5; k++) for (int i = 0; i < c.Count; i += 3) {
        if (c.Entries[i].IsDeleted) continue;
        int parent = c.Entries[i].Parent; ulong pf = parent < 0 ? 5UL : 1000UL + (ulong)parent;
        if (i >= 300_000) continue;
        c.Upsert(1000UL + (ulong)i, pf, c.GetName(i).ToString() + "_r" + k, c.Entries[i].IsDirectory ? 0x10u : 0x20u);
    }
    int lenBefore = c.NamesLength;
    var qq = Query.Parse("report", TargetKind.All, false);
    var pre = engine.Search(qq, new SortSpec(SortColumn.Name, false), CancellationToken.None).Items.ToHashSet();
    c.CompactIfNeeded();
    Console.WriteLine($"compaction: {lenBefore:N0} -> {c.NamesLength:N0} chars");
    Check(c.NamesLength < lenBefore, "compaction shrank");
    var post = engine.Search(qq, new SortSpec(SortColumn.Name, false), CancellationToken.None).Items.ToHashSet();
    Check(pre.SetEquals(post), "compaction preserves results");
    foreach (var qs in queries) {
        var q = Query.Parse(qs, TargetKind.All, false);
        Check(engine.Search(q, new SortSpec(SortColumn.Name, false), CancellationToken.None).Items.ToHashSet().SetEquals(Brute(engine, q)), "post-compaction " + qs);
    }
    engine.RebuildRanks();
    var all = engine.Search(Query.Parse("", TargetKind.All, false), new SortSpec(SortColumn.Name, false), CancellationToken.None);
    Check(all.Items.Length == c.LiveCount + d.LiveCount, "live count");
}

// ResolveParents: broken chains get dropped
{
    var v = new VolumeIndex(@"E:\", 16) { RootFrn = 5 };
    v.EnableFrnTracking(16);
    var parents = new ulong[16];
    int a = v.Add("dirA", -2, 0x10); v.SetFrn(a, 100); parents[a] = 5;
    int b = v.Add("fileB", -2, 0x20); v.SetFrn(b, 101); parents[b] = 100;
    int x = v.Add("orphanDir", -2, 0x10); v.SetFrn(x, 102); parents[x] = 999;
    int y = v.Add("orphanChild", -2, 0x20); v.SetFrn(y, 103); parents[y] = 102;
    int z1 = v.Add("cyc1", -2, 0x10); v.SetFrn(z1, 104); parents[z1] = 105;
    int z2 = v.Add("cyc2", -2, 0x10); v.SetFrn(z2, 105); parents[z2] = 104;
    v.ResolveParents(parents);
    Check(!v.Entries[a].IsDeleted && !v.Entries[b].IsDeleted, "valid kept");
    Check(v.Entries[x].IsDeleted && v.Entries[y].IsDeleted && v.Entries[z1].IsDeleted && v.Entries[z2].IsDeleted, "broken dropped");
    Check(v.GetFullPath(b) == @"E:\dirA\fileB", "full path " + v.GetFullPath(b));
}

Console.WriteLine(failures == 0 ? "ALL TESTS PASSED" : $"{failures} FAILURES");

// ── Benchmark: 3M files ──
if (args.Contains("--bench")) {
    var big = new SearchEngine();
    var sw = Stopwatch.StartNew();
    var bv = BuildVol(@"C:\", 3_000_000, 1000);
    big.Set(big.Reserve(), bv);
    Console.WriteLine($"\nbuild 3M entries: {sw.ElapsedMilliseconds} ms, names {bv.NamesLength * 2 / 1024 / 1024} MB x2");
    sw.Restart(); big.RebuildRanks(); Console.WriteLine($"rank rebuild: {sw.ElapsedMilliseconds} ms");
    foreach (var qs in new[] { "report zzzzq", "report", "보고서 final", "zzzz_nomatch", "*.jpg", "ext:pdf", "e", "", "\\사진\\", "regex:^IMG\\d{3}" }) {
        var q = Query.Parse(qs, TargetKind.All, false);
        big.Search(q, new SortSpec(SortColumn.Name, false), CancellationToken.None); // warm
        var times = new List<double>(); int n = 0;
        for (int k = 0; k < 5; k++) { var r = big.Search(q, new SortSpec(SortColumn.Name, false), CancellationToken.None); times.Add(r.Elapsed.TotalMilliseconds); n = r.Items.Length; }
        times.Sort();
        Console.WriteLine($"  {("'" + qs + "'"),-22} {n,10:N0} results  median {times[2],7:0.0} ms");
    }
}
return failures == 0 ? 0 : 1;
