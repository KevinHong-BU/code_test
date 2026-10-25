using System.Text;
using System.Text.RegularExpressions;

namespace FastFind;

internal enum TargetKind { All, Files, Folders }

/// <summary>
/// 검색어 해석 결과.
///
/// 문법 (Everything과 비슷):
///   보고서 2024        → 이름에 "보고서"와 "2024"가 모두 들어 있음 (AND)
///   "my file"          → 공백이 들어간 문구
///   !temp              → "temp"가 들어 있지 않음
///   *.pdf, ab?d        → 와일드카드 (이름 전체와 비교)
///   ext:jpg;png        → 확장자
///   users\kim          → \ 가 들어가면 전체 경로에서 찾음
///   regex:^IMG_\d+     → 정규식
/// </summary>
internal sealed class Query
{
    public List<Condition> Conditions { get; } = new();
    public TargetKind Target { get; init; }
    public bool MatchCase { get; init; }

    /// <summary>이름 버퍼 전체를 훑을 때 쓸 문자열 (대문자). 없으면 모든 항목을 하나씩 검사.</summary>
    public string? Prefilter { get; private set; }

    public bool IsEmpty => Conditions.Count == 0;

    public static Query Parse(string text, TargetKind target, bool matchCase)
    {
        var q = new Query { Target = target, MatchCase = matchCase };
        foreach (string raw in Tokenize(text))
        {
            string tok = raw;
            bool negate = false;
            while (tok.StartsWith('!')) { negate = !negate; tok = tok[1..]; }
            if (tok.Length == 0) continue;

            Condition? c;
            if (tok.StartsWith("ext:", StringComparison.OrdinalIgnoreCase))
                c = ExtensionCondition.Create(tok[4..]);
            else if (tok.StartsWith("regex:", StringComparison.OrdinalIgnoreCase))
                c = RegexCondition.Create(tok[6..], matchCase);
            else if (tok.Contains('\\') || tok.Contains('/'))
                c = new PathCondition(tok.Replace('/', '\\'), matchCase);
            else if (tok.Contains('*') || tok.Contains('?'))
                c = new WildcardCondition(tok, matchCase);
            else
                c = new LiteralCondition(tok, matchCase);

            if (c == null) continue;
            c.Negate = negate;
            q.Conditions.Add(c);
        }

        // 가장 긴 고정 문자열을 골라 버퍼 전체 훑기에 씁니다. 길수록 후보가 적어져 빠릅니다.
        string? best = null;
        foreach (var c in q.Conditions)
        {
            if (c.Negate) continue;
            string? lit = c.PrefilterLiteral;
            if (lit != null && lit.Length > 0 && (best == null || lit.Length > best.Length)) best = lit;
        }
        q.Prefilter = best;

        // 싼 조건을 앞에 두어 빨리 탈락시킵니다
        q.Conditions.Sort((a, b) => a.Cost.CompareTo(b.Cost));
        return q;
    }

    private static IEnumerable<string> Tokenize(string text)
    {
        var sb = new StringBuilder();
        bool inQuote = false;
        foreach (char ch in text)
        {
            if (ch == '"') { inQuote = !inQuote; continue; }
            if (!inQuote && char.IsWhiteSpace(ch))
            {
                if (sb.Length > 0) { yield return sb.ToString(); sb.Clear(); }
                continue;
            }
            sb.Append(ch);
        }
        if (sb.Length > 0) yield return sb.ToString();
    }

    /// <summary>모든 조건 검사 (대상 종류 포함).</summary>
    public bool Matches(VolumeIndex v, int idx)
    {
        ref readonly Entry e = ref v.Entries[idx];
        if (e.IsDeleted) return false;
        if (Target == TargetKind.Files && e.IsDirectory) return false;
        if (Target == TargetKind.Folders && !e.IsDirectory) return false;
        var conds = Conditions;
        for (int i = 0; i < conds.Count; i++)
        {
            var c = conds[i];
            if (c.IsMatch(v, idx) == c.Negate) return false;
        }
        return true;
    }
}

internal abstract class Condition
{
    public bool Negate;
    /// <summary>버퍼 훑기용 고정 문자열 (대문자). 없으면 null.</summary>
    public virtual string? PrefilterLiteral => null;
    public virtual int Cost => 1;
    public abstract bool IsMatch(VolumeIndex v, int idx);

    protected static string FoldedLongestLiteral(string pattern)
    {
        string best = "";
        foreach (var part in pattern.Split('*', '?'))
            if (part.Length > best.Length) best = part;
        return VolumeIndex.Fold(best);
    }

    /// <summary>와일드카드(*, ?) 비교. 탐욕적 + 마지막 * 위치로 되돌아가기 → O(n·m) 최악, 보통 O(n).</summary>
    public static bool WildMatch(ReadOnlySpan<char> s, ReadOnlySpan<char> p)
    {
        int si = 0, pi = 0, star = -1, mark = 0;
        while (si < s.Length)
        {
            if (pi < p.Length && (p[pi] == '?' || p[pi] == s[si])) { si++; pi++; }
            else if (pi < p.Length && p[pi] == '*') { star = pi++; mark = si; }
            else if (star >= 0) { pi = star + 1; si = ++mark; }
            else return false;
        }
        while (pi < p.Length && p[pi] == '*') pi++;
        return pi == p.Length;
    }
}

/// <summary>이름에 문자열이 들어 있음.</summary>
internal sealed class LiteralCondition : Condition
{
    private readonly string _text;
    private readonly string _folded;
    private readonly bool _matchCase;

    public LiteralCondition(string text, bool matchCase)
    {
        _text = text;
        _folded = VolumeIndex.Fold(text);
        _matchCase = matchCase;
    }

    public override string? PrefilterLiteral => _folded;

    public override bool IsMatch(VolumeIndex v, int idx) => _matchCase
        ? v.GetName(idx).IndexOf(_text.AsSpan()) >= 0
        : v.GetFoldedName(idx).IndexOf(_folded.AsSpan()) >= 0;
}

/// <summary>이름 전체가 와일드카드 패턴과 일치.</summary>
internal sealed class WildcardCondition : Condition
{
    private readonly string _pattern;
    private readonly bool _matchCase;
    private readonly string _literal;

    public WildcardCondition(string pattern, bool matchCase)
    {
        _matchCase = matchCase;
        _pattern = matchCase ? pattern : VolumeIndex.Fold(pattern);
        _literal = FoldedLongestLiteral(pattern);
    }

    public override string? PrefilterLiteral => _literal.Length > 0 ? _literal : null;
    public override int Cost => 2;

    public override bool IsMatch(VolumeIndex v, int idx) =>
        WildMatch(_matchCase ? v.GetName(idx) : v.GetFoldedName(idx), _pattern);
}

/// <summary>확장자 (ext:jpg;png).</summary>
internal sealed class ExtensionCondition : Condition
{
    private readonly string[] _exts; // 대문자, 점 없이

    private ExtensionCondition(string[] exts) => _exts = exts;

    public static ExtensionCondition? Create(string list)
    {
        var exts = list.Split(new[] { ';', ',', '|' }, StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
            .Select(x => VolumeIndex.Fold(x.TrimStart('.', '*'))).Where(x => x.Length > 0).Distinct().ToArray();
        return exts.Length == 0 ? null : new ExtensionCondition(exts);
    }

    // 확장자가 하나면 ".JPG"로 버퍼 훑기 가능
    public override string? PrefilterLiteral => _exts.Length == 1 ? "." + _exts[0] : null;

    public override bool IsMatch(VolumeIndex v, int idx)
    {
        if (v.Entries[idx].IsDirectory) return false;
        var name = v.GetFoldedName(idx);
        int dot = name.LastIndexOf('.');
        if (dot < 0) return false;
        var ext = name[(dot + 1)..];
        foreach (var e in _exts)
            if (ext.SequenceEqual(e)) return true;
        return false;
    }
}

/// <summary>
/// 전체 경로에 문자열(또는 와일드카드)이 들어 있음.
///
/// 전체 경로 = 폴더 경로(끝이 \) + 이름 이고 이름에는 \가 없으므로, 검색어를
/// 마지막 \ 까지(head)와 그 뒤(tail)로 나누면
///   일치 ⟺ 폴더 경로에 검색어가 있음  또는  (폴더 경로가 head로 끝나고 이름이 tail로 시작)
/// 입니다. 폴더별 결과를 검색 한 번 동안 캐시해 두므로 파일마다 경로를 만들 필요가 없습니다.
/// </summary>
internal sealed class PathCondition : Condition
{
    [ThreadStatic] private static char[]? t_buffer;

    private const byte Known = 1, DirContains = 2, DirEndsWithHead = 4;

    private readonly string _text;
    private readonly string _head;
    private readonly string _tail;
    private readonly bool _matchCase;
    private readonly bool _wild;
    private readonly System.Collections.Concurrent.ConcurrentDictionary<VolumeIndex, (long Version, byte[] States)> _cache = new();

    public PathCondition(string text, bool matchCase)
    {
        _matchCase = matchCase;
        _wild = text.Contains('*') || text.Contains('?');
        _text = matchCase ? text : VolumeIndex.Fold(text);
        // 와일드카드 경로는 전체 경로와 비교하되, 앞뒤에 * 가 없으면 경로 일부와도 맞도록 감쌉니다
        if (_wild) _text = "*" + _text.Trim('*') + "*";
        int last = _text.LastIndexOf('\\');
        _head = _text[..(last + 1)];
        _tail = _text[(last + 1)..];
    }

    public override int Cost => 10;

    public override bool IsMatch(VolumeIndex v, int idx)
    {
        if (_wild) return IsMatchSlow(v, idx);
        byte st = DirState(v, v.Entries[idx].Parent);
        if ((st & DirContains) != 0) return true;
        if (_tail.Length == 0 || (st & DirEndsWithHead) == 0) return false;
        var name = _matchCase ? v.GetName(idx) : v.GetFoldedName(idx);
        return name.StartsWith(_tail.AsSpan());
    }

    private byte DirState(VolumeIndex v, int dir)
    {
        byte[]? states = null;
        if (dir >= 0)
        {
            var entry = _cache.GetOrAdd(v, vv => (vv.Version, new byte[vv.Count]));
            if (entry.Version != v.Version || entry.States.Length < v.Count)
            {
                entry = (v.Version, new byte[v.Count]);
                _cache[v] = entry;
            }
            states = entry.States;
            byte cached = states[dir];
            if (cached != 0) return cached;
        }
        string path = _matchCase ? v.GetDirectoryPath(dir) : v.GetFoldedDirectoryPath(dir);
        byte st = Known;
        if (path.Contains(_text, StringComparison.Ordinal)) st |= DirContains;
        if (path.EndsWith(_head, StringComparison.Ordinal)) st |= DirEndsWithHead;
        if (states != null) states[dir] = st; // 여러 스레드가 같은 값을 써도 무해
        return st;
    }

    private bool IsMatchSlow(VolumeIndex v, int idx)
    {
        var buf = t_buffer ??= new char[33000];
        int len = v.WriteFullPath(idx, buf, !_matchCase);
        if (len < 0) return false;
        return WildMatch(buf.AsSpan(0, len), _text);
    }
}

/// <summary>정규식 (regex:패턴).</summary>
internal sealed class RegexCondition : Condition
{
    private readonly Regex _regex;

    private RegexCondition(Regex r) => _regex = r;

    public static RegexCondition? Create(string pattern, bool matchCase)
    {
        if (pattern.Length == 0) return null;
        try
        {
            var opts = RegexOptions.Compiled | RegexOptions.CultureInvariant;
            if (!matchCase) opts |= RegexOptions.IgnoreCase;
            return new RegexCondition(new Regex(pattern, opts, TimeSpan.FromMilliseconds(50)));
        }
        catch (ArgumentException) { return null; } // 입력 중인 잘못된 정규식은 무시
    }

    public override int Cost => 20;

    public override bool IsMatch(VolumeIndex v, int idx)
    {
        try { return _regex.IsMatch(v.GetName(idx)); }
        catch (RegexMatchTimeoutException) { return false; }
    }
}
