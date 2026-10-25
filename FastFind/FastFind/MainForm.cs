using System.Diagnostics;
using System.Security.Principal;

namespace FastFind;

internal sealed class MainForm : Form
{
    private const string HelpText =
        "검색어 문법\n\n" +
        "  보고서 2024\t이름에 두 단어가 모두 들어 있는 파일 (AND)\n" +
        "  \"my file\"\t공백이 들어간 문구\n" +
        "  !temp\t\t\"temp\"가 들어 있지 않은 파일\n" +
        "  *.pdf   a?c\t와일드카드 (* = 아무 글자들, ? = 한 글자)\n" +
        "  ext:jpg;png\t확장자\n" +
        "  users\\kim\t\\ 를 넣으면 폴더 경로까지 포함해서 찾음\n" +
        "  regex:^IMG_\\d+\t정규식\n\n" +
        "단축키\n\n" +
        "  Enter\t\t열기\n" +
        "  Ctrl+Enter\t파일 위치 열기\n" +
        "  Ctrl+C\t\t전체 경로 복사\n" +
        "  ↓ / Esc\t결과 목록 ↔ 검색창 이동, Esc 두 번 = 검색어 지우기\n" +
        "  F5\t\t전체 다시 색인\n\n" +
        "결과를 끌어서 탐색기나 다른 프로그램에 놓을 수 있습니다.";

    private readonly IndexManager _manager = new();
    private readonly TextBox _search;
    private readonly ComboBox _target;
    private readonly CheckBox _matchCase;
    private readonly ResultListView _list;
    private readonly ToolStripStatusLabel _resultLabel;
    private readonly ToolStripStatusLabel _indexLabel;
    private readonly ContextMenuStrip _menu;
    private readonly ShellIcons _icons;
    private readonly System.Windows.Forms.Timer _refreshTimer;

    private SearchResult _result = SearchResult.Empty;
    private CancellationTokenSource? _searchCts;
    private int _searchSeq;
    private SortSpec _sort = new(SortColumn.Name, false);
    private readonly Dictionary<long, (long Size, DateTime Modified)> _metaCache = new();
    private bool _pendingSearch;

    private static readonly string[] ColumnTitles = { "이름", "경로", "크기", "수정한 날짜" };
    private static readonly SortColumn[] ColumnSorts = { SortColumn.Name, SortColumn.Path, SortColumn.Size, SortColumn.Modified };

    private readonly bool _isAdmin;

    public MainForm()
    {
        _isAdmin = new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator);
        Text = "빠른 파일 찾기" + (_isAdmin ? "" : "  (일반 권한: 폴더 탐색 방식으로 색인)");
        AutoScaleMode = AutoScaleMode.Dpi;
        ClientSize = new Size(1100, 680);
        MinimumSize = new Size(500, 300);
        StartPosition = FormStartPosition.CenterScreen;
        KeyPreview = true;
        Icon = SystemIcons.Application;

        // ── 위쪽: 검색창 + 옵션 ──
        var top = new TableLayoutPanel
        {
            Dock = DockStyle.Top,
            AutoSize = true,
            ColumnCount = 4,
            RowCount = 1,
            Padding = new Padding(6, 6, 6, 4),
        };
        top.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        top.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        top.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        top.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));

        _search = new TextBox
        {
            Dock = DockStyle.Fill,
            Font = new Font("Malgun Gothic", 11f),
            PlaceholderText = "찾을 파일 이름  (예: 보고서 ext:pdf   *.jpg   users\\kim   !temp)",
            Margin = new Padding(0, 0, 6, 0),
        };
        _search.TextChanged += (_, _) => RunSearch(keepSelection: false);
        _search.KeyDown += Search_KeyDown;

        _target = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 90, Anchor = AnchorStyles.Left, Margin = new Padding(0, 3, 6, 0) };
        _target.Items.AddRange(new object[] { "전체", "파일만", "폴더만" });
        _target.SelectedIndex = 0;
        _target.SelectedIndexChanged += (_, _) => RunSearch(keepSelection: false);

        _matchCase = new CheckBox { Text = "대소문자 구분", AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(0, 6, 6, 0) };
        _matchCase.CheckedChanged += (_, _) => RunSearch(keepSelection: false);

        var help = new Button { Text = "도움말", AutoSize = true, Anchor = AnchorStyles.Left, Margin = new Padding(0, 2, 0, 0) };
        help.Click += (_, _) => MessageBox.Show(this, HelpText, "빠른 파일 찾기", MessageBoxButtons.OK, MessageBoxIcon.Information);

        top.Controls.Add(_search, 0, 0);
        top.Controls.Add(_target, 1, 0);
        top.Controls.Add(_matchCase, 2, 0);
        top.Controls.Add(help, 3, 0);

        // ── 결과 목록 (가상 모드: 화면에 보이는 줄만 만들어서 수백만 개도 즉시 표시) ──
        _icons = new ShellIcons(SystemInformation.SmallIconSize);
        _list = new ResultListView
        {
            Dock = DockStyle.Fill,
            View = View.Details,
            VirtualMode = true,
            FullRowSelect = true,
            HideSelection = false,
            SmallImageList = _icons.List,
            BorderStyle = BorderStyle.None,
        };
        int[] widths = { 300, 480, 100, 140 };
        for (int i = 0; i < ColumnTitles.Length; i++)
            _list.Columns.Add(ColumnTitles[i], widths[i], i == 2 ? HorizontalAlignment.Right : HorizontalAlignment.Left);
        _list.RetrieveVirtualItem += List_RetrieveVirtualItem;
        _list.ColumnClick += List_ColumnClick;
        _list.DoubleClick += (_, _) => OpenSelected(location: false);
        _list.KeyDown += List_KeyDown;
        _list.ItemDrag += List_ItemDrag;

        _menu = new ContextMenuStrip();
        _menu.Items.Add("열기", null, (_, _) => OpenSelected(location: false));
        _menu.Items.Add("파일 위치 열기", null, (_, _) => OpenSelected(location: true));
        _menu.Items.Add(new ToolStripSeparator());
        _menu.Items.Add("전체 경로 복사", null, (_, _) => CopySelected(fullPath: true));
        _menu.Items.Add("이름 복사", null, (_, _) => CopySelected(fullPath: false));
        _list.ContextMenuStrip = _menu;

        // ── 아래: 상태 표시줄 ──
        var status = new StatusStrip();
        _resultLabel = new ToolStripStatusLabel("색인 준비 중…") { Spring = false, Width = 260, TextAlign = ContentAlignment.MiddleLeft };
        _indexLabel = new ToolStripStatusLabel("") { Spring = true, TextAlign = ContentAlignment.MiddleRight };
        status.Items.Add(_resultLabel);
        status.Items.Add(_indexLabel);

        Controls.Add(_list);
        Controls.Add(top);
        Controls.Add(status);
        UpdateColumnHeaders();

        // 파일이 바뀌면 잠시 모았다가 검색을 다시 실행 (계속 바뀌어도 1초에 한 번 이하)
        _refreshTimer = new System.Windows.Forms.Timer { Interval = 1000 };
        _refreshTimer.Tick += (_, _) =>
        {
            _refreshTimer.Stop();
            _indexLabel.Text = _manager.GetStatusText();
            if (_pendingSearch) { _pendingSearch = false; RunSearch(keepSelection: true); }
        };

        _manager.IndexChanged += () => BeginInvokeSafe(() =>
        {
            _pendingSearch = true;
            if (!_refreshTimer.Enabled) _refreshTimer.Start();
        });
        _manager.StatusChanged += () => BeginInvokeSafe(() => _indexLabel.Text = _manager.GetStatusText());
    }

    protected override void OnShown(EventArgs e)
    {
        base.OnShown(e);
        _search.Focus();
        _manager.Start();
    }

    protected override void OnFormClosed(FormClosedEventArgs e)
    {
        _searchCts?.Cancel();
        _manager.Dispose();
        base.OnFormClosed(e);
    }

    private void BeginInvokeSafe(Action a)
    {
        if (IsDisposed || !IsHandleCreated) return;
        try { BeginInvoke(a); } catch (InvalidOperationException) { }
    }

    // ───────────────────────── 검색 ─────────────────────────

    private void RunSearch(bool keepSelection)
    {
        _searchCts?.Cancel();
        var cts = new CancellationTokenSource();
        _searchCts = cts;
        int seq = ++_searchSeq;

        var target = (TargetKind)Math.Max(0, _target.SelectedIndex);
        var query = Query.Parse(_search.Text, target, _matchCase.Checked);
        var sort = _sort;
        var engine = _manager.Engine;

        Task.Run(() =>
        {
            try { return engine.Search(query, sort, cts.Token); }
            catch (Exception) when (cts.IsCancellationRequested) { return null; }
            catch (Exception ex)
            {
                BeginInvokeSafe(() => _resultLabel.Text = "검색 오류: " + ex.Message);
                return null;
            }
        }).ContinueWith(t =>
        {
            if (t.Result == null || seq != _searchSeq || IsDisposed) return;
            ShowResult(t.Result, keepSelection);
        }, TaskScheduler.FromCurrentSynchronizationContext());
    }

    private void ShowResult(SearchResult result, bool keepSelection)
    {
        // 실시간 갱신으로 목록이 바뀌어도 보고 있던 파일의 선택과 스크롤 위치를 유지합니다
        VolumeIndex? focusedVol = null;
        int focusedIdx = -1;
        int topIndex = 0;
        if (keepSelection && _list.VirtualListSize > 0)
        {
            int f = _list.SelectedIndices.Count > 0 ? _list.SelectedIndices[0] : -1;
            if (f >= 0 && f < _result.Items.Length)
            {
                focusedVol = _result.Volumes[SearchResult.Volume(_result.Items[f])];
                focusedIdx = SearchResult.Index(_result.Items[f]);
            }
            topIndex = _list.TopItem?.Index ?? 0;
        }

        _list.BeginUpdate();
        try
        {
            _list.SelectedIndices.Clear();
            _result = result;
            _metaCache.Clear();
            _list.VirtualListSize = result.Items.Length;

            if (keepSelection)
            {
                if (focusedVol != null)
                {
                    int found = Array.FindIndex(result.Items, k =>
                        SearchResult.Index(k) == focusedIdx && ReferenceEquals(result.Volumes[SearchResult.Volume(k)], focusedVol));
                    if (found >= 0)
                    {
                        _list.SelectedIndices.Add(found);
                        _list.Items[found].Focused = true;
                    }
                }
                if (topIndex > 0 && topIndex < result.Items.Length)
                    _list.TopItem = _list.Items[topIndex];
            }
            else if (result.Items.Length > 0)
            {
                _list.EnsureVisible(0);
            }
            _list.Invalidate();
        }
        finally { _list.EndUpdate(); }

        string text = $"{result.Items.Length:N0}개  ·  {result.Elapsed.TotalMilliseconds:0.0} ms";
        if (result.Note != null) text += "  ·  " + result.Note;
        _resultLabel.Text = text;
    }

    // ───────────────────────── 목록 표시 ─────────────────────────

    private readonly record struct RowInfo(string Name, string Folder, string FullPath, bool IsDirectory);

    private bool TryGetRow(int row, out RowInfo info)
    {
        info = default;
        if ((uint)row >= (uint)_result.Items.Length) return false;
        long key = _result.Items[row];
        var v = _result.Volumes[SearchResult.Volume(key)];
        if (v == null) return false;
        int idx = SearchResult.Index(key);
        v.Lock.EnterReadLock();
        try
        {
            string name = v.GetName(idx).ToString();
            string folder = v.GetContainingFolder(idx);
            string full = folder.EndsWith('\\') ? folder + name : folder + "\\" + name;
            info = new RowInfo(name, folder, full, v.Entries[idx].IsDirectory);
        }
        finally { v.Lock.ExitReadLock(); }
        return true;
    }

    private void List_RetrieveVirtualItem(object? sender, RetrieveVirtualItemEventArgs e)
    {
        if (!TryGetRow(e.ItemIndex, out var row))
        {
            e.Item = new ListViewItem(new[] { "", "", "", "" });
            return;
        }

        long key = _result.Items[e.ItemIndex];
        if (!_metaCache.TryGetValue(key, out var meta))
        {
            NativeMethods.TryGetFileMeta(row.FullPath, out long size, out var mod);
            meta = (size, mod);
            if (_metaCache.Count > 20_000) _metaCache.Clear();
            _metaCache[key] = meta;
        }

        string sizeText = row.IsDirectory || meta.Size < 0 ? "" : FormatSize(meta.Size);
        string dateText = meta.Modified == DateTime.MinValue ? "" : meta.Modified.ToString("yyyy-MM-dd HH:mm");
        e.Item = new ListViewItem(new[] { row.Name, row.Folder, sizeText, dateText }, _icons.Get(row.Name, row.IsDirectory));
    }

    private static string FormatSize(long bytes)
    {
        if (bytes < 1024) return $"{bytes:N0} B";
        return $"{(bytes + 1023) / 1024:N0} KB";
    }

    private void List_ColumnClick(object? sender, ColumnClickEventArgs e)
    {
        var col = ColumnSorts[e.Column];
        _sort = _sort.Column == col ? _sort with { Descending = !_sort.Descending } : new SortSpec(col, col == SortColumn.Size || col == SortColumn.Modified);
        UpdateColumnHeaders();
        RunSearch(keepSelection: false);
    }

    private void UpdateColumnHeaders()
    {
        for (int i = 0; i < ColumnTitles.Length; i++)
        {
            string arrow = ColumnSorts[i] == _sort.Column ? (_sort.Descending ? "  ▼" : "  ▲") : "";
            _list.Columns[i].Text = ColumnTitles[i] + arrow;
        }
    }

    // ───────────────────────── 키보드 / 동작 ─────────────────────────

    protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
    {
        switch (keyData)
        {
            case Keys.F5:
                _manager.RebuildAll();
                return true;
            case Keys.Control | Keys.F:
            case Keys.Control | Keys.L:
            case Keys.F3:
                _search.Focus();
                _search.SelectAll();
                return true;
            case Keys.Escape:
                if (_search.Focused) _search.Clear();
                else { _search.Focus(); _search.SelectAll(); }
                return true;
        }
        return base.ProcessCmdKey(ref msg, keyData);
    }

    private void Search_KeyDown(object? sender, KeyEventArgs e)
    {
        if ((e.KeyCode == Keys.Down || e.KeyCode == Keys.Enter) && _list.VirtualListSize > 0)
        {
            _list.Focus();
            if (_list.SelectedIndices.Count == 0)
            {
                _list.SelectedIndices.Add(0);
                _list.Items[0].Focused = true;
            }
            if (e.KeyCode == Keys.Enter && e.Control) OpenSelected(location: true);
            else if (e.KeyCode == Keys.Enter) OpenSelected(location: false);
            e.Handled = e.SuppressKeyPress = true;
        }
    }

    private void List_KeyDown(object? sender, KeyEventArgs e)
    {
        if (e.KeyCode == Keys.Enter)
        {
            OpenSelected(location: e.Control);
            e.Handled = e.SuppressKeyPress = true;
        }
        else if (e.Control && e.KeyCode == Keys.C)
        {
            CopySelected(fullPath: !e.Shift);
            e.Handled = true;
        }
        else if (e.Control && e.KeyCode == Keys.A)
        {
            // 가상 목록 전체 선택 (항목 수가 많아도 빠름)
            _list.SelectAllItems();
            e.Handled = true;
        }
    }

    private List<RowInfo> SelectedRows(int max = 10_000)
    {
        var rows = new List<RowInfo>();
        foreach (int i in _list.SelectedIndices)
        {
            if (rows.Count >= max) break;
            if (TryGetRow(i, out var r)) rows.Add(r);
        }
        return rows;
    }

    private void OpenSelected(bool location)
    {
        var rows = SelectedRows(max: 20);
        foreach (var r in rows)
        {
            try
            {
                if (location)
                    Process.Start("explorer.exe", $"/select,\"{r.FullPath}\"");
                else if (_isAdmin)
                    // 이 프로그램은 관리자 권한으로 실행 중이므로, 탐색기를 거쳐 일반 권한으로 엽니다
                    Process.Start("explorer.exe", $"\"{r.FullPath}\"");
                else
                    Process.Start(new ProcessStartInfo(r.FullPath) { UseShellExecute = true });
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, $"{r.FullPath}\n\n{ex.Message}", "열 수 없음", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                break;
            }
        }
    }

    private void CopySelected(bool fullPath)
    {
        var rows = SelectedRows(max: 100_000);
        if (rows.Count == 0) return;
        string text = string.Join(Environment.NewLine, rows.Select(r => fullPath ? r.FullPath : r.Name));
        try { Clipboard.SetText(text); } catch { /* 클립보드를 다른 프로그램이 잡고 있으면 무시 */ }
    }

    private void List_ItemDrag(object? sender, ItemDragEventArgs e)
    {
        var paths = SelectedRows().Select(r => r.FullPath).ToArray();
        if (paths.Length == 0) return;
        _list.DoDragDrop(new DataObject(DataFormats.FileDrop, paths), DragDropEffects.Copy | DragDropEffects.Link);
    }
}

/// <summary>깜빡임 없는 가상 목록.</summary>
internal sealed class ResultListView : ListView
{
    private const int LVM_SETITEMSTATE = 0x1000 + 43;

    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
    private struct LVITEM
    {
        public uint mask;
        public int iItem;
        public int iSubItem;
        public uint state;
        public uint stateMask;
        public IntPtr pszText;
        public int cchTextMax;
        public int iImage;
        public IntPtr lParam;
        public int iIndent;
        public int iGroupId;
        public uint cColumns;
        public IntPtr puColumns;
        public IntPtr piColFmt;
        public int iGroup;
    }

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wParam, ref LVITEM lParam);

    public ResultListView()
    {
        DoubleBuffered = true;
    }

    /// <summary>항목 번호 -1에 선택 상태를 보내면 가상 목록 전체가 한 번에 선택됩니다.</summary>
    public void SelectAllItems()
    {
        var item = new LVITEM { mask = 0x8 /* LVIF_STATE */, state = 0x2, stateMask = 0x2 /* LVIS_SELECTED */ };
        SendMessage(Handle, LVM_SETITEMSTATE, new IntPtr(-1), ref item);
    }
}
