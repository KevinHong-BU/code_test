// BatchRename.cpp
// 한 폴더에서 선택한 파일들의 이름을 순서대로 한꺼번에 바꾸는 Win32 프로그램
// 예) 접두어 "사진_", 시작 번호 1, 자릿수 3  ->  사진_001.jpg, 사진_002.jpg, ...

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ---------------------------------------------------------------------------
// 컨트롤 ID
// ---------------------------------------------------------------------------
enum {
    IDC_SELECT = 101,
    IDC_FOLDER,
    IDC_LBL_PREFIX,
    IDC_PREFIX,
    IDC_LBL_SUFFIX,
    IDC_SUFFIX,
    IDC_LBL_START,
    IDC_START,
    IDC_LBL_DIGITS,
    IDC_DIGITS,
    IDC_KEEPEXT,
    IDC_LIST,
    IDC_UP,
    IDC_DOWN,
    IDC_REMOVE,
    IDC_SORTNAME,
    IDC_SORTDATE,
    IDC_REVERSE,
    IDC_STATUS,
    IDC_RENAME,
    IDC_CLOSE
};

struct FileItem {
    std::wstring name;   // 파일 이름 (경로 제외)
    FILETIME     mtime;  // 수정 시각
};

static HINSTANCE             g_hInst;
static HWND                  g_hWnd;
static HFONT                 g_hFont;
static std::wstring          g_folder;   // 선택한 파일들이 있는 폴더
static std::vector<FileItem> g_files;    // 이름을 바꿀 순서대로 정렬된 목록

static HWND Ctl(int id) { return GetDlgItem(g_hWnd, id); }

static std::wstring GetText(int id)
{
    HWND h = Ctl(id);
    int len = GetWindowTextLengthW(h);
    std::wstring s(len + 1, L'\0');
    GetWindowTextW(h, &s[0], len + 1);
    s.resize(len);
    return s;
}

static int GetInt(int id, int defVal)
{
    std::wstring s = GetText(id);
    if (s.empty()) return defVal;
    return _wtoi(s.c_str());
}

static std::wstring JoinPath(const std::wstring& dir, const std::wstring& name)
{
    if (!dir.empty() && dir.back() == L'\\') return dir + name;
    return dir + L"\\" + name;
}

static void SetStatus(const std::wstring& s) { SetWindowTextW(Ctl(IDC_STATUS), s.c_str()); }

// ---------------------------------------------------------------------------
// 새 이름 만들기
// ---------------------------------------------------------------------------
static std::wstring MakeNewName(size_t index)
{
    std::wstring prefix = GetText(IDC_PREFIX);
    std::wstring suffix = GetText(IDC_SUFFIX);
    int start  = GetInt(IDC_START, 1);
    int digits = GetInt(IDC_DIGITS, 3);
    if (digits < 1)  digits = 1;
    if (digits > 10) digits = 10;

    std::wstring num = std::to_wstring((long long)start + (long long)index);
    if ((int)num.size() < digits) num.insert(0, digits - num.size(), L'0');

    std::wstring ext;
    if (IsDlgButtonChecked(g_hWnd, IDC_KEEPEXT) == BST_CHECKED)
        ext = PathFindExtensionW(g_files[index].name.c_str());

    return prefix + num + suffix + ext;
}

// ---------------------------------------------------------------------------
// 목록 표시
// ---------------------------------------------------------------------------
static void UpdatePreview()
{
    HWND hList = Ctl(IDC_LIST);
    for (size_t i = 0; i < g_files.size(); ++i) {
        std::wstring nn = MakeNewName(i);
        ListView_SetItemText(hList, (int)i, 2, &nn[0]);
    }
}

static void RefreshList(const std::vector<bool>* selection = nullptr)
{
    HWND hList = Ctl(IDC_LIST);
    SendMessageW(hList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(hList);

    for (size_t i = 0; i < g_files.size(); ++i) {
        std::wstring no = std::to_wstring(i + 1);
        LVITEMW it = {};
        it.mask    = LVIF_TEXT;
        it.iItem   = (int)i;
        it.pszText = &no[0];
        ListView_InsertItem(hList, &it);
        ListView_SetItemText(hList, (int)i, 1, &g_files[i].name[0]);
        if (selection && i < selection->size() && (*selection)[i])
            ListView_SetItemState(hList, (int)i, LVIS_SELECTED, LVIS_SELECTED);
    }
    UpdatePreview();

    SendMessageW(hList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(hList, nullptr, TRUE);

    SetWindowTextW(Ctl(IDC_FOLDER), g_folder.empty() ? L"(선택된 폴더 없음)" : g_folder.c_str());
    if (!g_files.empty())
        SetStatus(std::to_wstring(g_files.size()) + L"개 파일");
}

static std::vector<bool> GetSelection()
{
    HWND hList = Ctl(IDC_LIST);
    std::vector<bool> sel(g_files.size(), false);
    for (size_t i = 0; i < g_files.size(); ++i)
        sel[i] = (ListView_GetItemState(hList, (int)i, LVIS_SELECTED) & LVIS_SELECTED) != 0;
    return sel;
}

// ---------------------------------------------------------------------------
// 파일 선택 (한 폴더 안에서 여러 개)
// ---------------------------------------------------------------------------
static void SelectFiles()
{
    const DWORD BUF = 1 << 20;   // 경로가 많아도 충분하도록 1M 문자
    std::vector<wchar_t> buf(BUF, L'\0');

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_hWnd;
    ofn.lpstrFilter = L"모든 파일 (*.*)\0*.*\0"
                      L"이미지 (*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.heic)\0*.jpg;*.jpeg;*.png;*.bmp;*.gif;*.heic\0"
                      L"문서 (*.txt;*.pdf;*.docx;*.xlsx;*.pptx;*.hwp)\0*.txt;*.pdf;*.docx;*.xlsx;*.pptx;*.hwp\0";
    ofn.lpstrFile   = buf.data();
    ofn.nMaxFile    = BUF;
    ofn.lpstrTitle  = L"이름을 바꿀 파일 선택 (Ctrl/Shift로 여러 개 선택, Ctrl+A로 전체 선택)";
    ofn.Flags       = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST |
                      OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;

    if (!GetOpenFileNameW(&ofn)) {
        if (CommDlgExtendedError() == FNERR_BUFFERTOOSMALL)
            MessageBoxW(g_hWnd, L"선택한 파일이 너무 많습니다. 나눠서 작업해 주세요.", L"오류", MB_ICONERROR);
        return;
    }

    // 결과 형식: 하나 선택 -> "전체경로\0\0"
    //            여러 개   -> "폴더\0파일1\0파일2\0...\0\0"
    std::wstring first = buf.data();
    const wchar_t* p = buf.data() + first.size() + 1;

    std::vector<std::wstring> names;
    std::wstring folder;
    if (*p == L'\0') {
        size_t pos = first.find_last_of(L'\\');
        folder = first.substr(0, pos);
        names.push_back(first.substr(pos + 1));
        if (folder.size() == 2 && folder[1] == L':') folder += L'\\';   // "C:" -> "C:\"
    } else {
        folder = first;
        while (*p) {
            names.push_back(p);
            p += wcslen(p) + 1;
        }
    }

    g_folder = folder;
    g_files.clear();
    for (const auto& n : names) {
        FileItem fi = {};
        fi.name = n;
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(JoinPath(folder, n).c_str(), GetFileExInfoStandard, &fad))
            fi.mtime = fad.ftLastWriteTime;
        g_files.push_back(fi);
    }

    // 기본은 탐색기와 같은 이름순(숫자 인식) 정렬
    std::stable_sort(g_files.begin(), g_files.end(), [](const FileItem& a, const FileItem& b) {
        return StrCmpLogicalW(a.name.c_str(), b.name.c_str()) < 0;
    });
    RefreshList();
}

// ---------------------------------------------------------------------------
// 순서 바꾸기 / 정렬 / 제거
// ---------------------------------------------------------------------------
static void MoveSelected(bool up)
{
    std::vector<bool> sel = GetSelection();
    size_t n = g_files.size();
    if (n < 2) return;

    if (up) {
        for (size_t i = 1; i < n; ++i)
            if (sel[i] && !sel[i - 1]) {
                std::swap(g_files[i], g_files[i - 1]);
                sel[i] = false; sel[i - 1] = true;
            }
    } else {
        for (size_t i = n - 1; i-- > 0;)
            if (sel[i] && !sel[i + 1]) {
                std::swap(g_files[i], g_files[i + 1]);
                sel[i] = false; sel[i + 1] = true;
            }
    }
    RefreshList(&sel);
    SetFocus(Ctl(IDC_LIST));
}

static void RemoveSelected()
{
    std::vector<bool> sel = GetSelection();
    std::vector<FileItem> keep;
    for (size_t i = 0; i < g_files.size(); ++i)
        if (!sel[i]) keep.push_back(g_files[i]);
    g_files.swap(keep);
    RefreshList();
}

static void SortByName()
{
    std::stable_sort(g_files.begin(), g_files.end(), [](const FileItem& a, const FileItem& b) {
        return StrCmpLogicalW(a.name.c_str(), b.name.c_str()) < 0;
    });
    RefreshList();
}

static void SortByDate()
{
    std::stable_sort(g_files.begin(), g_files.end(), [](const FileItem& a, const FileItem& b) {
        return CompareFileTime(&a.mtime, &b.mtime) < 0;
    });
    RefreshList();
}

static void ReverseOrder()
{
    std::reverse(g_files.begin(), g_files.end());
    RefreshList();
}

// ---------------------------------------------------------------------------
// 이름 바꾸기 실행
// 1단계: 모든 파일을 임시 이름으로 변경
// 2단계: 임시 이름 -> 최종 이름
// (예: 1.jpg <-> 2.jpg 처럼 서로 이름이 겹쳐도 안전하게 처리)
// ---------------------------------------------------------------------------
static bool ContainsNoCase(const std::vector<std::wstring>& v, const std::wstring& s)
{
    for (const auto& x : v)
        if (lstrcmpiW(x.c_str(), s.c_str()) == 0) return true;
    return false;
}

static void DoRename()
{
    if (g_files.empty()) {
        MessageBoxW(g_hWnd, L"먼저 [파일 선택...] 버튼으로 파일을 선택하세요.", L"알림", MB_ICONINFORMATION);
        return;
    }

    std::wstring prefix = GetText(IDC_PREFIX) + GetText(IDC_SUFFIX);
    if (prefix.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos) {
        MessageBoxW(g_hWnd, L"접두어/접미어에 다음 문자는 쓸 수 없습니다.\n\\ / : * ? \" < > |", L"오류", MB_ICONERROR);
        return;
    }

    const size_t n = g_files.size();
    std::vector<std::wstring> oldNames, newNames;
    for (size_t i = 0; i < n; ++i) {
        oldNames.push_back(g_files[i].name);
        newNames.push_back(MakeNewName(i));
    }

    // 새 이름끼리 중복 검사
    for (size_t i = 0; i < n; ++i)
        for (size_t j = i + 1; j < n; ++j)
            if (lstrcmpiW(newNames[i].c_str(), newNames[j].c_str()) == 0) {
                MessageBoxW(g_hWnd, (L"새 이름이 중복됩니다: " + newNames[i]).c_str(), L"오류", MB_ICONERROR);
                return;
            }

    // 목록에 없는 다른 파일과 이름이 겹치는지 검사
    for (size_t i = 0; i < n; ++i) {
        if (ContainsNoCase(oldNames, newNames[i])) continue;   // 바뀔 파일 자신이면 괜찮음
        if (GetFileAttributesW(JoinPath(g_folder, newNames[i]).c_str()) != INVALID_FILE_ATTRIBUTES) {
            MessageBoxW(g_hWnd,
                (L"같은 이름의 파일이 폴더에 이미 있습니다:\n" + newNames[i] +
                 L"\n\n접두어나 시작 번호를 바꿔 주세요.").c_str(), L"오류", MB_ICONERROR);
            return;
        }
    }

    std::wstring msg = std::to_wstring(n) + L"개 파일의 이름을 바꿉니다.\n\n" +
                       oldNames[0] + L"  →  " + newNames[0];
    if (n > 1) msg += L"\n...\n" + oldNames[n - 1] + L"  →  " + newNames[n - 1];
    msg += L"\n\n계속할까요?";
    if (MessageBoxW(g_hWnd, msg.c_str(), L"확인", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    std::wstring tag = std::to_wstring(GetTickCount()) + L"_" + std::to_wstring(GetCurrentProcessId());
    std::vector<std::wstring> tmpNames;
    for (size_t i = 0; i < n; ++i)
        tmpNames.push_back(L"~batchrename_" + tag + L"_" + std::to_wstring(i) + L".tmp");

    // 1단계
    for (size_t i = 0; i < n; ++i) {
        if (!MoveFileW(JoinPath(g_folder, oldNames[i]).c_str(), JoinPath(g_folder, tmpNames[i]).c_str())) {
            DWORD err = GetLastError();
            for (size_t j = 0; j < i; ++j)   // 되돌리기
                MoveFileW(JoinPath(g_folder, tmpNames[j]).c_str(), JoinPath(g_folder, oldNames[j]).c_str());
            MessageBoxW(g_hWnd,
                (L"이름을 바꾸지 못했습니다: " + oldNames[i] + L"\n(오류 코드 " + std::to_wstring(err) +
                 L")\n\n파일이 다른 프로그램에서 열려 있는지 확인하세요.\n변경 사항은 모두 되돌렸습니다.").c_str(),
                L"오류", MB_ICONERROR);
            return;
        }
    }

    // 2단계
    for (size_t i = 0; i < n; ++i) {
        if (!MoveFileW(JoinPath(g_folder, tmpNames[i]).c_str(), JoinPath(g_folder, newNames[i]).c_str())) {
            DWORD err = GetLastError();
            for (size_t j = 0; j < i; ++j)
                MoveFileW(JoinPath(g_folder, newNames[j]).c_str(), JoinPath(g_folder, tmpNames[j]).c_str());
            for (size_t j = 0; j < n; ++j)
                MoveFileW(JoinPath(g_folder, tmpNames[j]).c_str(), JoinPath(g_folder, oldNames[j]).c_str());
            MessageBoxW(g_hWnd,
                (L"이름을 바꾸지 못했습니다: " + newNames[i] + L"\n(오류 코드 " + std::to_wstring(err) +
                 L")\n\n변경 사항은 모두 되돌렸습니다.").c_str(), L"오류", MB_ICONERROR);
            return;
        }
    }

    for (size_t i = 0; i < n; ++i) g_files[i].name = newNames[i];
    RefreshList();
    SetStatus(L"완료: " + std::to_wstring(n) + L"개 파일의 이름을 바꿨습니다.");
    MessageBoxW(g_hWnd, (std::to_wstring(n) + L"개 파일의 이름을 바꿨습니다.").c_str(), L"완료", MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------
// 창 만들기 / 배치
// ---------------------------------------------------------------------------
static HWND AddCtl(const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD exStyle = 0)
{
    HWND h = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style,
                             0, 0, 10, 10, g_hWnd, (HMENU)(INT_PTR)id, g_hInst, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return h;
}

static void CreateControls()
{
    AddCtl(L"BUTTON", L"파일 선택...", WS_TABSTOP | BS_PUSHBUTTON, IDC_SELECT);
    AddCtl(L"STATIC", L"(선택된 폴더 없음)", SS_PATHELLIPSIS | SS_CENTERIMAGE, IDC_FOLDER);

    AddCtl(L"STATIC", L"접두어", SS_CENTERIMAGE, IDC_LBL_PREFIX);
    AddCtl(L"EDIT", L"파일_", WS_TABSTOP | ES_AUTOHSCROLL, IDC_PREFIX, WS_EX_CLIENTEDGE);
    AddCtl(L"STATIC", L"접미어", SS_CENTERIMAGE, IDC_LBL_SUFFIX);
    AddCtl(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, IDC_SUFFIX, WS_EX_CLIENTEDGE);
    AddCtl(L"STATIC", L"시작 번호", SS_CENTERIMAGE, IDC_LBL_START);
    AddCtl(L"EDIT", L"1", WS_TABSTOP | ES_NUMBER, IDC_START, WS_EX_CLIENTEDGE);
    AddCtl(L"STATIC", L"자릿수", SS_CENTERIMAGE, IDC_LBL_DIGITS);
    AddCtl(L"EDIT", L"3", WS_TABSTOP | ES_NUMBER, IDC_DIGITS, WS_EX_CLIENTEDGE);
    SendMessageW(Ctl(IDC_DIGITS), EM_SETLIMITTEXT, 2, 0);
    SendMessageW(Ctl(IDC_START), EM_SETLIMITTEXT, 9, 0);
    AddCtl(L"BUTTON", L"확장자 유지", WS_TABSTOP | BS_AUTOCHECKBOX, IDC_KEEPEXT);
    CheckDlgButton(g_hWnd, IDC_KEEPEXT, BST_CHECKED);

    HWND hList = AddCtl(WC_LISTVIEWW, L"", WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS, IDC_LIST, WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    const wchar_t* cols[] = { L"순서", L"현재 이름", L"새 이름" };
    int widths[] = { 50, 260, 260 };
    for (int i = 0; i < 3; ++i) {
        LVCOLUMNW c = {};
        c.mask    = LVCF_TEXT | LVCF_WIDTH;
        c.pszText = (LPWSTR)cols[i];
        c.cx      = widths[i];
        ListView_InsertColumn(hList, i, &c);
    }

    AddCtl(L"BUTTON", L"▲ 위로", WS_TABSTOP, IDC_UP);
    AddCtl(L"BUTTON", L"▼ 아래로", WS_TABSTOP, IDC_DOWN);
    AddCtl(L"BUTTON", L"목록에서 제거", WS_TABSTOP, IDC_REMOVE);
    AddCtl(L"BUTTON", L"이름순 정렬", WS_TABSTOP, IDC_SORTNAME);
    AddCtl(L"BUTTON", L"날짜순 정렬", WS_TABSTOP, IDC_SORTDATE);
    AddCtl(L"BUTTON", L"순서 뒤집기", WS_TABSTOP, IDC_REVERSE);

    AddCtl(L"STATIC", L"파일을 선택하세요. 목록 순서대로 번호가 붙습니다.", SS_CENTERIMAGE | SS_ENDELLIPSIS, IDC_STATUS);
    AddCtl(L"BUTTON", L"이름 바꾸기", WS_TABSTOP | BS_DEFPUSHBUTTON, IDC_RENAME);
    AddCtl(L"BUTTON", L"닫기", WS_TABSTOP, IDC_CLOSE);
}

static void LayoutControls(int cx, int cy)
{
    const int m = 10, h = 26, bw = 120;
    auto mv = [](int id, int x, int y, int w, int hh) { MoveWindow(Ctl(id), x, y, w, hh, TRUE); };

    mv(IDC_SELECT, m, m, bw, 30);
    mv(IDC_FOLDER, m + bw + 10, m, cx - (m + bw + 10) - m, 30);

    int y = 50, x = m;
    mv(IDC_LBL_PREFIX, x, y, 45, h);  x += 45;
    mv(IDC_PREFIX,     x, y, 140, h); x += 150;
    mv(IDC_LBL_SUFFIX, x, y, 45, h);  x += 45;
    mv(IDC_SUFFIX,     x, y, 100, h); x += 110;
    mv(IDC_LBL_START,  x, y, 60, h);  x += 60;
    mv(IDC_START,      x, y, 60, h);  x += 70;
    mv(IDC_LBL_DIGITS, x, y, 45, h);  x += 45;
    mv(IDC_DIGITS,     x, y, 40, h);  x += 55;
    mv(IDC_KEEPEXT,    x, y, 110, h);

    int top = 88, bottom = cy - 50;
    mv(IDC_LIST, m, top, cx - 2 * m - bw - 10, bottom - top);

    int bx = cx - m - bw, by = top;
    int ids[] = { IDC_UP, IDC_DOWN, IDC_REMOVE, IDC_SORTNAME, IDC_SORTDATE, IDC_REVERSE };
    for (int i = 0; i < 6; ++i) {
        mv(ids[i], bx, by, bw, 30);
        by += (i == 2) ? 50 : 36;   // 정렬 버튼 묶음 앞에 간격
    }

    mv(IDC_STATUS, m, cy - 40, cx - 3 * m - 2 * bw - 20, 30);
    mv(IDC_RENAME, cx - m - 2 * bw - 10, cy - 40, bw, 30);
    mv(IDC_CLOSE,  cx - m - bw, cy - 40, bw, 30);
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        g_hWnd = hWnd;
        CreateControls();
        return 0;

    case WM_SIZE:
        LayoutControls(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)lParam)->ptMinTrackSize.x = 860;
        ((MINMAXINFO*)lParam)->ptMinTrackSize.y = 480;
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wParam), code = HIWORD(wParam);
        switch (id) {
        case IDC_SELECT:   SelectFiles();      break;
        case IDC_UP:       MoveSelected(true);  break;
        case IDC_DOWN:     MoveSelected(false); break;
        case IDC_REMOVE:   RemoveSelected();   break;
        case IDC_SORTNAME: SortByName();       break;
        case IDC_SORTDATE: SortByDate();       break;
        case IDC_REVERSE:  ReverseOrder();     break;
        case IDC_RENAME:   DoRename();         break;
        case IDC_CLOSE:    DestroyWindow(hWnd); break;
        case IDC_PREFIX: case IDC_SUFFIX: case IDC_START: case IDC_DIGITS:
            if (code == EN_CHANGE) UpdatePreview();
            break;
        case IDC_KEEPEXT:
            if (code == BN_CLICKED) UpdatePreview();
            break;
        }
        return 0;
    }

    case WM_NOTIFY: {
        NMHDR* nh = (NMHDR*)lParam;
        if (nh->idFrom == IDC_LIST && nh->code == LVN_KEYDOWN) {
            NMLVKEYDOWN* kd = (NMLVKEYDOWN*)lParam;
            if (kd->wVKey == VK_DELETE) RemoveSelected();
            else if (kd->wVKey == 'A' && (GetKeyState(VK_CONTROL) & 0x8000))
                ListView_SetItemState(nh->hwndFrom, -1, LVIS_SELECTED, LVIS_SELECTED);
        }
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow)
{
    g_hInst = hInst;

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    NONCLIENTMETRICSW ncm = {};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g_hFont = CreateFontIndirectW(&ncm.lfMessageFont);

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"BatchRenameWnd";
    RegisterClassExW(&wc);

    HWND hWnd = CreateWindowExW(0, wc.lpszClassName, L"파일 이름 일괄 변경 (Batch Rename)",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 600,
                                nullptr, nullptr, hInst, nullptr);
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(hWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    DeleteObject(g_hFont);
    return (int)msg.wParam;
}
