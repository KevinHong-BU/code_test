#include "BookDialog.h"

#include <algorithm>
#include <string>

#include "ImageLibrary.h"

namespace {

const wchar_t kClassName[] = L"NewsletterBookDialog";

enum {
    IDC_B_TITLE = 100,
    IDC_B_AUTHOR,
    IDC_B_PUBLISHER,
    IDC_B_YEAR,
    IDC_B_CALL,
    IDC_B_DESC,
    IDC_B_COVER,
    IDC_B_COVER_LOAD,
    IDC_B_COVER_CLEAR,
    IDC_B_COVER_INFO,
};

struct State {
    nl::Book book;
    HWND hwnd = nullptr;
    HWND title = nullptr, author = nullptr, publisher = nullptr, year = nullptr, call = nullptr, desc = nullptr;
    HWND coverInfo = nullptr, coverBox = nullptr, coverClear = nullptr;
    HFONT font = nullptr;
    int dpi = 96;
    ImageLibrary* images = nullptr;
    const BookDialog::LoadPicture* loadPicture = nullptr;
    bool done = false;
    bool accepted = false;

    int S(int v) const { return MulDiv(v, dpi, 96); }
};

std::wstring GetText(HWND control) {
    int len = GetWindowTextLengthW(control);
    std::wstring s(static_cast<size_t>(len) + 1, L'\0');
    GetWindowTextW(control, &s[0], len + 1);
    s.resize(static_cast<size_t>(len));
    return s;
}

void UpdateCover(State* st) {
    std::wstring info = st->book.cover.empty()
                            ? L"표지 없음"
                            : std::to_wstring(st->book.coverWidth) + L" × " + std::to_wstring(st->book.coverHeight) +
                                  L" 픽셀";
    SetWindowTextW(st->coverInfo, info.c_str());
    EnableWindow(st->coverClear, !st->book.cover.empty());
    InvalidateRect(st->coverBox, nullptr, TRUE);
}

HWND Make(State* st, const wchar_t* cls, const wchar_t* text, DWORD style, DWORD exStyle, int id) {
    HWND h = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, st->hwnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), FALSE);
    return h;
}

void CreateControls(State* st) {
    const int labelW = st->S(70), rowH = st->S(24), step = st->S(32);
    const int left = st->S(14), right = st->S(446);
    const int fieldX = left + labelW;
    int y = st->S(14);

    auto label = [&](const wchar_t* text, int x, int yy, int w) {
        HWND h = Make(st, L"STATIC", text, SS_LEFT | SS_CENTERIMAGE, 0, -1);
        MoveWindow(h, x, yy, w, rowH, FALSE);
    };
    auto edit = [&](int id, const std::wstring& value, int x, int yy, int w, int h, bool multi) {
        DWORD style = WS_TABSTOP | (multi ? (ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL) : ES_AUTOHSCROLL);
        HWND e = Make(st, L"EDIT", value.c_str(), style, WS_EX_CLIENTEDGE, id);
        MoveWindow(e, x, yy, w, h, FALSE);
        return e;
    };

    label(L"서명", left, y, labelW);
    st->title = edit(IDC_B_TITLE, st->book.title, fieldX, y, right - fieldX, rowH, false);
    y += step;
    label(L"저자", left, y, labelW);
    st->author = edit(IDC_B_AUTHOR, st->book.author, fieldX, y, right - fieldX, rowH, false);
    y += step;
    const int half = (right - fieldX - st->S(8)) / 2;
    label(L"출판사", left, y, labelW);
    st->publisher = edit(IDC_B_PUBLISHER, st->book.publisher, fieldX, y, half, rowH, false);
    label(L"발행연도", fieldX + half + st->S(8), y, st->S(60));
    st->year = edit(IDC_B_YEAR, st->book.year, fieldX + half + st->S(68), y, right - fieldX - half - st->S(68), rowH,
                    false);
    y += step;
    label(L"청구기호", left, y, labelW);
    st->call = edit(IDC_B_CALL, st->book.callNumber, fieldX, y, half, rowH, false);
    y += step;
    label(L"소개", left, y, labelW);
    st->desc = edit(IDC_B_DESC, nl::ToCrLf(st->book.description), fieldX, y, right - fieldX, st->S(84), true);
    y += st->S(84) + st->S(10);

    label(L"표지", left, y, labelW);
    st->coverBox = Make(st, L"STATIC", L"", SS_OWNERDRAW, 0, IDC_B_COVER);
    MoveWindow(st->coverBox, fieldX, y, st->S(78), st->S(104), FALSE);
    const int bx = fieldX + st->S(90);
    HWND load = Make(st, L"BUTTON", L"그림 고르기...", WS_TABSTOP | BS_PUSHBUTTON, 0, IDC_B_COVER_LOAD);
    MoveWindow(load, bx, y, st->S(120), st->S(28), FALSE);
    st->coverClear = Make(st, L"BUTTON", L"표지 지우기", WS_TABSTOP | BS_PUSHBUTTON, 0, IDC_B_COVER_CLEAR);
    MoveWindow(st->coverClear, bx, y + st->S(34), st->S(120), st->S(28), FALSE);
    st->coverInfo = Make(st, L"STATIC", L"", SS_LEFT, 0, IDC_B_COVER_INFO);
    MoveWindow(st->coverInfo, bx, y + st->S(70), right - bx, st->S(40), FALSE);
    y += st->S(104) + st->S(16);

    HWND ok = Make(st, L"BUTTON", L"확인", WS_TABSTOP | BS_DEFPUSHBUTTON, 0, IDOK);
    MoveWindow(ok, right - st->S(186), y, st->S(90), st->S(30), FALSE);
    HWND cancel = Make(st, L"BUTTON", L"취소", WS_TABSTOP | BS_PUSHBUTTON, 0, IDCANCEL);
    MoveWindow(cancel, right - st->S(90), y, st->S(90), st->S(30), FALSE);
    UpdateCover(st);
}

void DrawCover(State* st, const DRAWITEMSTRUCT* item) {
    RECT rc = item->rcItem;
    FillRect(item->hDC, &rc, GetSysColorBrush(COLOR_WINDOW));
    FrameRect(item->hDC, &rc, GetSysColorBrush(COLOR_BTNSHADOW));
    const nl::Book& b = st->book;
    if (b.cover.empty() || b.coverWidth <= 0 || b.coverHeight <= 0 || !st->images) return;
    // 칸 안에 원본 비율대로 맞춰 넣습니다.
    int boxW = rc.right - rc.left - 4, boxH = rc.bottom - rc.top - 4;
    int w = boxW, h = boxW * b.coverHeight / b.coverWidth;
    if (h > boxH) {
        h = boxH;
        w = boxH * b.coverWidth / b.coverHeight;
    }
    RECT dest{rc.left + 2 + (boxW - w) / 2, rc.top + 2 + (boxH - h) / 2, 0, 0};
    dest.right = dest.left + w;
    dest.bottom = dest.top + h;
    st->images->Draw(item->hDC, b.coverId, b.cover, dest, false);
}

bool Accept(State* st) {
    std::wstring title = nl::Trim(GetText(st->title));
    if (title.empty()) {
        MessageBoxW(st->hwnd, L"서명(책 제목)을 입력해 주세요.", L"도서 정보", MB_OK | MB_ICONINFORMATION);
        SetFocus(st->title);
        return false;
    }
    st->book.title = title;
    st->book.author = nl::Trim(GetText(st->author));
    st->book.publisher = nl::Trim(GetText(st->publisher));
    st->book.year = nl::Trim(GetText(st->year));
    st->book.callNumber = nl::Trim(GetText(st->call));
    st->book.description = nl::Trim(nl::NormalizeNewlines(GetText(st->desc)));
    return true;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    State* st = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        st = static_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        st->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
    }
    if (!st) return DefWindowProcW(hwnd, msg, wParam, lParam);

    switch (msg) {
        case WM_CREATE:
            CreateControls(st);
            return 0;
        case DM_GETDEFID:  // IsDialogMessage가 Enter를 [확인]으로 보내도록
            return MAKELRESULT(IDOK, DC_HASDEFID);
        case WM_DRAWITEM: {
            auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (item->CtlID == IDC_B_COVER) {
                DrawCover(st, item);
                return TRUE;
            }
            break;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK:
                    if (Accept(st)) {
                        st->accepted = true;
                        st->done = true;
                    }
                    return 0;
                case IDCANCEL:
                    st->done = true;
                    return 0;
                case IDC_B_COVER_LOAD: {
                    nl::Picture picture;
                    if (st->loadPicture && (*st->loadPicture)(hwnd, &picture)) {
                        st->book.cover = std::move(picture.data);
                        st->book.coverId = picture.id;
                        st->book.coverWidth = picture.width;
                        st->book.coverHeight = picture.height;
                        UpdateCover(st);
                    }
                    return 0;
                }
                case IDC_B_COVER_CLEAR:
                    st->book.cover.clear();
                    st->book.coverWidth = st->book.coverHeight = 0;
                    UpdateCover(st);
                    return 0;
            }
            break;
        case WM_CLOSE:
            st->done = true;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

bool BookDialog::Run(HWND owner, HINSTANCE instance, HFONT font, int dpi, ImageLibrary* images,
                     const LoadPicture& loadPicture, const wchar_t* caption, nl::Book* book) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
        wc.lpszClassName = kClassName;
        registered = RegisterClassExW(&wc) != 0;
    }

    State st;
    st.book = *book;
    st.font = font;
    st.dpi = dpi;
    st.images = images;
    st.loadPicture = &loadPicture;

    // 주인 창 가운데에 놓습니다.
    RECT client{0, 0, st.S(460), st.S(424)};
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectEx(&client, style, FALSE, WS_EX_DLGMODALFRAME);
    int w = client.right - client.left, h = client.bottom - client.top;
    RECT ownerRect;
    GetWindowRect(owner, &ownerRect);
    int x = ownerRect.left + (ownerRect.right - ownerRect.left - w) / 2;
    int y = ownerRect.top + (ownerRect.bottom - ownerRect.top - h) / 2;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, kClassName, caption, style, x, y, w, h, owner,
                               nullptr, instance, &st);
    if (!dlg) return false;

    // 모달 루프: 대화 상자가 닫힐 때까지 주인 창을 막아 둡니다.
    EnableWindow(owner, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetFocus(st.title);
    SendMessageW(st.title, EM_SETSEL, 0, -1);
    MSG msg;
    bool quit = false;
    while (!st.done) {
        if (GetMessageW(&msg, nullptr, 0, 0) <= 0) {
            quit = true;
            break;
        }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    DestroyWindow(dlg);
    if (quit) PostQuitMessage(static_cast<int>(msg.wParam));

    if (st.accepted) *book = st.book;
    return st.accepted;
}
