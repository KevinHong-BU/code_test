#include "Renderer.h"

#include <algorithm>
#include <cmath>

namespace nl {

namespace {

const int kMarginLeft = 56;
const int kMarginRight = kPageWidth - 56;
const int kContentWidth = kMarginRight - kMarginLeft;
const int kContentTop = 56;
const int kContentBottom = kPageHeight - 84;
const int kFooterRuleY = kPageHeight - 64;

// 배치는 항상 이 배율의 측정용 DC에서 합니다.
// 화면 확대 비율과 상관없이 줄바꿈·쪽 나눔이 같아져서 미리보기와 인쇄 결과가 일치합니다.
const double kMeasureScale = 4.0;

const COLORREF kTextColor = RGB(40, 40, 40);
const COLORREF kHeadingColor = RGB(25, 25, 25);
const COLORREF kMutedColor = RGB(110, 110, 110);
const COLORREF kRuleColor = RGB(200, 200, 200);

COLORREF ToColorRef(Color c) { return RGB(c.r, c.g, c.b); }

// a와 b를 t(0~1) 비율로 섞습니다. t=0이면 a.
COLORREF Mix(Color a, Color b, double t) {
    auto ch = [t](int x, int y) { return static_cast<BYTE>(x + (y - x) * t + 0.5); };
    return RGB(ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b));
}

std::vector<std::wstring> SplitParagraphs(const std::wstring& s) {
    std::vector<std::wstring> out;
    std::wstring text = NormalizeNewlines(Trim(s));
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        out.push_back(Trim(text.substr(start, end - start)));
        start = end + 1;
    }
    return out;
}

}  // namespace

void SetupPageMapping(HDC hdc, double scale) {
    const int ext = 10000;
    SetMapMode(hdc, MM_ANISOTROPIC);
    SetWindowExtEx(hdc, ext, ext, nullptr);
    int vp = static_cast<int>(std::lround(ext * scale));
    SetViewportExtEx(hdc, vp, vp, nullptr);
}

Renderer::Renderer() { CreateFonts(); }

Renderer::~Renderer() {
    for (HFONT f : fonts_) {
        if (f) DeleteObject(f);
    }
}

void Renderer::CreateFonts() {
    struct Spec {
        int height;
        int weight;
    };
    const Spec specs[FontCount] = {
        {15, FW_NORMAL},  // FLibrary
        {36, FW_BOLD},    // FTitle
        {14, FW_NORMAL},  // FMeta
        {15, FW_NORMAL},  // FGreeting
        {12, FW_BOLD},    // FTag
        {22, FW_BOLD},    // FHeading
        {15, FW_NORMAL},  // FBody
        {11, FW_NORMAL},  // FFooter
        {15, FW_NORMAL},  // FEmpty
    };
    for (int i = 0; i < FontCount; ++i) {
        // 높이는 논리 단위이며, 음수는 글자 높이(em) 기준이라는 뜻입니다.
        fonts_[i] = CreateFontW(-specs[i].height, 0, 0, 0, specs[i].weight, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"맑은 고딕");
    }
}

int Renderer::LineHeight(HDC hdc, int font, double spacing) const {
    HGDIOBJ old = SelectObject(hdc, fonts_[font]);
    TEXTMETRICW tm{};
    GetTextMetricsW(hdc, &tm);
    SelectObject(hdc, old);
    return static_cast<int>(std::lround(tm.tmHeight * spacing));
}

int Renderer::TextWidth(HDC hdc, int font, const std::wstring& s) const {
    HGDIOBJ old = SelectObject(hdc, fonts_[font]);
    SIZE size{};
    GetTextExtentPoint32W(hdc, s.c_str(), static_cast<int>(s.size()), &size);
    SelectObject(hdc, old);
    return size.cx;
}

// 공백(어절) 단위로 줄을 나누고, 한 어절이 너무 길면 글자 단위로 자릅니다.
std::vector<std::wstring> Renderer::Wrap(HDC hdc, int font, const std::wstring& text, int width) const {
    std::vector<std::wstring> lines;
    HGDIOBJ old = SelectObject(hdc, fonts_[font]);
    size_t pos = 0;
    const size_t len = text.size();
    while (pos < len) {
        while (pos < len && text[pos] == L' ') ++pos;
        if (pos >= len) break;
        int fit = 0;
        SIZE size{};
        GetTextExtentExPointW(hdc, text.c_str() + pos, static_cast<int>(len - pos), width, &fit, nullptr,
                              &size);
        size_t take;
        size_t next;
        if (pos + fit >= len) {
            take = len - pos;
            next = len;
        } else {
            size_t space = text.rfind(L' ', pos + fit);
            if (space != std::wstring::npos && space > pos) {
                take = space - pos;
                next = space + 1;
            } else {
                take = std::max(fit, 1);
                next = pos + take;
            }
        }
        std::wstring line = text.substr(pos, take);
        while (!line.empty() && line.back() == L' ') line.pop_back();
        lines.push_back(line);
        pos = next;
    }
    SelectObject(hdc, old);
    if (lines.empty()) lines.push_back(L"");
    return lines;
}

Renderer::Item Renderer::MakeText(int left, int top, int right, int bottom, int font, COLORREF color,
                                  const std::wstring& text, UINT format) {
    return Item{Item::Text, RECT{left, top, right, bottom}, color, font, text, format};
}

Renderer::Item Renderer::MakeFill(int left, int top, int right, int bottom, COLORREF color, bool round) {
    return Item{round ? Item::RoundFill : Item::Fill, RECT{left, top, right, bottom}, color, 0, L"", 0};
}

void Renderer::Layout(const Newsletter& doc) {
    rows_.clear();
    pages_.clear();

    HDC hdc = CreateCompatibleDC(nullptr);
    SetupPageMapping(hdc, kMeasureScale);

    footerLeft_ = Trim(doc.libraryName);
    std::wstring titleIssue = Trim(Trim(doc.title) + L" " + Trim(doc.issue));
    if (!titleIssue.empty()) {
        if (!footerLeft_.empty()) footerLeft_ += L"  ·  ";
        footerLeft_ += titleIssue;
    }

    AddMasthead(hdc, doc);
    if (!Trim(doc.greeting).empty()) AddGreeting(hdc, doc);
    for (size_t i = 0; i < doc.articles.size(); ++i) AddArticle(hdc, doc, doc.articles[i], i == 0);
    if (doc.articles.empty()) {
        Row row;
        row.spaceBefore = 48;
        row.height = LineHeight(hdc, FEmpty, 1.5);
        row.items.push_back(MakeText(kMarginLeft, 0, kMarginRight, row.height, FEmpty, kMutedColor,
                                     L"아직 기사가 없습니다. [기사 추가] 버튼으로 첫 기사를 써 보세요.", DT_CENTER));
        rows_.push_back(row);
    }
    if (!Trim(doc.publisher).empty()) AddPublisher(hdc, doc);

    DeleteDC(hdc);
    Paginate();
}

void Renderer::AddMasthead(HDC hdc, const Newsletter& doc) {
    const Theme& theme = GetTheme(doc.theme);
    const int pad = 28;
    const int innerLeft = kMarginLeft + pad;
    const int innerRight = kMarginRight - pad;
    Row row;
    int y = 24;

    std::vector<Item> texts;
    if (!Trim(doc.libraryName).empty()) {
        int h = LineHeight(hdc, FLibrary, 1.2);
        texts.push_back(MakeText(innerLeft, y, innerRight, y + h, FLibrary, Mix(theme.primary, {255, 255, 255}, 0.75),
                                 Trim(doc.libraryName)));
        y += h + 2;
    }
    std::wstring title = Trim(doc.title).empty() ? L"(소식지 제목)" : Trim(doc.title);
    int titleH = LineHeight(hdc, FTitle, 1.15);
    for (const auto& line : Wrap(hdc, FTitle, title, innerRight - innerLeft)) {
        texts.push_back(MakeText(innerLeft, y, innerRight, y + titleH, FTitle, RGB(255, 255, 255), line));
        y += titleH;
    }
    std::wstring meta = Trim(doc.issue);
    if (!Trim(doc.date).empty()) {
        if (!meta.empty()) meta += L"   ·   ";
        meta += Trim(doc.date);
    }
    if (!meta.empty()) {
        y += 8;
        int h = LineHeight(hdc, FMeta, 1.2);
        texts.push_back(MakeText(innerLeft, y, innerRight, y + h, FMeta, Mix(theme.primary, {255, 255, 255}, 0.85), meta));
        y += h;
    }
    int bandHeight = y + 22;

    // 보조색 띠를 먼저 깔고 그 위에 주 색상 띠를 그려 아래쪽에 강조선이 보이게 합니다.
    row.items.push_back(MakeFill(kMarginLeft, 0, kMarginRight, bandHeight + 6, ToColorRef(theme.accent), true));
    row.items.push_back(MakeFill(kMarginLeft, 0, kMarginRight, bandHeight, ToColorRef(theme.primary), true));
    row.items.insert(row.items.end(), texts.begin(), texts.end());
    row.height = bandHeight + 6;
    rows_.push_back(row);
}

void Renderer::AddGreeting(HDC hdc, const Newsletter& doc) {
    const Theme& theme = GetTheme(doc.theme);
    const int padX = 20, padY = 14;
    const int textLeft = kMarginLeft + 4 + padX;
    const int textRight = kMarginRight - padX;
    const int lh = LineHeight(hdc, FGreeting, 1.45);

    Row row;
    row.spaceBefore = 24;
    std::vector<Item> texts;
    int y = padY;
    for (const auto& para : SplitParagraphs(doc.greeting)) {
        for (const auto& line : Wrap(hdc, FGreeting, para, textRight - textLeft)) {
            texts.push_back(MakeText(textLeft, y, textRight, y + lh, FGreeting, kTextColor, line));
            y += lh;
        }
    }
    int height = y + padY;
    row.items.push_back(MakeFill(kMarginLeft, 0, kMarginRight, height, ToColorRef(theme.light)));
    row.items.push_back(MakeFill(kMarginLeft, 0, kMarginLeft + 4, height, ToColorRef(theme.primary)));
    row.items.insert(row.items.end(), texts.begin(), texts.end());
    row.height = height;
    rows_.push_back(row);
}

void Renderer::AddArticle(HDC hdc, const Newsletter& doc, const Article& article, bool first) {
    const Theme& theme = GetTheme(doc.theme);

    // 머리 행: 분류 꼬리표 + 제목 + 밑줄. 본문 첫 줄과 같은 쪽에 놓이도록 keepWithNext.
    Row head;
    head.spaceBefore = first ? 30 : 36;
    head.keepWithNext = true;
    int y = 0;
    std::wstring category = Trim(article.category);
    if (!category.empty()) {
        int tagH = LineHeight(hdc, FTag, 1.0) + 6;
        int tagW = TextWidth(hdc, FTag, category) + 22;
        head.items.push_back(MakeFill(kMarginLeft, 0, kMarginLeft + tagW, tagH,
                                      ToColorRef(CategoryColor(category, doc.theme)), true));
        head.items.push_back(MakeText(kMarginLeft, 0, kMarginLeft + tagW, tagH, FTag, RGB(255, 255, 255), category,
                                      DT_CENTER | DT_VCENTER));
        y = tagH + 6;
    }
    std::wstring title = Trim(article.title);
    COLORREF titleColor = kHeadingColor;
    if (title.empty()) {
        title = L"(제목 없음)";
        titleColor = kMutedColor;
    }
    int titleH = LineHeight(hdc, FHeading, 1.25);
    for (const auto& line : Wrap(hdc, FHeading, title, kContentWidth)) {
        head.items.push_back(MakeText(kMarginLeft, y, kMarginRight, y + titleH, FHeading, titleColor, line));
        y += titleH;
    }
    y += 6;
    head.items.push_back(MakeFill(kMarginLeft, y, kMarginRight, y + 2, Mix(theme.primary, {255, 255, 255}, 0.7)));
    head.height = y + 2;
    rows_.push_back(head);

    // 본문: 줄 하나하나를 행으로 만들어 쪽 경계에서 자연스럽게 나뉘게 합니다.
    const int lh = LineHeight(hdc, FBody, 1.55);
    const COLORREF bulletColor = ToColorRef(CategoryColor(category, doc.theme));
    int pending = 10;
    bool lastWasBullet = false;
    for (const BodyLine& line : ParseBody(article.body)) {
        if (line.kind == BodyLine::Blank) {
            pending += lh / 2;
            lastWasBullet = false;
            continue;
        }
        bool bullet = line.kind == BodyLine::Bullet;
        if (bullet && !lastWasBullet) pending += 2;
        int left = bullet ? kMarginLeft + 24 : kMarginLeft;
        bool firstLine = true;
        for (const auto& text : Wrap(hdc, FBody, line.text, kMarginRight - left)) {
            Row row;
            row.spaceBefore = pending;
            row.height = lh;
            if (bullet && firstLine) {
                row.items.push_back(MakeText(kMarginLeft + 8, 0, left, lh, FBody, bulletColor, L"•"));
            }
            row.items.push_back(MakeText(left, 0, kMarginRight, lh, FBody, kTextColor, text));
            rows_.push_back(row);
            pending = 0;
            firstLine = false;
        }
        lastWasBullet = bullet;
    }
}

void Renderer::AddPublisher(HDC hdc, const Newsletter& doc) {
    const int lh = LineHeight(hdc, FMeta, 1.5);
    Row row;
    row.spaceBefore = 44;
    row.items.push_back(MakeFill(kMarginLeft, 0, kMarginRight, 1, kRuleColor));
    int y = 12;
    for (const auto& para : SplitParagraphs(doc.publisher)) {
        for (const auto& line : Wrap(hdc, FMeta, para, kContentWidth)) {
            row.items.push_back(MakeText(kMarginLeft, y, kMarginRight, y + lh, FMeta, kMutedColor, line, DT_CENTER));
            y += lh;
        }
    }
    row.height = y;
    rows_.push_back(row);
}

void Renderer::Paginate() {
    pages_.clear();
    pages_.emplace_back();
    int y = kContentTop;
    for (size_t i = 0; i < rows_.size(); ++i) {
        const Row& row = rows_[i];
        bool atTop = y == kContentTop;
        int before = atTop ? 0 : row.spaceBefore;
        int needed = before + row.height;
        if (row.keepWithNext && i + 1 < rows_.size()) needed += rows_[i + 1].spaceBefore + rows_[i + 1].height;
        if (!atTop && y + needed > kContentBottom) {
            pages_.emplace_back();
            y = kContentTop;
            before = 0;
        }
        pages_.back().push_back({i, y + before});
        y += before + row.height;
    }
}

void Renderer::DrawPage(HDC hdc, int page) const {
    if (page < 0 || page >= PageCount()) return;

    int saved = SaveDC(hdc);
    IntersectClipRect(hdc, 0, 0, kPageWidth, kPageHeight);
    SetBkMode(hdc, TRANSPARENT);

    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    RECT pageRect{0, 0, kPageWidth, kPageHeight};
    FillRect(hdc, &pageRect, white);
    DeleteObject(white);

    auto draw = [hdc, this](const Item& item, int dy) {
        RECT rc = item.rc;
        OffsetRect(&rc, 0, dy);
        switch (item.type) {
            case Item::Fill: {
                HBRUSH b = CreateSolidBrush(item.color);
                FillRect(hdc, &rc, b);
                DeleteObject(b);
                break;
            }
            case Item::RoundFill: {
                HBRUSH b = CreateSolidBrush(item.color);
                HGDIOBJ oldBrush = SelectObject(hdc, b);
                HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
                RoundRect(hdc, rc.left, rc.top, rc.right + 1, rc.bottom + 1, 16, 16);
                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBrush);
                DeleteObject(b);
                break;
            }
            case Item::Text: {
                HGDIOBJ oldFont = SelectObject(hdc, fonts_[item.font]);
                SetTextColor(hdc, item.color);
                DrawTextW(hdc, item.text.c_str(), static_cast<int>(item.text.size()), &rc,
                          item.format | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
                SelectObject(hdc, oldFont);
                break;
            }
        }
    };

    for (const Placement& p : pages_[page]) {
        for (const Item& item : rows_[p.row].items) draw(item, p.y);
    }

    // 쪽 바닥글
    const int footH = 20;
    const int footTop = kFooterRuleY + 10;
    draw(MakeFill(kMarginLeft, kFooterRuleY, kMarginRight, kFooterRuleY + 1, kRuleColor), 0);
    if (!footerLeft_.empty())
        draw(MakeText(kMarginLeft, footTop, kMarginRight - 80, footTop + footH, FFooter, kMutedColor, footerLeft_,
                      DT_LEFT | DT_END_ELLIPSIS), 0);
    std::wstring pageText = std::to_wstring(page + 1) + L" / " + std::to_wstring(PageCount());
    draw(MakeText(kMarginRight - 80, footTop, kMarginRight, footTop + footH, FFooter, kMutedColor, pageText, DT_RIGHT),
         0);

    RestoreDC(hdc, saved);
}

}  // namespace nl
