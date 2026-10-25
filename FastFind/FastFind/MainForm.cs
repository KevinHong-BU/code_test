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
        "  d:\\ 보고서\t특정 드라이브에서만 찾기\n" +
        "  regex:^IMG_\\d+\t정규식\n\n" +
        "폴더 탐색\n\n" +
        "  맨 앞의 드라이브 버튼에서 드라이브를 고르면 그 드라이브 안으로 들어갑니다.\n" +
        "  결과에서 폴더를 더블클릭(또는 Enter)하면 그 폴더 안으로 들어갑니다.\n" +
        "  폴더 안에서 검색어가 없으면 폴더 내용을, 검색어를 넣으면 그 폴더 아래 전체에서 찾습니다.\n" +
        "  경로 칸에 직접 경로를 입력하고 Enter를 눌러도 됩니다.\n\n" +
        "단축키\n\n" +
        "  Enter\t\t열기 / 폴더 안으로 들어가기\n" +
        "  Alt+← / Backspace\t뒤로\n" +
        "  Alt+↑\t\t상위 폴더\n" +
        "  Ctrl+Enter\t파일 위치 열기\n" +
        "  Ctrl+C\t\t전체 경로 복사\n" +
        "  ↓ / Esc\t결과 목록 ↔ 검색창 이동, Esc 두 번 = 검색어 지우기\n" +
        "  Ctrl+D\t\t드라이브 메뉴\n" +
        "  F5\t\t전체 다시 색인\n\n" +
        "결과를 끌어서 탐색기나 다른 프로그램에 놓을 수 있습니다.";

    private readonly IndexManager _manager = new();
    private readonly TextBox _search;
    private readonly ComboBox _target;
    private readonly Button _driveButton;
    private readonly ContextMenuStrip _driveMenu;
    private readonly Button _backButton;
    private readonly Button _upButton;
    private readonly TextBox _pathBox;
    private readonly System.Windows.Forms.Timer _deviceTimer;
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
    private bool _suppressSearch;

    /// <summary>
    /// 지금 보고 있는 위치. null = 선택한 모든 드라이브.
    /// Path는 끝에 \가 붙은 전체 경로이고, Vol/Dir은 그 경로를 색인에서 찾은 결과입니다.
    /// 다시 색인하면 Vol이 바뀌므로 그때는 Path로 다시 찾습니다.
    /// </summary>
    private sealed record NavLocation(string Root, string Path, VolumeIndex? Vol, int Dir);
    private NavLocation? _location;
    private readonly Stack<(NavLocation? Loc, string Text)> _history = new();
    private (VolumeIndex Vol, int Idx)? _selectAfter;  // 위로/뒤로 갔을 때 방금 나온 폴더를 선택

    private static readonly string[] ColumnTitles = { "이름", "경로", "크기", "수정한 날짜" };
    private static readonly SortColumn[] ColumnSorts = { SortColumn.Name, SortColumn.Path, SortColumn.Size, SortColumn.Modified };

    private readonly bool _isAdmin;
    private const string DefaultPlaceholder = "찾을 파일 이름  (예: 보고서 ext:pdf   *.jpg   users\\kim   !temp)";

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

        // ── 첫째 줄: [드라이브] [뒤로] [위로] [경로] ──
        var nav = new TableLayoutPanel
        {
            Dock = DockStyle.Top,
            AutoSize = true,
            ColumnCount = 4,
            RowCount = 1,
            Padding = new Padding(6, 6, 6, 0),
        };
        nav.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        nav.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        nav.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        nav.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));

        _driveButton = new Button
        {
            Text = "모든 드라이브 ▾",
            AutoSize = true,
            Anchor = AnchorStyles.Left,
            Margin = new Padding(0, 0, 6, 0),
        };
        _driveMenu = new ContextMenuStrip { ShowCheckMargin = true, ShowImageMargin = false };
        _driveButton.Click += (_, _) => ShowDriveMenu();

        _backButton = new Button { Text = "◀", Width = 34, Anchor = AnchorStyles.Left, Margin = new Padding(0, 0, 2, 0), Enabled = false };
        _backButton.Click += (_, _) => GoBack();
        _upButton = new Button { Text = "▲", Width = 34, Anchor = AnchorStyles.Left, Margin = new Padding(0, 0, 6, 0), Enabled = false };
        _upButton.Click += (_, _) => GoUp();
        var tips = new ToolTip();
        tips.SetToolTip(_driveButton, "드라이브 이동 / 색인할 드라이브 선택 (Ctrl+D)");
        tips.SetToolTip(_backButton, "뒤로 (Alt+←)");
        tips.SetToolTip(_upButton, "상위 폴더 (Alt+↑)");

        _pathBox = new TextBox
        {
            Dock = DockStyle.Fill,
            Font = new Font("Malgun Gothic", 10f),
            Margin = new Padding(0, 1, 0, 0),
            Text = "모든 드라이브",
        };
        _pathBox.KeyDown += PathBox_KeyDown;
        _pathBox.Enter += (_, _) => BeginInvokeSafe(_pathBox.SelectAll);

        nav.Controls.Add(_driveButton, 0, 0);
        nav.Controls.Add(_backButton, 1, 0);
        nav.Controls.Add(_upButton, 2, 0);
        nav.Controls.Add(_pathBox, 3, 0);

        // ── 둘째 줄: 검색창 + 옵션 ──
        var top = new TableLayoutPanel
        {
            Dock = DockStyle.Top,
            AutoSize = true,
            ColumnCount = 4,
            RowCount = 1,
            Padding = new Padding(6, 4, 6, 4),
        };
        top.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        top.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        top.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        top.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));

        _search = new TextBox
        {
            Dock = DockStyle.Fill,
            Font = new Font("Malgun Gothic", 11f),
            PlaceholderText = DefaultPlaceholder,
            Margin = new Padding(0, 0, 6, 0),
        };
        _search.TextChanged += (_, _) => { if (!_suppressSearch) RunSearch(keepSelection: false); };
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
        _list.MouseUp += (_, e) => { if (e.Button == MouseButtons.XButton1) GoBack(); }; // 마우스 뒤로 버튼
        _list.KeyDown += List_KeyDown;
        _list.ItemDrag += List_ItemDrag;

        _menu = new ContextMenuStrip();
        _menu.Items.Add("열기 / 폴더 안으로 들어가기", null, (_, _) => OpenSelected(location: false));
        _menu.Items.Add("탐색기로 열기", null, (_, _) => OpenSelected(location: false, shell: true));
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
        Controls.Add(nav);      // 나중에 넣은 것이 더 위쪽에 붙음 → nav가 맨 위
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
        _manager.StatusChanged += () => BeginInvokeSafe(() =>
        {
            _indexLabel.Text = _manager.GetStatusText();
            UpdateNav();
        });

        // USB 연결/해제 알림이 연달아 오므로 잠깐 모았다가 드라이브 목록을 다시 확인
        _deviceTimer = new System.Windows.Forms.Timer { Interval = 1500 };
        _deviceTimer.Tick += (_, _) =>
        {
            _deviceTimer.Stop();
            Task.Run(_manager.RefreshDrives);
        };
    }

    protected override void OnShown(EventArgs e)
    {
        base.OnShown(e);
        _search.Focus();
        // 드라이브 확인(IsReady)이 느린 경우가 있어 화면을 먼저 띄운 뒤 백그라운드에서 시작
        Task.Run(_manager.Start);
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
        var include = _manager.GetSearchFilter();

        ResolveLocation();
        var loc = _location;
        if (loc != null)
        {
            if (loc.Vol == null)
            {
                ShowResult(SearchResult.Empty, keepSelection: false);
                _resultLabel.Text = _manager.SlotOf(loc.Root) < 0 || !_manager.GetDrives().Any(d => d.Root == loc.Root)
                    ? $"{loc.Root.TrimEnd('\\')} 드라이브가 연결되어 있지 않습니다"
                    : $"{loc.Root.TrimEnd('\\')} 색인 중…";
                return;
            }
            // 이 위치의 드라이브만 검색합니다 (색인할 드라이브 선택과 관계없이)
            include = new bool[engine.Volumes.Length];
            int slot = _manager.SlotOf(loc.Root);
            if (slot >= 0 && slot < include.Length) include[slot] = true;

            // 검색어가 없으면 폴더 내용(바로 안)을, 있으면 폴더 아래 전체에서 찾기
            bool browse = query.IsEmpty;
            query.AddScope(new ScopeCondition(loc.Vol, loc.Dir, childrenOnly: browse));
            sort = sort with { FoldersFirst = browse };
        }

        Task.Run(() =>
        {
            try { return engine.Search(query, sort, cts.Token, include); }
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
            else if (_selectAfter is { } sel)
            {
                // 위로/뒤로 이동한 뒤에는 방금 있던 폴더를 선택해 둡니다
                int found = Array.FindIndex(result.Items, k =>
                    SearchResult.Index(k) == sel.Idx && ReferenceEquals(result.Volumes[SearchResult.Volume(k)], sel.Vol));
                if (found >= 0)
                {
                    _list.SelectedIndices.Add(found);
                    _list.Items[found].Focused = true;
                    _list.EnsureVisible(found);
                }
                else if (result.Items.Length > 0) _list.EnsureVisible(0);
            }
            else if (result.Items.Length > 0)
            {
                _list.EnsureVisible(0);
            }
            if (!keepSelection) _selectAfter = null;
            _list.Invalidate();
        }
        finally { _list.EndUpdate(); }

        string where = _location == null ? "" : (_search.Text.Trim().Length == 0 ? "폴더 내용 " : "이 폴더 아래 ");
        string text = $"{where}{result.Items.Length:N0}개  ·  {result.Elapsed.TotalMilliseconds:0.0} ms";
        if (result.Note != null) text += "  ·  " + result.Note;
        _resultLabel.Text = text;
    }

    // ───────────────────────── 목록 표시 ─────────────────────────

    private readonly record struct RowInfo(string Name, string Folder, string FullPath, bool IsDirectory, VolumeIndex Vol, int Idx);

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
            info = new RowInfo(name, folder, full, v.Entries[idx].IsDirectory, v, idx);
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

    // ───────────────────────── 드라이브 메뉴 ─────────────────────────

    private void ShowDriveMenu()
    {
        BuildDriveMenu();
        _driveMenu.Show(_driveButton, new Point(0, _driveButton.Height));
    }

    private void BuildDriveMenu()
    {
        var drives = _manager.GetDrives();
        _driveMenu.SuspendLayout();
        _driveMenu.Items.Clear();

        // ① 이동: 모든 드라이브 / 각 드라이브 안으로
        _driveMenu.Items.Add(new ToolStripMenuItem("모든 드라이브에서 찾기", null, (_, _) => NavigateTo(null))
        {
            Checked = _location == null,
        });
        _driveMenu.Items.Add(new ToolStripSeparator());
        if (drives.Count == 0)
            _driveMenu.Items.Add(new ToolStripMenuItem("연결된 드라이브가 없습니다") { Enabled = false });
        foreach (var d in drives)
        {
            string kind = d.Type == DriveType.Removable ? "  [이동식]" : "";
            string state = d.Selected ? $"  — {d.State}" : "  — 색인 안 함 (누르면 색인)";
            string root = d.Root;
            var item = new ToolStripMenuItem($"{root.TrimEnd('\\')}   {d.Label}{kind}{state}", null, (_, _) => EnterDrive(root))
            {
                Checked = _location != null && string.Equals(_location.Root, root, StringComparison.OrdinalIgnoreCase),
            };
            _driveMenu.Items.Add(item);
        }

        // ② 색인할 드라이브 고르기 (하위 메뉴, 여러 개를 연달아 체크할 수 있게 닫히지 않음)
        _driveMenu.Items.Add(new ToolStripSeparator());
        var indexMenu = new ToolStripMenuItem("색인할 드라이브 (모든 드라이브 검색 대상)");
        indexMenu.DropDown.Closing += (_, e) =>
        {
            if (e.CloseReason == ToolStripDropDownCloseReason.ItemClicked) e.Cancel = true;
        };
        foreach (var d in drives)
        {
            string kind = d.Type == DriveType.Removable ? "  [이동식]" : "";
            var check = new ToolStripMenuItem($"{d.Root.TrimEnd('\\')}   {d.Label}{kind}")
            {
                Checked = d.Selected,
                CheckOnClick = true,
                Tag = d.Root,
            };
            check.CheckedChanged += (_, _) =>
            {
                _manager.SetSelected((string)check.Tag!, check.Checked);
                UpdateNav();
                RunSearch(keepSelection: false);
            };
            indexMenu.DropDownItems.Add(check);
        }
        indexMenu.DropDownItems.Add(new ToolStripSeparator());
        indexMenu.DropDownItems.Add(new ToolStripMenuItem("모두 선택", null, (_, _) => SelectDrives(drives, d => true)));
        indexMenu.DropDownItems.Add(new ToolStripMenuItem("고정 디스크만", null,
            (_, _) => SelectDrives(drives, d => d.Type == DriveType.Fixed)));
        _driveMenu.Items.Add(indexMenu);
        _driveMenu.Items.Add(new ToolStripMenuItem("드라이브 목록 새로 고침", null, (_, _) => _manager.RefreshDrives()));
        _driveMenu.ResumeLayout();
    }

    private void SelectDrives(IReadOnlyList<IndexManager.DriveItem> drives, Func<IndexManager.DriveItem, bool> pick)
    {
        _manager.SetSelected(drives.Where(pick).Select(d => d.Root), true);
        _manager.SetSelected(drives.Where(d => !pick(d)).Select(d => d.Root), false);
        _driveMenu.Close();
        UpdateNav();
        RunSearch(keepSelection: false);
    }

    // ───────────────────────── 폴더 탐색 ─────────────────────────

    private void EnterDrive(string root)
    {
        // 색인하지 않던 드라이브면 지금 색인을 시작합니다 (끝나면 자동으로 내용이 나타남)
        if (!_manager.IsSelected(root)) _manager.SetSelected(root, true);
        NavigateTo(new NavLocation(root, root, null, -1));
    }

    private void EnterFolder(RowInfo row)
    {
        string path = row.FullPath.EndsWith('\\') ? row.FullPath : row.FullPath + "\\";
        NavigateTo(new NavLocation(row.Vol.Root, path, row.Vol, row.Idx));
    }

    private void GoUp()
    {
        var loc = _location;
        if (loc == null) return;
        if (loc.Dir < 0 && loc.Vol != null || loc.Path.Length <= loc.Root.Length)
        {
            NavigateTo(null);
            return;
        }
        if (loc.Vol != null)
        {
            var v = loc.Vol;
            int parent;
            string path;
            v.Lock.EnterReadLock();
            try
            {
                parent = v.Entries[loc.Dir].Parent;
                path = v.GetDirectoryPath(parent);
            }
            finally { v.Lock.ExitReadLock(); }
            _selectAfter = (v, loc.Dir);
            NavigateTo(new NavLocation(loc.Root, path, v, parent));
        }
        else
        {
            // 아직 색인 전: 경로 글자로 한 단계 위
            string trimmed = loc.Path.TrimEnd('\\');
            int cut = trimmed.LastIndexOf('\\');
            NavigateTo(new NavLocation(loc.Root, trimmed[..(cut + 1)], null, -1));
        }
    }

    private void GoBack()
    {
        if (_history.Count == 0) return;
        var (loc, text) = _history.Pop();
        if (_location?.Vol != null && _location.Dir >= 0) _selectAfter = (_location.Vol, _location.Dir);
        NavigateTo(loc, text, remember: false);
    }

    private void NavigateTo(NavLocation? loc, string searchText = "", bool remember = true)
    {
        if (remember) _history.Push((_location, _search.Text));
        _location = loc;
        _suppressSearch = true;
        _search.Text = searchText;
        _suppressSearch = false;
        UpdateNav();
        RunSearch(keepSelection: false);
    }

    /// <summary>
    /// 위치를 지금 색인과 맞춥니다: 다시 색인해서 색인이 바뀌었으면 경로로 다시 찾고,
    /// 폴더가 지워졌으면 남아 있는 상위 폴더로, 이름이 바뀌었으면 새 경로로 바꿉니다.
    /// </summary>
    private void ResolveLocation()
    {
        var loc = _location;
        if (loc == null) return;
        var vols = _manager.Engine.Volumes;
        int slot = _manager.SlotOf(loc.Root);
        var vol = slot >= 0 && slot < vols.Length ? vols[slot] : null;
        NavLocation updated;
        if (vol == null)
        {
            updated = loc with { Vol = null };
        }
        else
        {
            vol.Lock.EnterReadLock();
            try
            {
                int dir = loc.Dir;
                if (!ReferenceEquals(vol, loc.Vol)) dir = vol.FindDirectory(loc.Path, out _);
                for (int guard = 0; dir >= 0 && vol.Entries[dir].IsDeleted && guard < 4096; guard++)
                    dir = vol.Entries[dir].Parent;
                updated = new NavLocation(loc.Root, vol.GetDirectoryPath(dir), vol, dir);
            }
            finally { vol.Lock.ExitReadLock(); }
        }
        if (updated != loc)
        {
            _location = updated;
            UpdateNav();
        }
    }

    private void UpdateNav()
    {
        var loc = _location;
        _backButton.Enabled = _history.Count > 0;
        _upButton.Enabled = loc != null;
        if (!_pathBox.Focused)
            _pathBox.Text = loc == null ? "모든 드라이브" : (loc.Path.Length > 3 ? loc.Path.TrimEnd('\\') : loc.Path);

        if (loc == null)
        {
            var drives = _manager.GetDrives();
            var selected = drives.Where(d => d.Selected).Select(d => d.Root.TrimEnd('\\')).ToList();
            _driveButton.Text = (selected.Count == 0 ? "드라이브 없음"
                : selected.Count == drives.Count ? "모든 드라이브"
                : string.Join(" ", selected)) + " ▾";
            _search.PlaceholderText = DefaultPlaceholder;
        }
        else
        {
            _driveButton.Text = loc.Root.TrimEnd('\\') + " ▾";
            string name = loc.Path.Length > loc.Root.Length ? Path.GetFileName(loc.Path.TrimEnd('\\')) : loc.Root.TrimEnd('\\');
            _search.PlaceholderText = $"'{name}' 안에서 찾기  (비워 두면 폴더 내용 표시)";
        }
    }

    private void PathBox_KeyDown(object? sender, KeyEventArgs e)
    {
        if (e.KeyCode != Keys.Enter) return;
        e.Handled = e.SuppressKeyPress = true;
        string text = _pathBox.Text.Trim().Trim('"').Replace('/', '\\');
        if (text.Length == 0 || text == "모든 드라이브")
        {
            NavigateTo(null);
            _search.Focus();
            return;
        }
        if (text.Length >= 2 && text[1] == ':' && char.IsLetter(text[0]))
        {
            string root = char.ToUpperInvariant(text[0]) + ":\\";
            if (_manager.SlotOf(root) >= 0)
            {
                string path = root + text[2..].TrimStart('\\');
                if (!path.EndsWith('\\')) path += "\\";
                if (!_manager.IsSelected(root)) _manager.SetSelected(root, true);
                NavigateTo(new NavLocation(root, path, null, -1));
                _search.Focus();
                return;
            }
        }
        System.Media.SystemSounds.Beep.Play();
        _pathBox.SelectAll();
    }

    /// <summary>USB를 꽂거나 빼면 Windows가 보내는 알림.</summary>
    protected override void WndProc(ref Message m)
    {
        const int WM_DEVICECHANGE = 0x0219;
        const int DBT_DEVICEARRIVAL = 0x8000;
        const int DBT_DEVICEREMOVECOMPLETE = 0x8004;
        if (m.Msg == WM_DEVICECHANGE &&
            (m.WParam == (IntPtr)DBT_DEVICEARRIVAL || m.WParam == (IntPtr)DBT_DEVICEREMOVECOMPLETE))
        {
            _deviceTimer?.Stop();
            _deviceTimer?.Start();
        }
        base.WndProc(ref m);
    }

    // ───────────────────────── 키보드 / 동작 ─────────────────────────

    protected override bool ProcessCmdKey(ref Message msg, Keys keyData)
    {
        switch (keyData)
        {
            case Keys.Control | Keys.D:
                ShowDriveMenu();
                return true;
            case Keys.Alt | Keys.Left:
            case Keys.BrowserBack:
                GoBack();
                return true;
            case Keys.Alt | Keys.Up:
                GoUp();
                return true;
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
                if (_pathBox.Focused) { _search.Focus(); UpdateNav(); }
                else if (_search.Focused) _search.Clear();
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
        else if (e.KeyCode == Keys.Back)
        {
            GoBack();
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

    private void OpenSelected(bool location, bool shell = false)
    {
        var rows = SelectedRows(max: 20);
        // 폴더 하나를 고르고 열면 탐색기 대신 그 폴더 안으로 들어갑니다
        if (!location && !shell && rows.Count == 1 && rows[0].IsDirectory)
        {
            EnterFolder(rows[0]);
            return;
        }
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
