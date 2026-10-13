// 소식지를 GDI로 배치(layout)하고 쪽 단위로 그립니다.
// 미리보기 창과 인쇄가 같은 코드를 사용하므로 화면에 보이는 그대로 인쇄됩니다.
//
// 좌표는 "논리 단위"로, 쪽 하나가 kPageWidth x kPageHeight(A4 비율)입니다.
// Layout()으로 배치한 뒤, SetupPageMapping()으로 배율을 맞춘 DC에 DrawPage()로 그립니다.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "Newsletter.h"

namespace nl {

constexpr int kPageWidth = 800;
constexpr int kPageHeight = 1131;  // 800 * 297 / 210

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
    void DrawPage(HDC hdc, int page) const;

private:
    enum FontId { FLibrary, FTitle, FMeta, FGreeting, FTag, FHeading, FBody, FFooter, FEmpty, FontCount };

    struct Item {
        enum Type { Fill, RoundFill, Text } type;
        RECT rc;  // 행(row)의 왼쪽 위 기준 좌표
        COLORREF color;
        int font;
        std::wstring text;
        UINT format;
    };
    struct Row {
        int height = 0;
        int spaceBefore = 0;
        bool keepWithNext = false;
        std::vector<Item> items;
    };
    struct Placement {
        size_t row;
        int y;
    };

    void CreateFonts();
    int LineHeight(HDC hdc, int font, double spacing) const;
    int TextWidth(HDC hdc, int font, const std::wstring& s) const;
    std::vector<std::wstring> Wrap(HDC hdc, int font, const std::wstring& text, int width) const;

    void AddMasthead(HDC hdc, const Newsletter& doc);
    void AddGreeting(HDC hdc, const Newsletter& doc);
    void AddArticle(HDC hdc, const Newsletter& doc, const Article& article, bool first);
    void AddPublisher(HDC hdc, const Newsletter& doc);
    void Paginate();

    static Item MakeText(int left, int top, int right, int bottom, int font, COLORREF color,
                         const std::wstring& text, UINT format = DT_LEFT);
    static Item MakeFill(int left, int top, int right, int bottom, COLORREF color, bool round = false);

    HFONT fonts_[FontCount];
    std::vector<Row> rows_;
    std::vector<std::vector<Placement>> pages_;
    std::wstring footerLeft_;
};

}  // namespace nl
