#include "PreviewWindow.h"

#include <windowsx.h>

#include <algorithm>
#include <climits>
#include <cmath>

namespace {

const wchar_t kClassName[] = L"NewsletterPreview";
const int kGap = 20;  // 쪽 사이 여백(픽셀)
const double kMinZoom = 0.25;
const double kMaxZoom = 3.0;
const COLORREF kBackground = RGB(206, 210, 216);
const COLORREF kShadow = RGB(160, 164, 170);

}  // namespace

bool PreviewWindow::Register(HINSTANCE instance) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    return RegisterClassExW(&wc) != 0;
}

HWND PreviewWindow::Create(HWND parent, int id, HINSTANCE instance) {
    hwnd_ = CreateWindowExW(WS_EX_CLIENTEDGE, kClassName, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
                            WS_TABSTOP, 0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                            instance, this);
    return hwnd_;
}

void PreviewWindow::SetDocument(const nl::Newsletter* doc) {
    doc_ = doc;
    scrollX_ = scrollY_ = 0;
    Refresh();
}

void PreviewWindow::Refresh() {
    int oldPages = renderer_.PageCount();
    if (doc_) renderer_.Layout(*doc_);
    if (!hwnd_) return;
    UpdateScrollBars();
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (oldPages != renderer_.PageCount()) NotifyParent();
}

double PreviewWindow::Scale() const {
    if (zoom_ > 0) return zoom_;
    RECT rc{};
    if (hwnd_) GetClientRect(hwnd_, &rc);
    double fit = (rc.right - 2.0 * kGap) / nl::kPageWidth;
    return std::clamp(fit, kMinZoom, 1.5);
}

int PreviewWindow::ZoomPercent() const { return static_cast<int>(std::lround(Scale() * 100)); }

void PreviewWindow::SetZoom(double zoom) {
    double oldScale = Scale();
    zoom_ = zoom > 0 ? std::clamp(zoom, kMinZoom, kMaxZoom) : 0.0;
    // 확대/축소 후에도 보던 위치를 대략 유지합니다.
    double ratio = Scale() / oldScale;
    scrollX_ = static_cast<int>(scrollX_ * ratio);
    scrollY_ = static_cast<int>(scrollY_ * ratio);
    UpdateScrollBars();
    InvalidateRect(hwnd_, nullptr, FALSE);
    NotifyParent();
}

void PreviewWindow::ZoomIn() { SetZoom(Scale() * 1.25); }
void PreviewWindow::ZoomOut() { SetZoom(Scale() / 1.25); }
void PreviewWindow::ZoomFit() { SetZoom(0.0); }

void PreviewWindow::NotifyParent() {
    if (hwnd_) PostMessageW(GetParent(hwnd_), WM_APP_PREVIEW_CHANGED, 0, 0);
}

void PreviewWindow::UpdateScrollBars() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    double scale = Scale();
    int pageW = static_cast<int>(std::lround(nl::kPageWidth * scale));
    int pageH = static_cast<int>(std::lround(nl::kPageHeight * scale));
    int pages = std::max(1, renderer_.PageCount());
    int contentW = pageW + 2 * kGap;
    int contentH = kGap + pages * (pageH + kGap);

    scrollX_ = std::clamp(scrollX_, 0, std::max(0, contentW - static_cast<int>(rc.right)));
    scrollY_ = std::clamp(scrollY_, 0, std::max(0, contentH - static_cast<int>(rc.bottom)));

    SCROLLINFO si{sizeof(si)};
    // 세로 막대는 항상 보이게 해서, 막대가 생겼다 사라지며 너비 맞춤 배율이 흔들리지 않게 합니다.
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = contentH - 1;
    si.nPage = rc.bottom;
    si.nPos = scrollY_;
    SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMax = contentW - 1;
    si.nPage = rc.right;
    si.nPos = scrollX_;
    SetScrollInfo(hwnd_, SB_HORZ, &si, TRUE);
}

void PreviewWindow::ScrollTo(int x, int y) {
    scrollX_ = x;
    scrollY_ = y;
    UpdateScrollBars();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PreviewWindow::OnScroll(int bar, int code) {
    SCROLLINFO si{sizeof(si)};
    si.fMask = SIF_ALL;
    GetScrollInfo(hwnd_, bar, &si);
    int pos = si.nPos;
    const int line = 40;
    switch (code) {
        case SB_LINEUP: pos -= line; break;
        case SB_LINEDOWN: pos += line; break;
        case SB_PAGEUP: pos -= static_cast<int>(si.nPage); break;
        case SB_PAGEDOWN: pos += static_cast<int>(si.nPage); break;
        case SB_TOP: pos = 0; break;
        case SB_BOTTOM: pos = si.nMax; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        default: return;
    }
    if (bar == SB_VERT) ScrollTo(scrollX_, pos);
    else ScrollTo(pos, scrollY_);
}

void PreviewWindow::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int w = std::max(1L, rc.right), h = std::max(1L, rc.bottom);

    // 깜빡임을 없애기 위해 메모리 비트맵에 그린 뒤 한 번에 복사합니다.
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(kBackground);
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    double scale = Scale();
    int pageW = static_cast<int>(std::lround(nl::kPageWidth * scale));
    int pageH = static_cast<int>(std::lround(nl::kPageHeight * scale));
    int x0 = (pageW + 2 * kGap <= w) ? (w - pageW) / 2 : kGap - scrollX_;

    HBRUSH shadow = CreateSolidBrush(kShadow);
    for (int p = 0; p < renderer_.PageCount(); ++p) {
        int y0 = kGap + p * (pageH + kGap) - scrollY_;
        if (y0 > h || y0 + pageH < 0) continue;
        RECT sh{x0 + 4, y0 + 4, x0 + pageW + 4, y0 + pageH + 4};
        FillRect(mem, &sh, shadow);

        int saved = SaveDC(mem);
        nl::SetupPageMapping(mem, scale);
        SetViewportOrgEx(mem, x0, y0, nullptr);
        renderer_.DrawPage(mem, p);
        RestoreDC(mem, saved);
    }
    DeleteObject(shadow);

    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd_, &ps);
}

LRESULT CALLBACK PreviewWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PreviewWindow* self;
    if (msg == WM_NCCREATE) {
        self = static_cast<PreviewWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<PreviewWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT PreviewWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            OnPaint();
            return 0;
        case WM_SIZE:
            UpdateScrollBars();
            if (zoom_ == 0) NotifyParent();
            return 0;
        case WM_VSCROLL:
            OnScroll(SB_VERT, LOWORD(wParam));
            return 0;
        case WM_HSCROLL:
            OnScroll(SB_HORZ, LOWORD(wParam));
            return 0;
        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            if (GET_KEYSTATE_WPARAM(wParam) & MK_CONTROL) {
                if (delta > 0) ZoomIn();
                else ZoomOut();
            } else {
                ScrollTo(scrollX_, scrollY_ - delta * 100 / WHEEL_DELTA);
            }
            return 0;
        }
        case WM_LBUTTONDOWN:
            SetFocus(hwnd_);
            return 0;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS;
        case WM_KEYDOWN: {
            RECT rc;
            GetClientRect(hwnd_, &rc);
            switch (wParam) {
                case VK_UP: ScrollTo(scrollX_, scrollY_ - 40); break;
                case VK_DOWN: ScrollTo(scrollX_, scrollY_ + 40); break;
                case VK_LEFT: ScrollTo(scrollX_ - 40, scrollY_); break;
                case VK_RIGHT: ScrollTo(scrollX_ + 40, scrollY_); break;
                case VK_PRIOR: ScrollTo(scrollX_, scrollY_ - rc.bottom); break;
                case VK_NEXT: ScrollTo(scrollX_, scrollY_ + rc.bottom); break;
                case VK_HOME: ScrollTo(scrollX_, 0); break;
                case VK_END: ScrollTo(scrollX_, INT_MAX / 2); break;
                default: return DefWindowProcW(hwnd_, msg, wParam, lParam);
            }
            return 0;
        }
        case WM_NCDESTROY: {
            HWND hwnd = hwnd_;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            hwnd_ = nullptr;
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}
