#include "ImageLibrary.h"

#include <objidl.h>

#include <algorithm>
#include <cmath>
#include <cstring>

// gdiplus.h는 min/max 매크로를 기대하므로 NOMINMAX 환경에서는 std 버전을 쓰게 합니다.
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

namespace {

// 화면 미리보기용 축소본의 최대 변 길이(픽셀). 원본이 커도 끌기·크기 조절이 부드럽게 됩니다.
const int kScreenMaxSide = 1600;

IStream* StreamFromBytes(const std::string& data) {
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, data.size());
    if (!mem) return nullptr;
    void* p = GlobalLock(mem);
    if (!p) {
        GlobalFree(mem);
        return nullptr;
    }
    memcpy(p, data.data(), data.size());
    GlobalUnlock(mem);
    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(mem, TRUE, &stream))) {
        GlobalFree(mem);
        return nullptr;
    }
    return stream;
}

// 휴대폰 사진은 EXIF 방향 정보만 있고 픽셀은 돌아가 있지 않은 경우가 많아 바로 세워 줍니다.
void ApplyExifOrientation(Gdiplus::Bitmap& bitmap, int orientation) {
    Gdiplus::RotateFlipType flip;
    switch (orientation) {
        case 2: flip = Gdiplus::RotateNoneFlipX; break;
        case 3: flip = Gdiplus::Rotate180FlipNone; break;
        case 4: flip = Gdiplus::Rotate180FlipX; break;
        case 5: flip = Gdiplus::Rotate90FlipX; break;
        case 6: flip = Gdiplus::Rotate90FlipNone; break;
        case 7: flip = Gdiplus::Rotate270FlipX; break;
        case 8: flip = Gdiplus::Rotate270FlipNone; break;
        default: return;
    }
    bitmap.RotateFlip(flip);
}

}  // namespace

struct ImageLibrary::Entry {
    IStream* stream = nullptr;  // JPEG 등은 그릴 때까지 스트림이 살아 있어야 함
    std::unique_ptr<Gdiplus::Bitmap> full;
    std::unique_ptr<Gdiplus::Bitmap> screen;
    size_t dataSize = 0;
    bool failed = false;

    ~Entry() {
        screen.reset();
        full.reset();
        if (stream) stream->Release();
    }

    bool Load(const std::string& data) {
        dataSize = data.size();
        stream = StreamFromBytes(data);
        if (stream) full.reset(Gdiplus::Bitmap::FromStream(stream));
        if (!full || full->GetLastStatus() != Gdiplus::Ok || full->GetWidth() == 0 || full->GetHeight() == 0) {
            full.reset();
            failed = true;
            return false;
        }
        ApplyExifOrientation(*full, nl::JpegOrientation(data));

        UINT w = full->GetWidth(), h = full->GetHeight();
        double shrink = std::min(1.0, static_cast<double>(kScreenMaxSide) / std::max(w, h));
        INT sw = std::max(1, static_cast<INT>(std::lround(w * shrink)));
        INT sh = std::max(1, static_cast<INT>(std::lround(h * shrink)));
        screen = std::make_unique<Gdiplus::Bitmap>(sw, sh, PixelFormat32bppPARGB);
        Gdiplus::Graphics g(screen.get());
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.DrawImage(full.get(), 0, 0, sw, sh);
        return true;
    }
};

ImageLibrary::ImageLibrary() = default;
ImageLibrary::~ImageLibrary() = default;

bool ImageLibrary::ProbeSize(const std::string& data, int* width, int* height) {
    if (nl::ImageMimeType(data).empty()) return false;
    Entry entry;
    if (!entry.Load(data)) return false;
    *width = static_cast<int>(entry.full->GetWidth());
    *height = static_cast<int>(entry.full->GetHeight());
    return true;
}

ImageLibrary::Entry* ImageLibrary::Get(int id, const std::string& data) {
    auto it = cache_.find(id);
    if (it != cache_.end() && it->second->dataSize == data.size()) return it->second.get();
    auto entry = std::make_unique<Entry>();
    entry->Load(data);
    Entry* raw = entry.get();
    cache_[id] = std::move(entry);
    return raw;
}

bool ImageLibrary::PixelSize(int id, const std::string& data, int* width, int* height) {
    Entry* entry = Get(id, data);
    if (entry->failed) return false;
    *width = static_cast<int>(entry->full->GetWidth());
    *height = static_cast<int>(entry->full->GetHeight());
    return true;
}

void ImageLibrary::Prune(const nl::Newsletter& doc) {
    for (auto it = cache_.begin(); it != cache_.end();) {
        bool used = std::any_of(doc.pictures.begin(), doc.pictures.end(),
                                [&it](const nl::Picture& p) { return p.id == it->first; });
        for (const nl::Article& a : doc.articles) {
            for (const nl::Book& b : a.books) used = used || (!b.cover.empty() && b.coverId == it->first);
        }
        used = used || (!doc.background.data.empty() && doc.background.id == it->first);
        for (const auto& entry : doc.pageBackgrounds)
            used = used || (!entry.second.data.empty() && entry.second.id == it->first);
        it = used ? std::next(it) : cache_.erase(it);
    }
}

void ImageLibrary::DrawBackground(HDC hdc, const nl::Background& bg, const RECT& rect, bool forPrint) {
    if (bg.data.empty()) return;
    Entry* entry = Get(bg.id, bg.data);
    if (entry->failed) return;

    POINT pts[2] = {{rect.left, rect.top}, {rect.right, rect.bottom}};
    LPtoDP(hdc, pts, 2);
    const int x = pts[0].x, y = pts[0].y, w = pts[1].x - pts[0].x, h = pts[1].y - pts[0].y;
    if (w <= 0 || h <= 0) return;

    int saved = SaveDC(hdc);
    SetMapMode(hdc, MM_TEXT);
    SetViewportOrgEx(hdc, 0, 0, nullptr);
    SetWindowOrgEx(hdc, 0, 0, nullptr);
    {
        Gdiplus::Bitmap* bitmap = forPrint ? entry->full.get() : entry->screen.get();
        const double bw = bitmap->GetWidth(), bh = bitmap->GetHeight();
        Gdiplus::Graphics g(hdc);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.SetClip(Gdiplus::Rect(x, y, w, h));

        // 진하기: 알파 값에 비율을 곱해 흐리게 그립니다(흰 쪽 바탕 위라서 옅어 보임).
        Gdiplus::ColorMatrix matrix = {{{1, 0, 0, 0, 0},
                                        {0, 1, 0, 0, 0},
                                        {0, 0, 1, 0, 0},
                                        {0, 0, 0, std::clamp(bg.opacity, nl::kMinOpacity, nl::kMaxOpacity) / 100.0f, 0},
                                        {0, 0, 0, 0, 1}}};
        Gdiplus::ImageAttributes attributes;
        attributes.SetColorMatrix(&matrix);
        attributes.SetWrapMode(Gdiplus::WrapModeTileFlipXY);

        auto drawPart = [&](double dx, double dy, double dw, double dh, double sx, double sy, double sw, double sh) {
            g.DrawImage(bitmap,
                        Gdiplus::RectF(static_cast<Gdiplus::REAL>(dx), static_cast<Gdiplus::REAL>(dy),
                                       static_cast<Gdiplus::REAL>(dw), static_cast<Gdiplus::REAL>(dh)),
                        static_cast<Gdiplus::REAL>(sx), static_cast<Gdiplus::REAL>(sy), static_cast<Gdiplus::REAL>(sw),
                        static_cast<Gdiplus::REAL>(sh), Gdiplus::UnitPixel, &attributes);
        };
        switch (bg.fit) {
            case nl::BackgroundFit::Stretch:  // 쪽 크기에 맞춰 늘임(비율 무시)
                drawPart(x, y, w, h, 0, 0, bw, bh);
                break;
            case nl::BackgroundFit::Contain: {  // 비율 유지, 그림 전체가 보이게 가운데
                double s = std::min(w / bw, h / bh);
                drawPart(x + (w - bw * s) / 2, y + (h - bh * s) / 2, bw * s, bh * s, 0, 0, bw, bh);
                break;
            }
            case nl::BackgroundFit::Tile: {  // 바둑판: 쪽 너비의 1/4 크기로 반복
                double tw = w / 4.0, th = tw * bh / bw;
                for (double ty = y; ty < y + h; ty += th)
                    for (double tx = x; tx < x + w; tx += tw) drawPart(tx, ty, tw, th, 0, 0, bw, bh);
                break;
            }
            case nl::BackgroundFit::Cover:
            default: {  // 비율 유지, 쪽을 빈틈없이 채우고 넘치는 부분은 잘라냄
                double s = std::max(w / bw, h / bh);
                double sw = w / s, sh = h / s;
                drawPart(x, y, w, h, (bw - sw) / 2, (bh - sh) / 2, sw, sh);
                break;
            }
        }
    }
    RestoreDC(hdc, saved);
}

void ImageLibrary::Draw(HDC hdc, int id, const std::string& data, const RECT& rect, bool forPrint) {
    Entry* entry = Get(id, data);
    if (entry->failed) {
        // 읽을 수 없는 그림은 회색 상자에 X 표시로 자리만 보여 줍니다.
        HBRUSH gray = CreateSolidBrush(RGB(225, 225, 225));
        FillRect(hdc, &rect, gray);
        DeleteObject(gray);
        HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(DC_PEN));
        SetDCPenColor(hdc, RGB(160, 160, 160));
        MoveToEx(hdc, rect.left, rect.top, nullptr);
        LineTo(hdc, rect.right, rect.bottom);
        MoveToEx(hdc, rect.right, rect.top, nullptr);
        LineTo(hdc, rect.left, rect.bottom);
        SelectObject(hdc, oldPen);
        return;
    }

    // GDI+는 DC의 배율(매핑 모드)을 따르지 않으므로 장치 좌표로 바꿔서 그립니다.
    POINT pts[2] = {{rect.left, rect.top}, {rect.right, rect.bottom}};
    LPtoDP(hdc, pts, 2);
    int saved = SaveDC(hdc);
    SetMapMode(hdc, MM_TEXT);
    SetViewportOrgEx(hdc, 0, 0, nullptr);
    SetWindowOrgEx(hdc, 0, 0, nullptr);
    {
        Gdiplus::Bitmap* bitmap = forPrint ? entry->full.get() : entry->screen.get();
        Gdiplus::Graphics g(hdc);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        Gdiplus::ImageAttributes attributes;
        attributes.SetWrapMode(Gdiplus::WrapModeTileFlipXY);  // 가장자리에 생기는 반투명 테두리 방지
        Gdiplus::Rect dest(pts[0].x, pts[0].y, pts[1].x - pts[0].x, pts[1].y - pts[0].y);
        g.DrawImage(bitmap, dest, 0, 0, static_cast<INT>(bitmap->GetWidth()), static_cast<INT>(bitmap->GetHeight()),
                    Gdiplus::UnitPixel, &attributes);
    }
    RestoreDC(hdc, saved);
}
