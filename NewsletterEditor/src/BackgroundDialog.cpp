#include "BackgroundDialog.h"

#include <algorithm>
#include <string>

#include "ImageLibrary.h"

namespace {

const wchar_t kClassName[] = L"NewsletterBackgroundDialog";

enum {
    IDC_G_SEPARATE = 100,
    IDC_G_PREVIEW,
    IDC_G_LOAD,
    IDC_G_CLEAR,
    IDC_G_FIT,
    IDC_G_OPACITY,
    IDC_G_INFO,
};

const int kOpacities[] = {100, 70, 50, 30, 15};

struct State {
    nl::Background bg;
    bool forPage = false;
    bool separate = true;
    HWND hwnd = nullptr;
    HWND separateCheck = nullptr, preview = nullptr, load = nullptr, clear = nullptr, fit = nullptr, opacity = nullptr,
         info = nullptr, fitLabel = nullptr, opacityLabel = nullptr;
    HFONT font = nullptr;
    int dpi = 96;
    ImageLibrary* images = nullptr;
    const BookDialog::LoadPicture* loadPicture = nullptr;
    bool done = false;
    bool accepted = false;

    int S(int v) const { return MulDiv(v, dpi, 96); }
};

HWND Make(State* st, const wchar_t* cls, const wchar_t* text, DWORD style, int id, int x, int y, int w, int h) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, st->hwnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(st->font), FALSE);
    return c;
}

void UpdateControls(State* st) {
    bool enabled = !st->forPage || st->separate;
    bool hasPicture = !st->bg.data.empty();
    EnableWindow(st->load, enabled);
    EnableWindow(st->clear, enabled && hasPicture);
    EnableWindow(st->fit, enabled && hasPicture);
    EnableWindow(st->opacity, enabled && hasPicture);
    EnableWindow(st->fitLabel, enabled && hasPicture);
    EnableWindow(st->opacityLabel, enabled && hasPicture);
    std::wstring info;
    if (!enabled) info = L"모든 쪽 배경을 그대로 씁니다.";
    else if (!hasPicture) info = st->forPage ? L"그림 없음: 이 쪽은 배경 없이 인쇄됩니다." : L"그림 없음";
    else info = std::to_wstring(st->bg.width) + L" × " + std::to_wstring(st->bg.height) + L" 픽셀";
    SetWindowTextW(st->info, info.c_str());
    InvalidateRect(st->preview, nullptr, TRUE);
}

void CreateControls(State* st) {
    const int left = st->S(14);
    int y = st->S(14);
    if (st->forPage) {
        st->separateCheck = Make(st, L"BUTTON", L"이 쪽은 모든 쪽 배경 대신 따로 설정", WS_TABSTOP | BS_AUTOCHECKBOX,
                                 IDC_G_SEPARATE, left, y, st->S(380), st->S(24));
        SendMessageW(st->separateCheck, BM_SETCHECK, st->separate ? BST_CHECKED : BST_UNCHECKED, 0);
        y += st->S(32);
    }
    // 왼쪽: A4 비율 미리보기
    const int pw = st->S(150), ph = st->S(212);
    st->preview = Make(st, L"STATIC", L"", SS_OWNERDRAW, IDC_G_PREVIEW, left, y, pw, ph);

    // 오른쪽: 그림 고르기·맞춤·진하기
    const int rx = left + pw + st->S(16), rw = st->S(220), rowH = st->S(26);
    int ry = y;
    st->load = Make(st, L"BUTTON", L"그림 고르기...", WS_TABSTOP | BS_PUSHBUTTON, IDC_G_LOAD, rx, ry, st->S(108), rowH + 2);
    st->clear = Make(st, L"BUTTON", L"그림 지우기", WS_TABSTOP | BS_PUSHBUTTON, IDC_G_CLEAR, rx + st->S(112), ry,
                     st->S(108), rowH + 2);
    ry += st->S(40);
    st->fitLabel = Make(st, L"STATIC", L"맞춤", SS_LEFT | SS_CENTERIMAGE, -1, rx, ry, st->S(50), rowH);
    st->fit = Make(st, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, IDC_G_FIT, rx + st->S(50), ry,
                   rw - st->S(50), st->S(200));
    for (const wchar_t* text : {L"채우기 (넘치는 곳 자름)", L"늘이기", L"맞추기 (전체 보이기)", L"바둑판"})
        SendMessageW(st->fit, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text));
    int fitIndex = st->bg.fit == nl::BackgroundFit::Stretch   ? 1
                   : st->bg.fit == nl::BackgroundFit::Contain ? 2
                   : st->bg.fit == nl::BackgroundFit::Tile    ? 3
                                                              : 0;
    SendMessageW(st->fit, CB_SETCURSEL, fitIndex, 0);
    ry += st->S(36);
    st->opacityLabel = Make(st, L"STATIC", L"진하기", SS_LEFT | SS_CENTERIMAGE, -1, rx, ry, st->S(50), rowH);
    st->opacity = Make(st, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, IDC_G_OPACITY, rx + st->S(50),
                       ry, rw - st->S(50), st->S(200));
    int opacityIndex = -1;
    for (int i = 0; i < static_cast<int>(sizeof(kOpacities) / sizeof(kOpacities[0])); ++i) {
        std::wstring text = std::to_wstring(kOpacities[i]) + L"%";
        if (kOpacities[i] == 100) text += L" (원래대로)";
        if (kOpacities[i] == 15) text += L" (아주 흐리게)";
        int index = static_cast<int>(SendMessageW(st->opacity, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str())));
        SendMessageW(st->opacity, CB_SETITEMDATA, index, kOpacities[i]);
        if (kOpacities[i] == st->bg.opacity) opacityIndex = index;
    }
    if (opacityIndex < 0) {  // 파일에 목록에 없는 값이 있으면 그 값도 보여 줌
        std::wstring text = std::to_wstring(st->bg.opacity) + L"%";
        opacityIndex = static_cast<int>(SendMessageW(st->opacity, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str())));
        SendMessageW(st->opacity, CB_SETITEMDATA, opacityIndex, st->bg.opacity);
    }
    SendMessageW(st->opacity, CB_SETCURSEL, opacityIndex, 0);
    ry += st->S(36);
    st->info = Make(st, L"STATIC", L"", SS_LEFT, IDC_G_INFO, rx, ry, rw, st->S(46));
    Make(st, L"STATIC", L"글자가 잘 읽히도록 진하기를 30% 안팎으로 두는 것을 권합니다.", SS_LEFT, -1, rx,
         y + ph - st->S(44), rw, st->S(44));

    const int by = y + ph + st->S(16);
    const int right = rx + rw;
    Make(st, L"BUTTON", L"확인", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK, right - st->S(186), by, st->S(90), st->S(30));
    Make(st, L"BUTTON", L"취소", WS_TABSTOP | BS_PUSHBUTTON, IDCANCEL, right - st->S(90), by, st->S(90), st->S(30));
    UpdateControls(st);
}

void DrawPreview(State* st, const DRAWITEMSTRUCT* item) {
    HDC dc = item->hDC;
    RECT rc = item->rcItem;
    FillRect(dc, &rc, GetSysColorBrush(COLOR_BTNFACE));
    RECT page = rc;
    InflateRect(&page, -2, -2);
    FillRect(dc, &page, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    bool enabled = !st->forPage || st->separate;
    if (enabled && st->images && !st->bg.data.empty()) st->images->DrawBackground(dc, st->bg, page, false);
    // 글자 대신 회색 줄을 몇 개 그려, 배경 위에서 글이 얼마나 잘 보일지 가늠하게 합니다.
    HBRUSH textBrush = CreateSolidBrush(RGB(90, 90, 90));
    const int w = page.right - page.left;
    for (int i = 0; i < 9; ++i) {
        int y = page.top + (page.bottom - page.top) * (8 + i * 9) / 100;
        int lineRight = page.left + w * (i % 3 == 2 ? 60 : 88) / 100;
        RECT line{page.left + w * 12 / 100, y, lineRight, y + std::max(2, w / 60)};
        FillRect(dc, &line, textBrush);
    }
    DeleteObject(textBrush);
    FrameRect(dc, &page, GetSysColorBrush(COLOR_BTNSHADOW));
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
        case DM_GETDEFID:
            return MAKELRESULT(IDOK, DC_HASDEFID);
        case WM_DRAWITEM: {
            auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (item->CtlID == IDC_G_PREVIEW) {
                DrawPreview(st, item);
                return TRUE;
            }
            break;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDOK:
                    st->accepted = true;
                    st->done = true;
                    return 0;
                case IDCANCEL:
                    st->done = true;
                    return 0;
                case IDC_G_SEPARATE:
                    st->separate = SendMessageW(st->separateCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
                    UpdateControls(st);
                    return 0;
                case IDC_G_LOAD: {
                    nl::Picture picture;
                    if (st->loadPicture && (*st->loadPicture)(hwnd, &picture)) {
                        st->bg.data = std::move(picture.data);
                        st->bg.id = picture.id;
                        st->bg.width = picture.width;
                        st->bg.height = picture.height;
                        UpdateControls(st);
                    }
                    return 0;
                }
                case IDC_G_CLEAR:
                    st->bg.data.clear();
                    st->bg.width = st->bg.height = 0;
                    UpdateControls(st);
                    return 0;
                case IDC_G_FIT:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        int sel = static_cast<int>(SendMessageW(st->fit, CB_GETCURSEL, 0, 0));
                        st->bg.fit = sel == 1   ? nl::BackgroundFit::Stretch
                                     : sel == 2 ? nl::BackgroundFit::Contain
                                     : sel == 3 ? nl::BackgroundFit::Tile
                                                : nl::BackgroundFit::Cover;
                        UpdateControls(st);
                    }
                    return 0;
                case IDC_G_OPACITY:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        int sel = static_cast<int>(SendMessageW(st->opacity, CB_GETCURSEL, 0, 0));
                        if (sel >= 0) st->bg.opacity = static_cast<int>(SendMessageW(st->opacity, CB_GETITEMDATA, sel, 0));
                        UpdateControls(st);
                    }
                    return 0;
            }
            break;
        case WM_CTLCOLORSTATIC:
            if (reinterpret_cast<HWND>(lParam) == st->preview) return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
            break;
        case WM_CLOSE:
            st->done = true;
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

bool BackgroundDialog::Run(HWND owner, HINSTANCE instance, HFONT font, int dpi, ImageLibrary* images,
                           const BookDialog::LoadPicture& loadPicture, int page, bool* separate,
                           nl::Background* background) {
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
    st.bg = *background;
    st.forPage = page >= 0;
    st.separate = !st.forPage || *separate;
    st.font = font;
    st.dpi = dpi;
    st.images = images;
    st.loadPicture = &loadPicture;

    std::wstring caption = page < 0 ? L"모든 쪽 배경 그림" : std::to_wstring(page + 1) + L"쪽 배경 그림";
    RECT client{0, 0, st.S(414), st.S(page < 0 ? 288 : 320)};
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AdjustWindowRectEx(&client, style, FALSE, WS_EX_DLGMODALFRAME);
    int w = client.right - client.left, h = client.bottom - client.top;
    RECT ownerRect;
    GetWindowRect(owner, &ownerRect);
    int x = ownerRect.left + (ownerRect.right - ownerRect.left - w) / 2;
    int y = ownerRect.top + (ownerRect.bottom - ownerRect.top - h) / 2;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, kClassName, caption.c_str(), style, x, y, w,
                               h, owner, nullptr, instance, &st);
    if (!dlg) return false;

    EnableWindow(owner, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetFocus(st.forPage ? st.separateCheck : st.load);
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

    if (st.accepted) {
        *background = st.bg;
        if (st.forPage) *separate = st.separate;
    }
    return st.accepted;
}
