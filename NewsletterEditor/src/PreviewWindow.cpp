#include "PreviewWindow.h"

#include <windowsx.h>

#include <algorithm>
#include <climits>
#include <cmath>

#include "ImageLibrary.h"
#include "resource.h"

namespace {

const wchar_t kClassName[] = L"NewsletterPreview";
const int kGap = 20;  // 쪽 사이 여백(픽셀)
const double kMinZoom = 0.25;
const double kMaxZoom = 3.0;
const int kMinPictureSize = 16;  // 그림 최소 크기(논리 단위)
const int kHandleSize = 8;       // 크기 조절 손잡이(픽셀)
const COLORREF kBackground = RGB(206, 210, 216);
const COLORREF kShadow = RGB(160, 164, 170);
const COLORREF kSelection = RGB(0, 120, 215);

// 손잡이 번호: 0 왼쪽위, 1 위, 2 오른쪽위, 3 오른쪽, 4 오른쪽아래, 5 아래, 6 왼쪽아래, 7 왼쪽
bool MovesLeft(int h) { return h == 0 || h == 6 || h == 7; }
bool MovesRight(int h) { return h == 2 || h == 3 || h == 4; }
bool MovesTop(int h) { return h == 0 || h == 1 || h == 2; }
bool MovesBottom(int h) { return h == 4 || h == 5 || h == 6; }
bool IsCorner(int h) { return h % 2 == 0; }

POINT HandlePoint(const RECT& r, int h) {
    int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
    const POINT pts[8] = {{r.left, r.top},     {cx, r.top},     {r.right, r.top}, {r.right, cy},
                          {r.right, r.bottom}, {cx, r.bottom}, {r.left, r.bottom}, {r.left, cy}};
    return pts[h];
}

}  // namespace

bool PreviewWindow::Register(HINSTANCE instance) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = nullptr;  // WM_SETCURSOR에서 상황에 맞게 정함
    wc.lpszClassName = kClassName;
    return RegisterClassExW(&wc) != 0;
}

HWND PreviewWindow::Create(HWND parent, int id, HINSTANCE instance) {
    HDC screen = GetDC(nullptr);
    int dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);
    bannerFont_ = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                              L"맑은 고딕");
    hwnd_ = CreateWindowExW(WS_EX_CLIENTEDGE, kClassName, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL |
                            WS_TABSTOP, 0, 0, 0, 0, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                            instance, this);
    return hwnd_;
}

void PreviewWindow::SetDocument(nl::Newsletter* doc, ImageLibrary* images) {
    doc_ = doc;
    images_ = images;
    scrollX_ = scrollY_ = 0;
    selected_ = -1;
    titleSelected_ = false;
    placing_ = false;
    drag_ = Drag::None;
    Refresh();
    NotifyParent();
}

void PreviewWindow::Refresh() {
    int oldPages = renderer_.PageCount();
    if (doc_) {
        renderer_.Layout(*doc_);
        if (selected_ >= static_cast<int>(doc_->pictures.size())) selected_ = -1;
    }
    if (!hwnd_) return;
    UpdateScrollBars();
    InvalidateRect(hwnd_, nullptr, FALSE);
    if (oldPages != renderer_.PageCount()) NotifyParent();
}

// ---------------------------------------------------------------------------
// 좌표 변환

double PreviewWindow::Scale() const {
    if (zoom_ > 0) return zoom_;
    RECT rc{};
    if (hwnd_) GetClientRect(hwnd_, &rc);
    double fit = (rc.right - 2.0 * kGap) / nl::kPageWidth;
    return std::clamp(fit, kMinZoom, 1.5);
}

int PreviewWindow::PageWidthPx() const { return static_cast<int>(std::lround(nl::kPageWidth * Scale())); }
int PreviewWindow::PageHeightPx() const { return static_cast<int>(std::lround(nl::kPageHeight * Scale())); }

int PreviewWindow::PageLeftPx() const {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int pageW = PageWidthPx();
    return (pageW + 2 * kGap <= rc.right) ? (rc.right - pageW) / 2 : kGap - scrollX_;
}

int PreviewWindow::PageTopPx(int page) const { return kGap + page * (PageHeightPx() + kGap) - scrollY_; }

int PreviewWindow::PageAt(int deviceY) const {
    int p = static_cast<int>(std::floor(static_cast<double>(deviceY + scrollY_ - kGap / 2) / (PageHeightPx() + kGap)));
    return std::clamp(p, 0, std::max(0, renderer_.PageCount() - 1));
}

POINT PreviewWindow::ToDevice(int page, double x, double y) const {
    double s = Scale();
    return POINT{PageLeftPx() + static_cast<int>(std::lround(x * s)),
                 PageTopPx(page) + static_cast<int>(std::lround(y * s))};
}

void PreviewWindow::ToLogical(POINT pt, int page, double* x, double* y) const {
    double s = Scale();
    *x = (pt.x - PageLeftPx()) / s;
    *y = (pt.y - PageTopPx(page)) / s;
}

RECT PreviewWindow::PictureDeviceRect(const nl::Picture& p) const {
    POINT a = ToDevice(p.page, p.x, p.y);
    POINT b = ToDevice(p.page, p.x + p.width, p.y + p.height);
    return RECT{a.x, a.y, b.x, b.y};
}

int PreviewWindow::HitPicture(POINT pt) const {
    if (!doc_) return -1;
    for (int i = static_cast<int>(doc_->pictures.size()) - 1; i >= 0; --i) {
        RECT r = PictureDeviceRect(doc_->pictures[i]);
        if (PtInRect(&r, pt)) return i;
    }
    return -1;
}

int PreviewWindow::HitHandle(POINT pt) const {
    if (!doc_ || selected_ < 0) return -1;
    RECT r = PictureDeviceRect(doc_->pictures[selected_]);
    for (int h = 0; h < 8; ++h) {
        POINT c = HandlePoint(r, h);
        if (std::abs(pt.x - c.x) <= kHandleSize && std::abs(pt.y - c.y) <= kHandleSize) return h;
    }
    return -1;
}

RECT PreviewWindow::TitleDeviceRect() const {
    RECT r = renderer_.TitleRect();
    if (IsRectEmpty(&r)) return r;
    POINT a = ToDevice(0, r.left, r.top);
    POINT b = ToDevice(0, r.right, r.bottom);
    return RECT{a.x - 4, a.y - 3, b.x + 4, b.y + 3};
}

bool PreviewWindow::HitTitle(POINT pt) const {
    RECT r = TitleDeviceRect();
    return doc_ && !IsRectEmpty(&r) && PtInRect(&r, pt);
}

void PreviewWindow::MoveTitle(int dx, int dy) {
    doc_->titleOffsetX = std::clamp(titleOrigX_ + dx, -nl::kMaxTitleOffset, nl::kMaxTitleOffset);
    doc_->titleOffsetY = std::clamp(titleOrigY_ + dy, -16, nl::kMaxTitleOffset);
    Refresh();
    // 색 띠 끝에 닿아 덜 움직였으면 실제 적용된 값으로 맞춰 둡니다(되돌아올 때 헛도는 구간이 없도록).
    doc_->titleOffsetX = renderer_.TitleOffsetX();
    doc_->titleOffsetY = renderer_.TitleOffsetY();
}

// ---------------------------------------------------------------------------
// 확대/축소, 스크롤

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

void PreviewWindow::NotifyEdited() {
    if (hwnd_) PostMessageW(GetParent(hwnd_), WM_APP_DOC_EDITED, 0, 0);
}

void PreviewWindow::UpdateScrollBars() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int pages = std::max(1, renderer_.PageCount());
    int contentW = PageWidthPx() + 2 * kGap;
    int contentH = kGap + pages * (PageHeightPx() + kGap);

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

// ---------------------------------------------------------------------------
// 그리기

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
    int pageW = PageWidthPx(), pageH = PageHeightPx();
    int x0 = PageLeftPx();

    HBRUSH shadow = CreateSolidBrush(kShadow);
    for (int p = 0; doc_ && p < renderer_.PageCount(); ++p) {
        int y0 = PageTopPx(p);
        if (y0 > h || y0 + pageH < 0) continue;
        RECT sh{x0 + 4, y0 + 4, x0 + pageW + 4, y0 + pageH + 4};
        FillRect(mem, &sh, shadow);

        int saved = SaveDC(mem);
        nl::SetupPageMapping(mem, scale);
        SetViewportOrgEx(mem, x0, y0, nullptr);
        renderer_.DrawPage(mem, p, *doc_, images_, false);
        RestoreDC(mem, saved);
    }
    DeleteObject(shadow);

    DrawOverlay(mem);

    BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd_, &ps);
}

// 선택 테두리·손잡이, 넣을 영역, 안내 띠 (화면에만 보이고 인쇄되지 않음)
void PreviewWindow::DrawOverlay(HDC hdc) {
    HPEN pen = CreatePen(PS_SOLID, 1, kSelection);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));

    if (doc_ && selected_ >= 0) {
        RECT r = PictureDeviceRect(doc_->pictures[selected_]);
        Rectangle(hdc, r.left - 1, r.top - 1, r.right + 1, r.bottom + 1);
        SelectObject(hdc, GetStockObject(WHITE_BRUSH));
        for (int i = 0; i < 8; ++i) {
            POINT c = HandlePoint(r, i);
            Rectangle(hdc, c.x - kHandleSize / 2, c.y - kHandleSize / 2, c.x + kHandleSize / 2 + 1,
                      c.y + kHandleSize / 2 + 1);
        }
    }

    if (doc_ && titleSelected_) {
        RECT r = TitleDeviceRect();
        HPEN dashTitle = CreatePen(PS_DOT, 1, kSelection);
        SelectObject(hdc, dashTitle);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        SetBkMode(hdc, TRANSPARENT);
        Rectangle(hdc, r.left, r.top, r.right, r.bottom);
        SelectObject(hdc, pen);
        DeleteObject(dashTitle);
    }

    if (drag_ == Drag::Place) {
        HPEN dash = CreatePen(PS_DOT, 1, kSelection);
        SelectObject(hdc, dash);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        SetBkMode(hdc, TRANSPARENT);
        Rectangle(hdc, std::min(placeStart_.x, placeNow_.x), std::min(placeStart_.y, placeNow_.y),
                  std::max(placeStart_.x, placeNow_.x) + 1, std::max(placeStart_.y, placeNow_.y) + 1);
        SelectObject(hdc, pen);
        DeleteObject(dash);
    }
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    if (placing_) {
        RECT rc;
        GetClientRect(hwnd_, &rc);
        RECT band{0, 0, rc.right, MulDiv(32, GetDeviceCaps(hdc, LOGPIXELSY), 96)};
        HBRUSH b = CreateSolidBrush(RGB(255, 244, 200));
        FillRect(hdc, &band, b);
        DeleteObject(b);
        HGDIOBJ oldFont = SelectObject(hdc, bannerFont_);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(90, 70, 0));
        const wchar_t* text = L"그림을 넣을 영역을 마우스로 끌어서 지정하세요 (그냥 누르면 기본 크기 · Esc 취소)";
        DrawTextW(hdc, text, -1, &band, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(hdc, oldFont);
    }
}

// ---------------------------------------------------------------------------
// 그림 편집

void PreviewWindow::BeginPlacing(const nl::Picture& picture) {
    pending_ = picture;
    placing_ = true;
    selected_ = -1;
    titleSelected_ = false;
    drag_ = Drag::None;
    SetFocus(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
    NotifyParent();
}

void PreviewWindow::CancelInteraction() {
    if (drag_ != Drag::None) {
        Drag cancelled = drag_;
        drag_ = Drag::None;  // ReleaseCapture가 보내는 WM_CAPTURECHANGED에서 다시 들어오지 않게 먼저 지움
        if (GetCapture() == hwnd_) ReleaseCapture();
        if ((cancelled == Drag::Move || cancelled == Drag::Resize) && selected_ >= 0) {
            doc_->pictures[selected_] = original_;  // 끌던 것을 되돌림
            Refresh();
        } else if (cancelled == Drag::Title) {
            doc_->titleOffsetX = titleOrigX_;
            doc_->titleOffsetY = titleOrigY_;
            Refresh();
        }
    } else if (placing_) {
        placing_ = false;
    } else {
        selected_ = -1;
        titleSelected_ = false;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    NotifyParent();
}

void PreviewWindow::SelectPicture(int index) {
    if (!doc_ || index < 0 || index >= static_cast<int>(doc_->pictures.size())) index = -1;
    selected_ = index;
    if (index >= 0) titleSelected_ = false;
    if (index >= 0) {
        RECT rc;
        GetClientRect(hwnd_, &rc);
        RECT r = PictureDeviceRect(doc_->pictures[index]);
        if (r.top < 0 || r.bottom > rc.bottom) ScrollTo(scrollX_, scrollY_ + r.top - 40);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    NotifyParent();
}

void PreviewWindow::DeleteSelected() {
    if (!doc_ || selected_ < 0) return;
    doc_->pictures.erase(doc_->pictures.begin() + selected_);
    selected_ = -1;
    Refresh();
    NotifyParent();
    NotifyEdited();
}

void PreviewWindow::ResetAspectRatio() {
    if (!doc_ || selected_ < 0 || !images_) return;
    nl::Picture& p = doc_->pictures[selected_];
    int pw = 0, ph = 0;
    if (!images_->PixelSize(p, &pw, &ph) || pw <= 0 || ph <= 0) return;
    // 폭은 그대로 두고 높이를 원본 비율에 맞춥니다.
    p.height = std::max(kMinPictureSize, static_cast<int>(std::lround(p.width * static_cast<double>(ph) / pw)));
    Refresh();
    NotifyEdited();
}

void PreviewWindow::ToggleWrap() {
    if (!doc_ || selected_ < 0) return;
    doc_->pictures[selected_].wrapText = !doc_->pictures[selected_].wrapText;
    Refresh();
    NotifyParent();
    NotifyEdited();
}

void PreviewWindow::BringToFront() {
    if (!doc_ || selected_ < 0) return;
    nl::Picture p = doc_->pictures[selected_];
    doc_->pictures.erase(doc_->pictures.begin() + selected_);
    doc_->pictures.push_back(p);
    selected_ = static_cast<int>(doc_->pictures.size()) - 1;
    Refresh();
    NotifyEdited();
}

void PreviewWindow::SendToBack() {
    if (!doc_ || selected_ < 0) return;
    nl::Picture p = doc_->pictures[selected_];
    doc_->pictures.erase(doc_->pictures.begin() + selected_);
    doc_->pictures.insert(doc_->pictures.begin(), p);
    selected_ = 0;
    Refresh();
    NotifyEdited();
}

void PreviewWindow::Nudge(int dx, int dy) {
    nl::Picture& p = doc_->pictures[selected_];
    p.x = std::clamp(p.x + dx, 0, std::max(0, nl::kPageWidth - p.width));
    p.y = std::clamp(p.y + dy, 0, std::max(0, nl::kPageHeight - p.height));
    Refresh();
    NotifyEdited();
}

// ---------------------------------------------------------------------------
// 마우스

void PreviewWindow::OnMouseDown(POINT pt, WPARAM) {
    SetFocus(hwnd_);
    if (!doc_) return;

    if (placing_) {
        drag_ = Drag::Place;
        placePage_ = PageAt(pt.y);
        placeStart_ = placeNow_ = pt;
        SetCapture(hwnd_);
        return;
    }

    int handle = HitHandle(pt);
    if (handle >= 0) {
        drag_ = Drag::Resize;
        handle_ = handle;
        original_ = doc_->pictures[selected_];
        SetCapture(hwnd_);
        return;
    }

    int hit = HitPicture(pt);
    bool onTitle = hit < 0 && HitTitle(pt);
    if (hit != selected_ || onTitle != titleSelected_) {
        selected_ = hit;
        titleSelected_ = onTitle;
        NotifyParent();
    }
    if (onTitle) {
        drag_ = Drag::Title;
        titleOrigX_ = doc_->titleOffsetX;
        titleOrigY_ = doc_->titleOffsetY;
        ToLogical(pt, 0, &titleStartX_, &titleStartY_);
        SetCapture(hwnd_);
    } else if (hit >= 0) {
        drag_ = Drag::Move;
        original_ = doc_->pictures[hit];
        double lx, ly;
        ToLogical(pt, original_.page, &lx, &ly);
        grabX_ = lx - original_.x;
        grabY_ = ly - original_.y;
        SetCapture(hwnd_);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PreviewWindow::OnMouseMove(POINT pt, WPARAM keys) {
    if (drag_ == Drag::None) return;

    // 창 위아래로 끌고 나가면 그쪽으로 스크롤합니다.
    RECT rc;
    GetClientRect(hwnd_, &rc);
    if (pt.y < 0) ScrollTo(scrollX_, scrollY_ - 24);
    else if (pt.y > rc.bottom) ScrollTo(scrollX_, scrollY_ + 24);

    if (drag_ == Drag::Place) {
        placeNow_ = pt;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (drag_ == Drag::Title) {
        double lx, ly;
        ToLogical(pt, 0, &lx, &ly);
        MoveTitle(static_cast<int>(std::lround(lx - titleStartX_)), static_cast<int>(std::lround(ly - titleStartY_)));
        return;
    }

    if (selected_ < 0 || selected_ >= static_cast<int>(doc_->pictures.size())) return;
    nl::Picture& p = doc_->pictures[selected_];
    if (drag_ == Drag::Move) {
        // 다른 쪽 위로 끌면 그 쪽으로 옮겨 갑니다.
        int page = PageAt(pt.y);
        double lx, ly;
        ToLogical(pt, page, &lx, &ly);
        p.page = page;
        p.x = std::clamp(static_cast<int>(std::lround(lx - grabX_)), 0, std::max(0, nl::kPageWidth - p.width));
        p.y = std::clamp(static_cast<int>(std::lround(ly - grabY_)), 0, std::max(0, nl::kPageHeight - p.height));
    } else {
        double lx, ly;
        ToLogical(pt, original_.page, &lx, &ly);
        lx = std::clamp(lx, 0.0, static_cast<double>(nl::kPageWidth));
        ly = std::clamp(ly, 0.0, static_cast<double>(nl::kPageHeight));
        const nl::Picture& o = original_;
        double left = o.x, top = o.y, right = o.x + o.width, bottom = o.y + o.height;
        if (MovesLeft(handle_)) left = std::min(lx, right - kMinPictureSize);
        if (MovesRight(handle_)) right = std::max(lx, left + kMinPictureSize);
        if (MovesTop(handle_)) top = std::min(ly, bottom - kMinPictureSize);
        if (MovesBottom(handle_)) bottom = std::max(ly, top + kMinPictureSize);
        // 모서리 손잡이는 가로세로 비율을 유지합니다(Shift를 누르면 자유롭게).
        if (IsCorner(handle_) && !(keys & MK_SHIFT) && o.width > 0 && o.height > 0) {
            double aspect = static_cast<double>(o.width) / o.height;
            double w = right - left, h = bottom - top;
            if (w / o.width > h / o.height) h = w / aspect;
            else w = h * aspect;
            if (MovesLeft(handle_)) left = right - w;
            else right = left + w;
            if (MovesTop(handle_)) top = bottom - h;
            else bottom = top + h;
        }
        p.x = static_cast<int>(std::lround(left));
        p.y = static_cast<int>(std::lround(top));
        p.width = std::max(kMinPictureSize, static_cast<int>(std::lround(right - left)));
        p.height = std::max(kMinPictureSize, static_cast<int>(std::lround(bottom - top)));
    }
    Refresh();  // 글자가 그림을 피해 다시 흐르는 모습을 바로 보여 줌
}

void PreviewWindow::OnMouseUp(POINT pt) {
    if (drag_ == Drag::None) return;
    // ReleaseCapture가 보내는 WM_CAPTURECHANGED가 '취소'로 처리되지 않도록 상태를 먼저 끝냅니다.
    Drag finished = drag_;
    drag_ = Drag::None;
    if (GetCapture() == hwnd_) ReleaseCapture();
    if (finished == Drag::Place) {
        FinishPlacing(pt);
        return;
    }
    if (finished == Drag::Title) {
        if (doc_->titleOffsetX != titleOrigX_ || doc_->titleOffsetY != titleOrigY_) NotifyEdited();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
    }
    if (selected_ < 0 || selected_ >= static_cast<int>(doc_->pictures.size())) return;
    const nl::Picture& p = doc_->pictures[selected_];
    if (p.page != original_.page || p.x != original_.x || p.y != original_.y || p.width != original_.width ||
        p.height != original_.height)
        NotifyEdited();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void PreviewWindow::FinishPlacing(POINT pt) {
    placing_ = false;
    double x0, y0, x1, y1;
    ToLogical(placeStart_, placePage_, &x0, &y0);
    ToLogical(pt, placePage_, &x1, &y1);
    auto clampX = [](double v) { return std::clamp(v, 0.0, static_cast<double>(nl::kPageWidth)); };
    auto clampY = [](double v) { return std::clamp(v, 0.0, static_cast<double>(nl::kPageHeight)); };
    x0 = clampX(x0), x1 = clampX(x1), y0 = clampY(y0), y1 = clampY(y1);
    double left = std::min(x0, x1), right = std::max(x0, x1);
    double top = std::min(y0, y1), bottom = std::max(y0, y1);

    nl::Picture p = pending_;
    double aspect = (p.width > 0 && p.height > 0) ? static_cast<double>(p.width) / p.height : 4.0 / 3.0;
    double w, h;
    if (right - left < 12 && bottom - top < 12) {
        // 그냥 눌렀으면 기본 크기로, 누른 곳이 그림 가운데가 되게 넣습니다.
        w = p.kind == nl::Picture::Logo ? 160 : 300;
        h = w / aspect;
        left = x0 - w / 2;
        top = y0 - h / 2;
    } else {
        // 지정한 영역 안에 원본 비율을 유지하며 최대한 크게 넣습니다.
        double rw = std::max(right - left, 1.0), rh = std::max(bottom - top, 1.0);
        if (rw / rh > aspect) {
            h = rh;
            w = h * aspect;
        } else {
            w = rw;
            h = w / aspect;
        }
        left += (rw - w) / 2;
        top += (rh - h) / 2;
    }
    p.page = placePage_;
    p.width = std::max(kMinPictureSize, static_cast<int>(std::lround(w)));
    p.height = std::max(kMinPictureSize, static_cast<int>(std::lround(h)));
    p.x = std::clamp(static_cast<int>(std::lround(left)), 0, std::max(0, nl::kPageWidth - p.width));
    p.y = std::clamp(static_cast<int>(std::lround(top)), 0, std::max(0, nl::kPageHeight - p.height));
    doc_->pictures.push_back(p);
    selected_ = static_cast<int>(doc_->pictures.size()) - 1;
    Refresh();
    InvalidateRect(hwnd_, nullptr, FALSE);
    NotifyParent();
    NotifyEdited();
}

void PreviewWindow::OnContextMenu(POINT screenPt) {
    if (!doc_ || placing_) return;
    POINT pt = screenPt;
    if (pt.x == -1 && pt.y == -1) {  // 키보드(메뉴 키)로 연 경우: 선택한 그림 위치에 띄움
        if (selected_ < 0 && !titleSelected_) return;
        RECT r = selected_ >= 0 ? PictureDeviceRect(doc_->pictures[selected_]) : TitleDeviceRect();
        pt = POINT{r.left + 10, r.top + 10};
        ClientToScreen(hwnd_, &pt);
        screenPt = pt;
    } else {
        ScreenToClient(hwnd_, &pt);
        int hit = HitPicture(pt);
        bool onTitle = hit < 0 && HitTitle(pt);
        if (hit != selected_ || onTitle != titleSelected_) {
            selected_ = hit;
            titleSelected_ = onTitle;
            InvalidateRect(hwnd_, nullptr, FALSE);
            NotifyParent();
        }
    }
    HMENU menu = CreatePopupMenu();
    if (titleSelected_) {
        auto radio = [&](int id, nl::TitleAlign a, const wchar_t* text) {
            AppendMenuW(menu, MF_STRING | (doc_->titleAlign == a ? MF_CHECKED : 0), id, text);
        };
        radio(IDM_TITLE_ALIGN_LEFT, nl::TitleAlign::Left, L"왼쪽 정렬");
        radio(IDM_TITLE_ALIGN_CENTER, nl::TitleAlign::Center, L"가운데 정렬");
        radio(IDM_TITLE_ALIGN_RIGHT, nl::TitleAlign::Right, L"오른쪽 정렬");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | ((doc_->titleOffsetX || doc_->titleOffsetY) ? 0 : MF_GRAYED), IDM_TITLE_RESET,
                    L"제목 위치 처음대로");
    } else if (selected_ >= 0) {
        bool wrap = doc_->pictures[selected_].wrapText;
        AppendMenuW(menu, MF_STRING, IDM_PICTURE_REPLACE, L"그림 바꾸기...");
        AppendMenuW(menu, MF_STRING, IDM_PICTURE_ASPECT, L"원래 비율로 맞추기");
        AppendMenuW(menu, MF_STRING | (wrap ? MF_CHECKED : 0), IDM_PICTURE_WRAP, L"글이 그림을 피해 가기");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_PICTURE_FRONT, L"맨 앞으로 가져오기");
        AppendMenuW(menu, MF_STRING, IDM_PICTURE_BACK, L"맨 뒤로 보내기");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, IDM_PICTURE_DELETE, L"그림 삭제\tDel");
    } else {
        AppendMenuW(menu, MF_STRING, IDM_INSERT_PHOTO, L"사진 추가...");
        AppendMenuW(menu, MF_STRING, IDM_INSERT_LOGO, L"로고 넣기...");
    }
    // 명령은 부모(주 창)로 가서 메뉴와 같은 경로로 처리됩니다.
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, screenPt.x, screenPt.y, 0, GetParent(hwnd_), nullptr);
    DestroyMenu(menu);
}

bool PreviewWindow::OnSetCursor() {
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient(hwnd_, &pt);
    LPCWSTR cursor = IDC_ARROW;
    if (placing_ || drag_ == Drag::Place) {
        cursor = IDC_CROSS;
    } else if (drag_ == Drag::Move || drag_ == Drag::Title) {
        cursor = IDC_SIZEALL;
    } else {
        int h = drag_ == Drag::Resize ? handle_ : HitHandle(pt);
        if (h >= 0) {
            static const LPCWSTR kHandleCursors[8] = {IDC_SIZENWSE, IDC_SIZENS, IDC_SIZENESW, IDC_SIZEWE,
                                                      IDC_SIZENWSE, IDC_SIZENS, IDC_SIZENESW, IDC_SIZEWE};
            cursor = kHandleCursors[h];
        } else if (HitPicture(pt) >= 0 || HitTitle(pt)) {
            cursor = IDC_SIZEALL;
        }
    }
    SetCursor(LoadCursorW(nullptr, cursor));
    return true;
}

// ---------------------------------------------------------------------------
// 메시지 처리

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
            OnMouseDown(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, wParam);
            return 0;
        case WM_MOUSEMOVE:
            OnMouseMove(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, wParam);
            return 0;
        case WM_LBUTTONUP:
            OnMouseUp(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            return 0;
        case WM_CAPTURECHANGED:
            if (drag_ != Drag::None && reinterpret_cast<HWND>(lParam) != hwnd_) CancelInteraction();
            return 0;
        case WM_CONTEXTMENU:
            OnContextMenu(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lParam) == HTCLIENT && OnSetCursor()) return TRUE;
            break;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_KEYDOWN: {
            RECT rc;
            GetClientRect(hwnd_, &rc);
            bool hasSelection = doc_ && selected_ >= 0 && drag_ == Drag::None;
            if (doc_ && titleSelected_ && drag_ == Drag::None &&
                (wParam == VK_UP || wParam == VK_DOWN || wParam == VK_LEFT || wParam == VK_RIGHT)) {
                // 선택한 제호를 방향키로 조금씩(Shift: 크게) 옮깁니다.
                int s = (GetKeyState(VK_SHIFT) & 0x8000) ? 10 : 2;
                titleOrigX_ = doc_->titleOffsetX;
                titleOrigY_ = doc_->titleOffsetY;
                MoveTitle(wParam == VK_LEFT ? -s : wParam == VK_RIGHT ? s : 0,
                          wParam == VK_UP ? -s : wParam == VK_DOWN ? s : 0);
                NotifyEdited();
                return 0;
            }
            int step = (GetKeyState(VK_SHIFT) & 0x8000) ? 10 : 2;
            switch (wParam) {
                case VK_DELETE:
                    if (hasSelection) DeleteSelected();
                    break;
                case VK_UP:
                    if (hasSelection) Nudge(0, -step);
                    else ScrollTo(scrollX_, scrollY_ - 40);
                    break;
                case VK_DOWN:
                    if (hasSelection) Nudge(0, step);
                    else ScrollTo(scrollX_, scrollY_ + 40);
                    break;
                case VK_LEFT:
                    if (hasSelection) Nudge(-step, 0);
                    else ScrollTo(scrollX_ - 40, scrollY_);
                    break;
                case VK_RIGHT:
                    if (hasSelection) Nudge(step, 0);
                    else ScrollTo(scrollX_ + 40, scrollY_);
                    break;
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
            if (bannerFont_) DeleteObject(bannerFont_);
            bannerFont_ = nullptr;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            hwnd_ = nullptr;
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}
