#include "MainWindow.h"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>

#include "HtmlExport.h"
#include "Renderer.h"
#include "resource.h"

namespace {

const wchar_t kClassName[] = L"LibraryNewsletterEditor";
const wchar_t kAppName[] = L"도서관 소식지 편집기";
const wchar_t kFileFilter[] = L"도서관 소식지 (*.lnews)\0*.lnews\0모든 파일 (*.*)\0*.*\0";
const wchar_t kHtmlFilter[] = L"HTML 문서 (*.html)\0*.html;*.htm\0모든 파일 (*.*)\0*.*\0";

std::wstring GetText(HWND control) {
    int len = GetWindowTextLengthW(control);
    std::wstring s(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(control, &s[0], len + 1);
    s.resize(static_cast<size_t>(len));
    return s;
}

bool ReadFileBytes(const std::wstring& path, std::string& out) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(file, &size) && size.QuadPart < 64 * 1024 * 1024;
    if (ok) {
        out.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        ok = out.empty() || (ReadFile(file, &out[0], static_cast<DWORD>(out.size()), &read, nullptr) &&
                             read == out.size());
    }
    DWORD error = ok ? 0 : GetLastError();
    CloseHandle(file);
    if (!ok) SetLastError(error ? error : ERROR_FILE_TOO_LARGE);
    return ok;
}

// 임시 파일에 먼저 쓴 뒤 바꿔치기해서, 저장 도중 실패해도 원래 파일이 망가지지 않게 합니다.
bool WriteFileBytes(const std::wstring& path, const std::string& data) {
    std::wstring temp = path + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
              written == data.size();
    ok = CloseHandle(file) && ok;
    if (ok) ok = MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) {
        DWORD error = GetLastError();
        DeleteFileW(temp.c_str());
        SetLastError(error);
    }
    return ok;
}

// 마지막 Windows 오류를 사람이 읽을 수 있는 문장으로 바꿉니다.
std::wstring LastErrorText() {
    DWORD code = GetLastError();
    wchar_t* buffer = nullptr;
    DWORD len = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                               nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring text = len ? nl::Trim(std::wstring(buffer, len)) : L"";
    if (buffer) LocalFree(buffer);
    return text + L" (오류 " + std::to_wstring(code) + L")";
}

std::wstring FileNameOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// 파일 이름에 쓸 수 없는 문자를 바꿉니다.
std::wstring SanitizeFileName(std::wstring name) {
    for (wchar_t& c : name) {
        if (wcschr(L"\\/:*?\"<>|", c) || c < 32) c = L'_';
    }
    name = nl::Trim(name);
    return name.empty() ? L"소식지" : name;
}

std::wstring ListItemText(const nl::Article& a) {
    std::wstring title = nl::Trim(a.title).empty() ? L"(제목 없음)" : nl::Trim(a.title);
    std::wstring category = nl::Trim(a.category);
    return category.empty() ? title : L"[" + category + L"] " + title;
}

}  // namespace

MainWindow::MainWindow(HINSTANCE instance) : instance_(instance) {}

MainWindow::~MainWindow() {
    if (uiFont_) DeleteObject(uiFont_);
    if (accel_) DestroyAcceleratorTable(accel_);
}

bool MainWindow::PreTranslateMessage(MSG* msg) {
    // 휠 메시지는 포커스를 가진 컨트롤로 가므로, 마우스가 미리보기 위에 있으면 미리보기로 보냅니다.
    if (msg->message == WM_MOUSEWHEEL) {
        POINT pt{static_cast<short>(LOWORD(msg->lParam)), static_cast<short>(HIWORD(msg->lParam))};
        if (WindowFromPoint(pt) == preview_.hwnd()) msg->hwnd = preview_.hwnd();
    }
    if (TranslateAcceleratorW(hwnd_, accel_, msg)) return true;
    return IsDialogMessageW(hwnd_, msg) != 0;
}

bool MainWindow::Create(int showCommand) {
    if (!PreviewWindow::Register(instance_)) return false;

    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance_;
    wc.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc)) return false;

    HDC screen = GetDC(nullptr);
    dpi_ = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);

    ACCEL keys[] = {
        {FCONTROL | FVIRTKEY, 'N', IDM_NEW},
        {FCONTROL | FVIRTKEY, 'O', IDM_OPEN},
        {FCONTROL | FVIRTKEY, 'S', IDM_SAVE},
        {FCONTROL | FSHIFT | FVIRTKEY, 'S', IDM_SAVE_AS},
        {FCONTROL | FVIRTKEY, 'E', IDM_EXPORT_HTML},
        {FCONTROL | FVIRTKEY, 'P', IDM_PRINT},
        {FCONTROL | FSHIFT | FVIRTKEY, 'N', IDM_ADD_ARTICLE},
        {FCONTROL | FSHIFT | FVIRTKEY, 'D', IDM_DUPLICATE_ARTICLE},
        {FCONTROL | FSHIFT | FVIRTKEY, VK_UP, IDM_MOVE_UP},
        {FCONTROL | FSHIFT | FVIRTKEY, VK_DOWN, IDM_MOVE_DOWN},
        {FCONTROL | FVIRTKEY, VK_OEM_PLUS, IDM_ZOOM_IN},
        {FCONTROL | FVIRTKEY, VK_ADD, IDM_ZOOM_IN},
        {FCONTROL | FVIRTKEY, VK_OEM_MINUS, IDM_ZOOM_OUT},
        {FCONTROL | FVIRTKEY, VK_SUBTRACT, IDM_ZOOM_OUT},
        {FCONTROL | FVIRTKEY, '0', IDM_ZOOM_FIT},
    };
    accel_ = CreateAcceleratorTableW(keys, static_cast<int>(sizeof(keys) / sizeof(keys[0])));

    // WS_CLIPCHILDREN은 쓰지 않습니다. 그룹 상자는 안쪽을 지우지 않아서, 부모가 그 영역을 건너뛰면 배경이 깨집니다.
    hwnd_ = CreateWindowExW(WS_EX_CONTROLPARENT | WS_EX_ACCEPTFILES, kClassName, kAppName,
                            WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, S(1280), S(860),
                            nullptr, nullptr, instance_, this);
    if (!hwnd_) return false;
    ShowWindow(hwnd_, showCommand);
    UpdateWindow(hwnd_);
    return true;
}

// ---------------------------------------------------------------------------
// 화면 구성

HWND MainWindow::AddLabel(const wchar_t* text, DWORD extraStyle) {
    HWND h = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | extraStyle, 0, 0, 0,
                             0, hwnd_,
                             nullptr, instance_, nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), FALSE);
    return h;
}

HWND MainWindow::AddEdit(int id, bool multiline) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
    style |= multiline ? (ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL) : ES_AUTOHSCROLL;
    HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", style, 0, 0, 0, 0, hwnd_,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), FALSE);
    SendMessageW(h, EM_SETLIMITTEXT, 1000000, 0);
    return h;
}

HWND MainWindow::AddButton(int id, const wchar_t* text) {
    HWND h = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd_,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), FALSE);
    return h;
}

void MainWindow::CreateControls() {
    uiFont_ = CreateFontW(-MulDiv(10, dpi_, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                          L"맑은 고딕");

    // 생성 순서가 Tab 키 이동 순서입니다.
    libraryLabel_ = AddLabel(L"도서관명");
    library_ = AddEdit(IDC_LIBRARY, false);
    issueLabel_ = AddLabel(L"호수");
    issue_ = AddEdit(IDC_ISSUE, false);
    titleLabel_ = AddLabel(L"제목");
    title_ = AddEdit(IDC_TITLE, false);
    dateLabel_ = AddLabel(L"발행일");
    date_ = AddEdit(IDC_DATE, false);
    today_ = AddButton(IDC_TODAY, L"오늘");
    themeLabel_ = AddLabel(L"테마");
    theme_ = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                             0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_THEME)), instance_, nullptr);
    SendMessageW(theme_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), FALSE);
    for (int i = 0; i < nl::ThemeCount(); ++i)
        SendMessageW(theme_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(nl::GetTheme(i).name));
    publisherLabel_ = AddLabel(L"발행처");
    publisher_ = AddEdit(IDC_PUBLISHER, true);
    greetingLabel_ = AddLabel(L"머리말");
    greeting_ = AddEdit(IDC_GREETING, true);

    list_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 0, 0,
                            0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_ARTICLE_LIST)), instance_, nullptr);
    SendMessageW(list_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), FALSE);
    addButton_ = AddButton(IDM_ADD_ARTICLE, L"기사 추가");
    duplicateButton_ = AddButton(IDM_DUPLICATE_ARTICLE, L"복제");
    deleteButton_ = AddButton(IDM_DELETE_ARTICLE, L"삭제");
    upButton_ = AddButton(IDM_MOVE_UP, L"▲ 위로");
    downButton_ = AddButton(IDM_MOVE_DOWN, L"▼ 아래로");

    categoryLabel_ = AddLabel(L"분류");
    category_ = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWN |
                                CBS_AUTOHSCROLL, 0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CATEGORY)), instance_,
                                nullptr);
    SendMessageW(category_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), FALSE);
    for (const auto& c : nl::DefaultCategories())
        SendMessageW(category_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(c.c_str()));
    articleTitleLabel_ = AddLabel(L"기사 제목");
    articleTitle_ = AddEdit(IDC_ART_TITLE, false);
    bodyLabel_ = AddLabel(L"본문");
    body_ = AddEdit(IDC_BODY, true);
    bodyHint_ = AddLabel(L"줄 앞에 \"- \"를 쓰면 글머리표 목록, 빈 줄은 문단 구분", SS_ENDELLIPSIS);

    // 그룹 상자는 다른 컨트롤 뒤(Z 순서 아래)에 놓이도록 마지막에 만듭니다.
    auto group = [this](const wchar_t* text) {
        HWND h = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0, 0, 0, 0, hwnd_, nullptr,
                                 instance_, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), FALSE);
        return h;
    };
    infoGroup_ = group(L"소식지 정보");
    articleGroup_ = group(L"기사");

    preview_.Create(hwnd_, IDC_PREVIEW, instance_);

    status_ = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, hwnd_,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_STATUS)), instance_, nullptr);
    SendMessageW(status_, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), FALSE);
}

void MainWindow::CreateMenuBar() {
    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, IDM_NEW, L"새 소식지(&N)\tCtrl+N");
    AppendMenuW(file, MF_STRING, IDM_NEW_SAMPLE, L"예제 소식지 열기(&X)");
    AppendMenuW(file, MF_STRING, IDM_OPEN, L"열기(&O)...\tCtrl+O");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_SAVE, L"저장(&S)\tCtrl+S");
    AppendMenuW(file, MF_STRING, IDM_SAVE_AS, L"다른 이름으로 저장(&A)...\tCtrl+Shift+S");
    AppendMenuW(file, MF_STRING, IDM_EXPORT_HTML, L"HTML로 내보내기(&H)...\tCtrl+E");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_PRINT, L"인쇄(&P)...\tCtrl+P");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, IDM_EXIT, L"끝내기(&X)");

    HMENU edit = CreatePopupMenu();
    AppendMenuW(edit, MF_STRING, IDM_ADD_ARTICLE, L"기사 추가(&A)\tCtrl+Shift+N");
    AppendMenuW(edit, MF_STRING, IDM_DUPLICATE_ARTICLE, L"기사 복제(&C)\tCtrl+Shift+D");
    AppendMenuW(edit, MF_STRING, IDM_DELETE_ARTICLE, L"기사 삭제(&D)");
    AppendMenuW(edit, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(edit, MF_STRING, IDM_MOVE_UP, L"위로 이동(&U)\tCtrl+Shift+↑");
    AppendMenuW(edit, MF_STRING, IDM_MOVE_DOWN, L"아래로 이동(&W)\tCtrl+Shift+↓");

    HMENU view = CreatePopupMenu();
    AppendMenuW(view, MF_STRING, IDM_ZOOM_IN, L"확대(&I)\tCtrl++");
    AppendMenuW(view, MF_STRING, IDM_ZOOM_OUT, L"축소(&O)\tCtrl+-");
    AppendMenuW(view, MF_STRING, IDM_ZOOM_FIT, L"너비에 맞춤(&F)\tCtrl+0");

    HMENU help = CreatePopupMenu();
    AppendMenuW(help, MF_STRING, IDM_ABOUT, L"정보(&A)...");

    HMENU bar = CreateMenu();
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"파일(&F)");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), L"편집(&E)");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"보기(&V)");
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(help), L"도움말(&H)");
    SetMenu(hwnd_, bar);
}

void MainWindow::LayoutControls() {
    RECT client;
    GetClientRect(hwnd_, &client);
    SendMessageW(status_, WM_SIZE, 0, 0);
    RECT statusRect;
    GetWindowRect(status_, &statusRect);
    const int statusH = statusRect.bottom - statusRect.top;

    int parts[] = {S(160), S(280), S(400), -1};
    SendMessageW(status_, SB_SETPARTS, 4, reinterpret_cast<LPARAM>(parts));

    const int pad = S(10);
    const int panelW = S(470);
    const int rowH = S(26);
    const int rowStep = S(32);
    const int labelW = S(66);
    const int colGap = S(14);
    const int bottom = client.bottom - statusH - pad;

    HDWP dwp = BeginDeferWindowPos(40);
    auto place = [&dwp](HWND h, int x, int y, int w, int hgt) {
        dwp = DeferWindowPos(dwp, h, nullptr, x, y, std::max(0, w), std::max(0, hgt), SWP_NOZORDER | SWP_NOACTIVATE);
    };

    // 소식지 정보 그룹
    const int il = pad + S(12);
    const int ir = panelW - S(12);
    // 왼쪽 열(도서관명·발행일)을 조금 더 넓게 잡습니다.
    const int half = (ir - il - colGap) * 3 / 5;
    const int col2 = il + half + colGap;
    int y = pad + S(24);

    place(libraryLabel_, il, y, labelW, rowH);
    place(library_, il + labelW, y, half - labelW, rowH);
    place(issueLabel_, col2, y, S(44), rowH);
    place(issue_, col2 + S(44), y, ir - col2 - S(44), rowH);
    y += rowStep;
    place(titleLabel_, il, y, labelW, rowH);
    place(title_, il + labelW, y, ir - il - labelW, rowH);
    y += rowStep;
    place(dateLabel_, il, y, labelW, rowH);
    place(date_, il + labelW, y, half - labelW - S(52), rowH);
    place(today_, il + half - S(48), y, S(48), rowH);
    place(themeLabel_, col2, y, S(44), rowH);
    place(theme_, col2 + S(44), y, ir - col2 - S(44), S(260));
    y += rowStep;
    place(publisherLabel_, il, y, labelW, rowH);
    place(publisher_, il + labelW, y, ir - il - labelW, S(48));
    y += S(48) + S(6);
    place(greetingLabel_, il, y, labelW, rowH);
    place(greeting_, il + labelW, y, ir - il - labelW, S(72));
    y += S(72) + S(12);
    place(infoGroup_, pad, pad, panelW - pad, y - pad);

    // 기사 그룹
    const int groupTop = y + S(6);
    y = groupTop + S(24);
    const int buttonW = S(86);
    const int listH = S(160);
    place(list_, il, y, ir - il - buttonW - S(8), listH);
    HWND buttons[] = {addButton_, duplicateButton_, deleteButton_, upButton_, downButton_};
    for (int i = 0; i < 5; ++i) place(buttons[i], ir - buttonW, y + i * S(32), buttonW, S(28));
    y += listH + S(10);
    place(categoryLabel_, il, y, labelW, rowH);
    place(category_, il + labelW, y, S(200), S(240));
    y += rowStep;
    place(articleTitleLabel_, il, y, labelW, rowH);
    place(articleTitle_, il + labelW, y, ir - il - labelW, rowH);
    y += rowStep;
    const int hintH = S(20);
    const int groupBottom = std::max(y + S(120), bottom);
    place(bodyLabel_, il, y, labelW, rowH);
    place(body_, il + labelW, y, ir - il - labelW, groupBottom - S(12) - hintH - S(4) - y);
    place(bodyHint_, il + labelW, groupBottom - S(12) - hintH, ir - il - labelW, hintH);
    place(articleGroup_, pad, groupTop, panelW - pad, groupBottom - groupTop);

    // 미리보기
    place(preview_.hwnd(), panelW + pad, pad, client.right - panelW - 2 * pad, bottom - pad);

    EndDeferWindowPos(dwp);
}

// ---------------------------------------------------------------------------
// 문서 <-> 화면

void MainWindow::LoadDocumentToUi() {
    loading_ = true;
    SetWindowTextW(library_, doc_.libraryName.c_str());
    SetWindowTextW(issue_, doc_.issue.c_str());
    SetWindowTextW(title_, doc_.title.c_str());
    SetWindowTextW(date_, doc_.date.c_str());
    SetWindowTextW(publisher_, nl::ToCrLf(doc_.publisher).c_str());
    SetWindowTextW(greeting_, nl::ToCrLf(doc_.greeting).c_str());
    SendMessageW(theme_, CB_SETCURSEL, doc_.theme, 0);

    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    for (const auto& a : doc_.articles)
        SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(ListItemText(a).c_str()));
    loading_ = false;

    current_ = -1;
    SelectArticle(doc_.articles.empty() ? -1 : 0);
    preview_.SetDocument(&doc_);
    UpdateStatus();
}

void MainWindow::SelectArticle(int index) {
    if (index >= static_cast<int>(doc_.articles.size())) index = static_cast<int>(doc_.articles.size()) - 1;
    current_ = index;
    loading_ = true;
    SendMessageW(list_, LB_SETCURSEL, index, 0);
    bool has = index >= 0;
    const nl::Article empty;
    const nl::Article& a = has ? doc_.articles[index] : empty;
    SetWindowTextW(category_, a.category.c_str());
    SetWindowTextW(articleTitle_, a.title.c_str());
    SetWindowTextW(body_, nl::ToCrLf(a.body).c_str());
    for (HWND h : {category_, articleTitle_, body_, categoryLabel_, articleTitleLabel_, bodyLabel_})
        EnableWindow(h, has);
    loading_ = false;
    UpdateArticleButtons();
}

void MainWindow::RefreshArticleItem(int index) {
    if (index < 0 || index >= static_cast<int>(doc_.articles.size())) return;
    loading_ = true;
    int top = static_cast<int>(SendMessageW(list_, LB_GETTOPINDEX, 0, 0));
    SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list_, LB_DELETESTRING, index, 0);
    SendMessageW(list_, LB_INSERTSTRING, index, reinterpret_cast<LPARAM>(ListItemText(doc_.articles[index]).c_str()));
    SendMessageW(list_, LB_SETCURSEL, current_, 0);
    SendMessageW(list_, LB_SETTOPINDEX, top, 0);
    SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list_, nullptr, TRUE);
    loading_ = false;
}

void MainWindow::UpdateArticleButtons() {
    int count = static_cast<int>(doc_.articles.size());
    bool has = current_ >= 0;
    EnableWindow(duplicateButton_, has);
    EnableWindow(deleteButton_, has);
    EnableWindow(upButton_, has && current_ > 0);
    EnableWindow(downButton_, has && current_ < count - 1);

    HMENU menu = GetMenu(hwnd_);
    if (!menu) return;
    auto enable = [menu](int id, bool on) { EnableMenuItem(menu, id, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED)); };
    enable(IDM_DUPLICATE_ARTICLE, has);
    enable(IDM_DELETE_ARTICLE, has);
    enable(IDM_MOVE_UP, has && current_ > 0);
    enable(IDM_MOVE_DOWN, has && current_ < count - 1);
}

void MainWindow::OnInfoChanged(int id) {
    switch (id) {
        case IDC_LIBRARY: doc_.libraryName = GetText(library_); break;
        case IDC_ISSUE: doc_.issue = GetText(issue_); break;
        case IDC_TITLE: doc_.title = GetText(title_); break;
        case IDC_DATE: doc_.date = GetText(date_); break;
        case IDC_PUBLISHER: doc_.publisher = nl::NormalizeNewlines(GetText(publisher_)); break;
        case IDC_GREETING: doc_.greeting = nl::NormalizeNewlines(GetText(greeting_)); break;
        case IDC_THEME: {
            int sel = static_cast<int>(SendMessageW(theme_, CB_GETCURSEL, 0, 0));
            doc_.theme = sel < 0 ? 0 : sel;
            break;
        }
        default: return;
    }
    MarkDirty();
    preview_.Refresh();
}

void MainWindow::OnArticleChanged(int id, int code) {
    if (current_ < 0) return;
    nl::Article& a = doc_.articles[current_];
    switch (id) {
        case IDC_CATEGORY:
            if (code == CBN_SELCHANGE) {
                // 선택 알림 시점에는 편집 칸 글자가 아직 바뀌지 않았으므로 목록에서 직접 읽습니다.
                int sel = static_cast<int>(SendMessageW(category_, CB_GETCURSEL, 0, 0));
                if (sel < 0) return;
                int len = static_cast<int>(SendMessageW(category_, CB_GETLBTEXTLEN, sel, 0));
                std::wstring text(static_cast<size_t>(len) + 1, L'\0');
                SendMessageW(category_, CB_GETLBTEXT, sel, reinterpret_cast<LPARAM>(&text[0]));
                text.resize(static_cast<size_t>(len));
                a.category = text;
            } else {
                a.category = GetText(category_);
            }
            RefreshArticleItem(current_);
            break;
        case IDC_ART_TITLE:
            a.title = GetText(articleTitle_);
            RefreshArticleItem(current_);
            break;
        case IDC_BODY:
            a.body = nl::NormalizeNewlines(GetText(body_));
            break;
        default:
            return;
    }
    MarkDirty();
    preview_.Refresh();
}

// ---------------------------------------------------------------------------
// 기사 편집

void MainWindow::AddArticle(bool duplicate) {
    nl::Article article;
    if (duplicate) {
        if (current_ < 0) return;
        article = doc_.articles[current_];
        article.title += L" (사본)";
    } else {
        article = nl::CreateArticle(current_ >= 0 ? doc_.articles[current_].category : nl::DefaultCategories()[0]);
    }
    int index = current_ >= 0 ? current_ + 1 : static_cast<int>(doc_.articles.size());
    doc_.articles.insert(doc_.articles.begin() + index, article);
    loading_ = true;
    SendMessageW(list_, LB_INSERTSTRING, index, reinterpret_cast<LPARAM>(ListItemText(article).c_str()));
    loading_ = false;
    SelectArticle(index);
    MarkDirty();
    preview_.Refresh();
    UpdateStatus();

    SetFocus(articleTitle_);
    SendMessageW(articleTitle_, EM_SETSEL, 0, -1);
}

void MainWindow::DeleteArticle() {
    if (current_ < 0) return;
    std::wstring message = L"'" + ListItemText(doc_.articles[current_]) + L"' 기사를 삭제할까요?";
    if (MessageBoxW(hwnd_, message.c_str(), kAppName, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return;
    int index = current_;
    doc_.articles.erase(doc_.articles.begin() + index);
    loading_ = true;
    SendMessageW(list_, LB_DELETESTRING, index, 0);
    loading_ = false;
    SelectArticle(std::min(index, static_cast<int>(doc_.articles.size()) - 1));
    MarkDirty();
    preview_.Refresh();
    UpdateStatus();
}

void MainWindow::MoveArticle(int delta) {
    int target = current_ + delta;
    if (current_ < 0 || target < 0 || target >= static_cast<int>(doc_.articles.size())) return;
    std::swap(doc_.articles[current_], doc_.articles[target]);
    int from = current_;
    current_ = target;
    RefreshArticleItem(from);
    RefreshArticleItem(target);
    SendMessageW(list_, LB_SETCURSEL, target, 0);
    UpdateArticleButtons();
    MarkDirty();
    preview_.Refresh();
}

// ---------------------------------------------------------------------------
// 파일

void MainWindow::NewDocument(bool sample) {
    doc_ = sample ? nl::CreateSample() : nl::CreateBlank();
    path_.clear();
    LoadDocumentToUi();
    SetClean();
}

bool MainWindow::OpenPath(const std::wstring& path) {
    std::string bytes;
    if (!ReadFileBytes(path, bytes)) {
        MessageBoxW(hwnd_, (L"파일을 읽을 수 없습니다.\n" + path + L"\n\n" + LastErrorText()).c_str(), kAppName,
                    MB_OK | MB_ICONERROR);
        return false;
    }
    nl::Newsletter loaded;
    std::wstring error;
    if (!nl::Deserialize(bytes, loaded, error)) {
        MessageBoxW(hwnd_, (L"파일을 열 수 없습니다.\n" + path + L"\n\n" + error).c_str(), kAppName,
                    MB_OK | MB_ICONERROR);
        return false;
    }
    doc_ = loaded;
    path_ = path;
    LoadDocumentToUi();
    SetClean();
    return true;
}

bool MainWindow::ConfirmDiscard() {
    if (!dirty_) return true;
    std::wstring name = path_.empty() ? L"제목 없음" : FileNameOf(path_);
    std::wstring message = L"'" + name + L"'의 변경 내용을 저장할까요?";
    switch (MessageBoxW(hwnd_, message.c_str(), kAppName, MB_YESNOCANCEL | MB_ICONQUESTION)) {
        case IDYES: return Save();
        case IDNO: return true;
        default: return false;
    }
}

bool MainWindow::Save() {
    if (path_.empty()) return SaveAs();
    return WriteDocument(path_);
}

std::wstring MainWindow::SuggestedFileName() const {
    if (!path_.empty()) {
        std::wstring name = FileNameOf(path_);
        size_t dot = name.find_last_of(L'.');
        return dot == std::wstring::npos ? name : name.substr(0, dot);
    }
    return SanitizeFileName(nl::Trim(doc_.title + L" " + doc_.issue));
}

bool MainWindow::SaveAs() {
    wchar_t file[MAX_PATH] = {};
    lstrcpynW(file, SuggestedFileName().c_str(), MAX_PATH);
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = kFileFilter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"lnews";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOREADONLYRETURN;
    if (!GetSaveFileNameW(&ofn)) return false;
    if (!WriteDocument(file)) return false;
    path_ = file;
    UpdateTitle();
    return true;
}

bool MainWindow::WriteDocument(const std::wstring& path) {
    if (!WriteFileBytes(path, nl::Serialize(doc_))) {
        MessageBoxW(hwnd_, (L"저장하지 못했습니다.\n" + path + L"\n\n" + LastErrorText()).c_str(), kAppName,
                    MB_OK | MB_ICONERROR);
        return false;
    }
    SetClean();
    return true;
}

void MainWindow::Open() {
    if (!ConfirmDiscard()) return;
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = kFileFilter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"lnews";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) OpenPath(file);
}

void MainWindow::ExportHtml() {
    wchar_t file[MAX_PATH] = {};
    lstrcpynW(file, SuggestedFileName().c_str(), MAX_PATH);
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = kHtmlFilter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"html";
    ofn.lpstrTitle = L"HTML로 내보내기";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOREADONLYRETURN;
    if (!GetSaveFileNameW(&ofn)) return;
    if (!WriteFileBytes(file, nl::ExportHtml(doc_))) {
        MessageBoxW(hwnd_, (std::wstring(L"내보내지 못했습니다.\n") + file + L"\n\n" + LastErrorText()).c_str(),
                    kAppName, MB_OK | MB_ICONERROR);
        return;
    }
    if (MessageBoxW(hwnd_, L"HTML 파일을 만들었습니다. 지금 웹 브라우저로 열어 볼까요?", kAppName,
                    MB_YESNO | MB_ICONINFORMATION) == IDYES) {
        ShellExecuteW(hwnd_, L"open", file, nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void MainWindow::Print() {
    // 화면과 같은 배치를 인쇄용으로 한 번 더 계산합니다(배치는 장치와 무관하므로 쪽 나눔이 같습니다).
    nl::Renderer renderer;
    renderer.Layout(doc_);
    const int pageCount = renderer.PageCount();

    PRINTDLGW pd{sizeof(pd)};
    pd.hwndOwner = hwnd_;
    pd.Flags = PD_RETURNDC | PD_NOSELECTION | PD_USEDEVMODECOPIESANDCOLLATE;
    pd.nMinPage = 1;
    pd.nMaxPage = static_cast<WORD>(pageCount);
    pd.nFromPage = 1;
    pd.nToPage = static_cast<WORD>(pageCount);
    pd.nCopies = 1;
    if (!PrintDlgW(&pd)) return;

    int first = 0, last = pageCount - 1;
    if (pd.Flags & PD_PAGENUMS) {
        first = std::max(1, static_cast<int>(pd.nFromPage)) - 1;
        last = std::min(pageCount, static_cast<int>(pd.nToPage)) - 1;
    }

    HDC dc = pd.hDC;
    std::wstring docName = nl::Trim(doc_.title + L" " + doc_.issue);
    if (docName.empty()) docName = kAppName;
    DOCINFOW info{sizeof(info)};
    info.lpszDocName = docName.c_str();

    bool ok = StartDocW(dc, &info) > 0;
    for (int page = first; ok && page <= last; ++page) {
        ok = StartPage(dc) > 0;
        if (!ok) break;
        // 인쇄 가능한 영역에 쪽 전체가 들어가도록 비율을 유지해 맞춥니다.
        int printableW = GetDeviceCaps(dc, HORZRES);
        int printableH = GetDeviceCaps(dc, VERTRES);
        double scale = std::min(static_cast<double>(printableW) / nl::kPageWidth,
                                static_cast<double>(printableH) / nl::kPageHeight);
        int offsetX = static_cast<int>((printableW - nl::kPageWidth * scale) / 2);
        int offsetY = static_cast<int>((printableH - nl::kPageHeight * scale) / 2);
        nl::SetupPageMapping(dc, scale);
        SetViewportOrgEx(dc, offsetX, offsetY, nullptr);
        renderer.DrawPage(dc, page);
        ok = EndPage(dc) > 0;
    }
    if (ok) EndDoc(dc);
    else AbortDoc(dc);

    DeleteDC(dc);
    if (pd.hDevMode) GlobalFree(pd.hDevMode);
    if (pd.hDevNames) GlobalFree(pd.hDevNames);
    if (!ok) MessageBoxW(hwnd_, L"인쇄하는 중에 오류가 발생했습니다.", kAppName, MB_OK | MB_ICONERROR);
}

// ---------------------------------------------------------------------------
// 상태 표시

void MainWindow::MarkDirty() {
    if (dirty_) return;
    dirty_ = true;
    UpdateTitle();
    UpdateStatus();
}

void MainWindow::SetClean() {
    dirty_ = false;
    UpdateTitle();
    UpdateStatus();
}

void MainWindow::UpdateTitle() {
    std::wstring name = path_.empty() ? L"제목 없음" : FileNameOf(path_);
    std::wstring text = (dirty_ ? L"*" : L"") + name + L" - " + kAppName;
    SetWindowTextW(hwnd_, text.c_str());
}

void MainWindow::UpdateStatus() {
    if (!status_) return;
    std::wstring state = dirty_ ? L"수정됨" : (path_.empty() ? L"새 문서" : L"저장됨");
    std::wstring articles = L"기사 " + std::to_wstring(doc_.articles.size()) + L"개";
    std::wstring pages = L"총 " + std::to_wstring(preview_.PageCount()) + L"쪽 (A4)";
    std::wstring zoom = L"미리보기 " + std::to_wstring(preview_.ZoomPercent()) + L"%";
    SendMessageW(status_, SB_SETTEXTW, 0, reinterpret_cast<LPARAM>(state.c_str()));
    SendMessageW(status_, SB_SETTEXTW, 1, reinterpret_cast<LPARAM>(articles.c_str()));
    SendMessageW(status_, SB_SETTEXTW, 2, reinterpret_cast<LPARAM>(pages.c_str()));
    SendMessageW(status_, SB_SETTEXTW, 3, reinterpret_cast<LPARAM>(zoom.c_str()));
}

// ---------------------------------------------------------------------------
// 메시지 처리

void MainWindow::OnCommand(int id, int code, HWND control) {
    if (control) {
        if (loading_) return;
        switch (id) {
            case IDC_LIBRARY:
            case IDC_ISSUE:
            case IDC_TITLE:
            case IDC_DATE:
            case IDC_PUBLISHER:
            case IDC_GREETING:
                if (code == EN_CHANGE) OnInfoChanged(id);
                return;
            case IDC_THEME:
                if (code == CBN_SELCHANGE) OnInfoChanged(id);
                return;
            case IDC_CATEGORY:
                if (code == CBN_SELCHANGE || code == CBN_EDITCHANGE) OnArticleChanged(id, code);
                return;
            case IDC_ART_TITLE:
            case IDC_BODY:
                if (code == EN_CHANGE) OnArticleChanged(id, code);
                return;
            case IDC_ARTICLE_LIST:
                if (code == LBN_SELCHANGE) {
                    int sel = static_cast<int>(SendMessageW(list_, LB_GETCURSEL, 0, 0));
                    if (sel != LB_ERR) SelectArticle(sel);
                }
                return;
            case IDC_TODAY:
                if (code == BN_CLICKED) SetWindowTextW(date_, nl::TodayString().c_str());
                return;
        }
        if (code != BN_CLICKED) return;
    }

    switch (id) {
        case IDM_NEW:
            if (ConfirmDiscard()) NewDocument(false);
            break;
        case IDM_NEW_SAMPLE:
            if (ConfirmDiscard()) NewDocument(true);
            break;
        case IDM_OPEN: Open(); break;
        case IDM_SAVE: Save(); break;
        case IDM_SAVE_AS: SaveAs(); break;
        case IDM_EXPORT_HTML: ExportHtml(); break;
        case IDM_PRINT: Print(); break;
        case IDM_EXIT: PostMessageW(hwnd_, WM_CLOSE, 0, 0); break;
        case IDM_ADD_ARTICLE: AddArticle(false); break;
        case IDM_DUPLICATE_ARTICLE: AddArticle(true); break;
        case IDM_DELETE_ARTICLE: DeleteArticle(); break;
        case IDM_MOVE_UP: MoveArticle(-1); break;
        case IDM_MOVE_DOWN: MoveArticle(+1); break;
        case IDM_ZOOM_IN: preview_.ZoomIn(); break;
        case IDM_ZOOM_OUT: preview_.ZoomOut(); break;
        case IDM_ZOOM_FIT: preview_.ZoomFit(); break;
        case IDM_ABOUT:
            MessageBoxW(hwnd_,
                        L"도서관 소식지 편집기 1.0\n\n"
                        L"도서관 소식지(뉴스레터)를 작성하고, 미리보기·인쇄·HTML 내보내기를 할 수 있습니다.\n\n"
                        L"· 왼쪽에서 소식지 정보와 기사를 입력하면 오른쪽 미리보기에 바로 반영됩니다.\n"
                        L"· 본문에서 \"- \"로 시작하는 줄은 글머리표 목록이 됩니다.\n"
                        L"· Ctrl+마우스 휠로 미리보기를 확대/축소할 수 있습니다.",
                        kAppName, MB_OK | MB_ICONINFORMATION);
            break;
    }
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self;
    if (msg == WM_NCCREATE) {
        self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            CreateControls();
            CreateMenuBar();
            LayoutControls();
            return 0;
        case WM_SIZE:
            LayoutControls();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = S(900);
            info->ptMinTrackSize.y = S(700);
            return 0;
        }
        case WM_COMMAND:
            OnCommand(LOWORD(wParam), HIWORD(wParam), reinterpret_cast<HWND>(lParam));
            return 0;
        case WM_APP_PREVIEW_CHANGED:
            UpdateStatus();
            return 0;
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wParam);
            wchar_t file[MAX_PATH];
            bool got = DragQueryFileW(drop, 0, file, MAX_PATH) > 0;
            DragFinish(drop);
            if (got && ConfirmDiscard()) OpenPath(file);
            return 0;
        }
        case WM_CLOSE:
            if (ConfirmDiscard()) DestroyWindow(hwnd_);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}
