// 소식지를 GDI로 배치(layout)하고 쪽 단위로 그립니다.
// 미리보기 창과 인쇄가 같은 코드를 사용하므로 화면에 보이는 그대로 인쇄됩니다.
//
// 좌표는 "논리 단위"로, 쪽 하나가 kPageWidth x kPageHeight(A4 비율)입니다.
// Layout()으로 배치한 뒤, SetupPageMapping()으로 배율을 맞춘 DC에 DrawPage()로 그립니다.
// 그림(로고·사진)은 쪽 위에 자유롭게 놓이며, '글 피해 가기'가 켜진 그림은 본문 글자가 비켜 갑니다.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "Newsletter.h"

class ImageLibrary;

namespace nl {

constexpr int kPageWidth = 800;
constexpr int kPageHeight = 1131;  // 800 * 297 / 210
constexpr int kMarginLeft = 56;
constexpr int kMarginRight = kPageWidth - 56;
constexpr int kContentTop = 56;

// 논리 단위 1 = 장치 픽셀 scale 개가 되도록 DC를 설정합니다.
void SetupPageMapping(HDC hdc, double scale);

class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void Layout(const Newsletter& doc);
    int PageCount() const { return static_cast<int>(pages_.size()); }
    // 현재 뷰포트 원점을 쪽의 왼쪽 위로 보고 page번째 쪽을 그립니다.
    // images가 있으면 doc.pictures 가운데 이 쪽에 놓인 그림도 그립니다.
    void DrawPage(HDC hdc, int page, const Newsletter& doc, ImageLibrary* images, bool forPrint) const;

    // (page, y) 위치 바로 위에서 시작한 기사 번호. 그 앞이면 -1. HTML 내보내기에서 사진 위치를 정할 때 씁니다.
    int AnchorFor(int page, int y) const;

    // 1쪽 맨 위 제호(도서관명·소식지 제목·호수/발행일) 글자들이 차지하는 영역(쪽 좌표).
    RECT TitleRect() const { return titleRect_; }
    // 실제로 적용된 제호 이동 거리(색 띠를 벗어나지 않도록 줄어든 값).
    int TitleOffsetX() const { return titleOffsetX_; }
    int TitleOffsetY() const { return titleOffsetY_; }

private:
    enum FontId { FLibrary, FTitle, FMeta, FGreeting, FTag, FHeading, FBody, FFooter, FEmpty, FontCount };

    struct Item {
        enum Type { Fill, RoundFill, Dot, Text } type;
        RECT rc;  // 쪽 좌표
        COLORREF color;
        int font;
        std::wstring text;
        UINT format;
    };
    struct Span {
        int left, right;
        bool empty() const { return right <= left; }
    };
    struct Mark {
        int page, y;
    };

    void EnsureFonts(const Newsletter& doc);
    int LineHeight(int font, double spacing) const;
    int TextWidth(int font, const std::wstring& s) const;
    // text[pos..]에서 width 안에 들어가는 한 줄을 잘라 냅니다. 다음 줄 시작 위치를 next에 돌려줍니다.
    std::wstring TakeLine(int font, const std::wstring& text, size_t pos, int width, size_t* next) const;

    // 그림을 피해 [top, bottom) 높이에서 쓸 수 있는 가장 넓은 가로 구간.
    Span FreeSpan(int top, int bottom, int left, int right, int minWidth) const;
    int NextFreeY(int top, int bottom, int left, int right) const;

    void NewPage();
    bool AtTop() const { return y_ == kContentTop; }
    void Add(const Item& item) { pages_[page_].push_back(item); }
    // 문단 하나를 그림을 피해 줄 단위로 흘려 넣습니다. indent/bullet은 글머리표 항목용.
    // 글머리표는 글꼴에 '•' 글자가 없어도 보이도록 동그라미 도형으로 그립니다.
    void FlowText(int font, COLORREF color, const std::wstring& text, int left, int right, int lineHeight,
                  UINT align = DT_LEFT, int indent = 0, bool bullet = false, COLORREF bulletColor = 0);
    // (startPage, startY)부터 현재 위치까지 쪽마다 배경 상자를 깔아 줍니다.
    void InsertBackground(int startPage, size_t startIndex, int startY, int left, int right, COLORREF color,
                          bool round = false);

    void AddMasthead(const Newsletter& doc);
    void AddGreeting(const Newsletter& doc);
    void AddArticle(const Newsletter& doc, const Article& article, bool first);
    void AddPublisher(const Newsletter& doc);

    struct FontKey {
        std::wstring headingFace, bodyFace;
        int headingSize = 0, bodySize = 0, titleSize = 0;
        bool operator==(const FontKey& o) const {
            return headingFace == o.headingFace && bodyFace == o.bodyFace && headingSize == o.headingSize &&
                   bodySize == o.bodySize && titleSize == o.titleSize;
        }
    };
    HFONT fonts_[FontCount];
    FontKey fontKey_;
    double bodyUnits_ = 15;  // 본문 글자 크기(논리 단위)
    RECT titleRect_{};       // 1쪽 제호 글자들이 차지하는 영역
    int titleOffsetX_ = 0, titleOffsetY_ = 0;
    HDC measure_ = nullptr;  // Layout 동안만 유효

    std::vector<std::vector<Item>> pages_;
    std::vector<std::vector<RECT>> obstacles_;  // 쪽별로 글자가 피해야 할 영역
    std::vector<Mark> articleMarks_;
    std::wstring footerLeft_;
    int page_ = 0;
    int y_ = kContentTop;
};

}  // namespace nl
