#include "Renderer.h"

#include <algorithm>
#include <climits>
#include <cmath>

#include "ImageLibrary.h"

namespace nl {

namespace {

const int kContentWidth = kMarginRight - kMarginLeft;
const int kContentBottom = kPageHeight - 84;
const int kFooterRuleY = kPageHeight - 64;
const int kMinLineWidth = 90;  // 그림 옆에 이보다 좁은 틈만 남으면 그림 아래로 내려감
const int kWrapGapX = 14;      // 그림과 글자 사이 가로 여백
const int kWrapGapY = 6;
const int kMaxPages = 500;

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

Renderer::Renderer() {
    for (HFONT& f : fonts_) f = nullptr;
    EnsureFonts(Newsletter{});
}

Renderer::~Renderer() {
    for (HFONT f : fonts_) {
        if (f) DeleteObject(f);
    }
    for (HFONT f : extraFonts_) DeleteObject(f);
}

int Renderer::ExtraFont(const std::wstring& face, double height, int weight) {
    auto key = std::make_tuple(face.substr(0, LF_FACESIZE - 1), static_cast<int>(std::lround(height)), weight);
    auto it = extraFontIndex_.find(key);
    if (it != extraFontIndex_.end()) return it->second;
    HFONT font = CreateFontW(-std::get<1>(key), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS,
                             std::get<0>(key).c_str());
    extraFonts_.push_back(font);
    int index = FontCount + static_cast<int>(extraFonts_.size()) - 1;
    extraFontIndex_[key] = index;
    return index;
}

// 문서의 글꼴·글자 크기로 글꼴 개체를 만듭니다. 바뀌지 않았으면 그대로 씁니다.
void Renderer::EnsureFonts(const Newsletter& doc) {
    FontKey key{FontOrDefault(doc.headingFont).substr(0, LF_FACESIZE - 1),
                FontOrDefault(doc.bodyFont).substr(0, LF_FACESIZE - 1),
                std::clamp(doc.headingSize, kMinFontSize, kMaxFontSize),
                std::clamp(doc.bodySize, kMinFontSize, kMaxFontSize),
                std::clamp(doc.titleSize, kMinFontSize, kMaxFontSize)};
    if (fonts_[0] && key == fontKey_) return;
    fontKey_ = key;

    // 1pt = 쪽 너비 800 / A4 너비 595.3pt 논리 단위. 크기는 반 포인트 단위로 저장되어 있음.
    const double unitsPerHalfPoint = kPageWidth / 595.28 / 2;
    const double heading = key.headingSize * unitsPerHalfPoint;
    const double body = key.bodySize * unitsPerHalfPoint;
    const double title = key.titleSize * unitsPerHalfPoint;
    bodyUnits_ = body;
    headingUnits_ = heading;

    struct Spec {
        double height;
        int weight;
        bool heading;
    };
    const Spec specs[FontCount] = {
        {body, FW_NORMAL, false},                                  // FLibrary
        {title, FW_BOLD, true},                                    // FTitle
        {body * 0.93, FW_NORMAL, false},                           // FMeta
        {body, FW_NORMAL, false},                                  // FGreeting
        {std::max(9.0, heading * 0.55), FW_BOLD, true},            // FTag
        {heading, FW_BOLD, true},                                  // FHeading
        {body, FW_NORMAL, false},                                  // FBody
        {std::clamp(body * 0.73, 9.0, 14.0), FW_NORMAL, false},    // FFooter
        {body, FW_NORMAL, false},                                  // FEmpty
        {body * 1.08, FW_BOLD, false},                             // FBookTitle
    };
    for (int i = 0; i < FontCount; ++i) {
        if (fonts_[i]) DeleteObject(fonts_[i]);
        // 높이는 논리 단위이며, 음수는 글자 높이(em) 기준이라는 뜻입니다.
        // 설치되지 않은 글꼴이면 Windows가 비슷한 글꼴로 대신합니다.
        fonts_[i] = CreateFontW(-static_cast<int>(std::lround(specs[i].height)), 0, 0, 0, specs[i].weight, FALSE, FALSE,
                                FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_SWISS,
                                (specs[i].heading ? key.headingFace : key.bodyFace).c_str());
    }
}

int Renderer::LineHeight(int font, double spacing) const {
    HGDIOBJ old = SelectObject(measure_, Font(font));
    TEXTMETRICW tm{};
    GetTextMetricsW(measure_, &tm);
    SelectObject(measure_, old);
    return static_cast<int>(std::lround(tm.tmHeight * spacing));
}

int Renderer::TextWidth(int font, const std::wstring& s) const {
    HGDIOBJ old = SelectObject(measure_, Font(font));
    SIZE size{};
    GetTextExtentPoint32W(measure_, s.c_str(), static_cast<int>(s.size()), &size);
    SelectObject(measure_, old);
    return size.cx;
}

// 공백(어절) 단위로 줄을 나누고, 한 어절이 너무 길면 글자 단위로 자릅니다.
std::wstring Renderer::TakeLine(int font, const std::wstring& text, size_t pos, int width, size_t* next) const {
    const size_t len = text.size();
    HGDIOBJ old = SelectObject(measure_, Font(font));
    int fit = 0;
    SIZE size{};
    GetTextExtentExPointW(measure_, text.c_str() + pos, static_cast<int>(len - pos), std::max(width, 1), &fit,
                          nullptr, &size);
    SelectObject(measure_, old);

    size_t take;
    if (pos + fit >= len) {
        take = len - pos;
        *next = len;
    } else {
        size_t space = text.rfind(L' ', pos + fit);
        if (space != std::wstring::npos && space > pos) {
            take = space - pos;
            *next = space + 1;
        } else {
            take = std::max(fit, 1);
            *next = pos + take;
        }
    }
    std::wstring line = text.substr(pos, take);
    while (!line.empty() && line.back() == L' ') line.pop_back();
    return line;
}

Renderer::Span Renderer::FreeSpan(int top, int bottom, int left, int right, int minWidth) const {
    std::vector<std::pair<int, int>> blocked;
    if (page_ < static_cast<int>(obstacles_.size())) {
        for (const RECT& r : obstacles_[page_]) {
            if (r.top < bottom && r.bottom > top && r.left < right && r.right > left)
                blocked.push_back({std::max<int>(r.left, left), std::min<int>(r.right, right)});
        }
    }
    std::sort(blocked.begin(), blocked.end());
    Span best{0, 0};
    int cursor = left;
    auto consider = [&best](int l, int r) {
        if (r - l > best.right - best.left) best = {l, r};
    };
    for (const auto& b : blocked) {
        if (b.first > cursor) consider(cursor, b.first);
        cursor = std::max(cursor, b.second);
    }
    consider(cursor, right);
    if (best.right - best.left < minWidth) return {0, 0};
    return best;
}

int Renderer::NextFreeY(int top, int bottom, int left, int right) const {
    int next = INT_MAX;
    if (page_ < static_cast<int>(obstacles_.size())) {
        for (const RECT& r : obstacles_[page_]) {
            if (r.top < bottom && r.bottom > top && r.left < right && r.right > left) next = std::min<int>(next, r.bottom);
        }
    }
    return next == INT_MAX ? top + 1 : std::max(next, top + 1);
}

void Renderer::NewPage() {
    if (PageCount() >= kMaxPages) {
        y_ = kContentTop;  // 비정상적으로 긴 문서: 마지막 쪽에 겹쳐 그리더라도 멈추지 않게 함
        return;
    }
    pages_.emplace_back();
    page_ = PageCount() - 1;
    y_ = kContentTop;
}

void Renderer::FlowText(int font, COLORREF color, const std::wstring& text, int left, int right, int lineHeight,
                        UINT align, int indent, bool bullet, COLORREF bulletColor) {
    // 현재 위치에서 한 줄을 놓을 수 있는 구간을 찾습니다. 그림에 막히면 그 아래로, 쪽 끝이면 다음 쪽으로.
    auto findSpan = [&]() -> Span {
        for (int guard = 0; guard < 2000; ++guard) {
            if (y_ + lineHeight > kContentBottom && !AtTop()) NewPage();
            Span s = FreeSpan(y_, y_ + lineHeight, left, right, kMinLineWidth + indent);
            if (!s.empty()) return s;
            int next = NextFreeY(y_, y_ + lineHeight, left, right);
            if (next + lineHeight > kContentBottom) NewPage();
            else y_ = next;
        }
        return {left, right};
    };

    size_t pos = 0;
    bool first = true;
    while (true) {
        while (pos < text.size() && text[pos] == L' ') ++pos;
        if (pos >= text.size() && !first) break;
        Span s = findSpan();
        if (pos >= text.size()) {  // 빈 문단: 한 줄 높이만 차지
            y_ += lineHeight;
            break;
        }
        int textLeft = s.left + indent;
        size_t next = 0;
        std::wstring line = TakeLine(font, text, pos, s.right - textLeft, &next);
        if (bullet && first) {
            const int size = std::max(4, static_cast<int>(std::lround(bodyUnits_ * 0.4)));
            const int center = y_ + LineHeight(font, 0.55);  // 글자 높이의 중간쯤
            Add({Item::Dot, RECT{s.left + 9, center - size / 2, s.left + 9 + size, center + size / 2}, bulletColor, 0,
                 L"", 0});
        }
        Add({Item::Text, RECT{textLeft, y_, s.right, y_ + lineHeight}, color, font, line, align});
        y_ += lineHeight;
        pos = next;
        first = false;
    }
}

void Renderer::InsertBackground(int startPage, size_t startIndex, int startY, int left, int right, COLORREF color,
                                bool round) {
    for (int p = startPage; p <= page_; ++p) {
        int top = p == startPage ? startY : kContentTop;
        int bottom = p == page_ ? y_ : kContentBottom;
        if (bottom <= top) continue;
        size_t index = p == startPage ? startIndex : 0;
        Item item{round ? Item::RoundFill : Item::Fill, RECT{left, top, right, bottom}, color, 0, L"", 0};
        pages_[p].insert(pages_[p].begin() + static_cast<std::ptrdiff_t>(index), item);
    }
}

void Renderer::Layout(const Newsletter& doc) {
    pages_.assign(1, {});
    articleMarks_.clear();
    page_ = 0;
    y_ = kContentTop;

    // 글자가 피해 갈 그림 영역을 쪽별로 모읍니다.
    int lastPicturePage = 0;
    obstacles_.clear();
    for (const Picture& p : doc.pictures) {
        lastPicturePage = std::max(lastPicturePage, std::min(p.page, kMaxPages - 1));
        if (!p.wrapText || p.page >= kMaxPages) continue;
        if (static_cast<int>(obstacles_.size()) <= p.page) obstacles_.resize(p.page + 1);
        obstacles_[p.page].push_back(
            RECT{p.x - kWrapGapX, p.y - kWrapGapY, p.x + p.width + kWrapGapX, p.y + p.height + kWrapGapY});
    }

    EnsureFonts(doc);
    measure_ = CreateCompatibleDC(nullptr);
    SetupPageMapping(measure_, kMeasureScale);

    footerLeft_ = Trim(doc.libraryName);
    std::wstring titleIssue = Trim(Trim(doc.title) + L" " + Trim(doc.issue));
    if (!titleIssue.empty()) {
        if (!footerLeft_.empty()) footerLeft_ += L"  ·  ";
        footerLeft_ += titleIssue;
    }

    AddMasthead(doc);
    if (!Trim(doc.greeting).empty()) AddGreeting(doc);
    for (size_t i = 0; i < doc.articles.size(); ++i) AddArticle(doc, i, i == 0);
    if (doc.articles.empty()) {
        if (!AtTop()) y_ += 48;
        FlowText(FEmpty, kMutedColor, L"아직 기사가 없습니다. [기사 추가] 버튼으로 첫 기사를 써 보세요.", kMarginLeft,
                 kMarginRight, LineHeight(FEmpty, 1.5), DT_CENTER);
    }
    if (!Trim(doc.publisher).empty()) AddPublisher(doc);

    DeleteDC(measure_);
    measure_ = nullptr;

    // 글이 끝난 뒤의 쪽에 놓인 그림도 사라지지 않도록 그 쪽까지 만들어 둡니다.
    while (PageCount() <= lastPicturePage) pages_.emplace_back();
}

void Renderer::AddMasthead(const Newsletter& doc) {
    const Theme& theme = GetTheme(doc.theme);
    const int pad = 28;
    const int top = y_;
    const size_t startIndex = pages_[page_].size();
    const UINT align = doc.titleAlign == TitleAlign::Center  ? DT_CENTER
                       : doc.titleAlign == TitleAlign::Right ? DT_RIGHT
                                                             : DT_LEFT;

    // 제호 글자들을 (dx, dy)만큼 옮긴 틀에 배치하고, 글자들이 실제로 차지한 영역을 돌려줍니다.
    auto layoutText = [&](int dx, int dy) {
        pages_[0].resize(startIndex);
        const int left = kMarginLeft + pad + dx, right = kMarginRight - pad + dx;
        y_ = top + 24 + dy;
        if (!Trim(doc.libraryName).empty()) {
            FlowText(FLibrary, Mix(theme.primary, {255, 255, 255}, 0.75), Trim(doc.libraryName), left, right,
                     LineHeight(FLibrary, 1.2), align);
            y_ += 2;
        }
        std::wstring title = Trim(doc.title).empty() ? L"(소식지 제목)" : Trim(doc.title);
        FlowText(FTitle, RGB(255, 255, 255), title, left, right, LineHeight(FTitle, 1.15), align);
        std::wstring meta = Trim(doc.issue);
        if (!Trim(doc.date).empty()) {
            if (!meta.empty()) meta += L"   ·   ";
            meta += Trim(doc.date);
        }
        if (!meta.empty()) {
            y_ += 8;
            FlowText(FMeta, Mix(theme.primary, {255, 255, 255}, 0.85), meta, left, right, LineHeight(FMeta, 1.2),
                     align);
        }
        RECT box{0, 0, 0, 0};
        for (size_t i = startIndex; i < pages_[0].size(); ++i) {
            const Item& item = pages_[0][i];
            if (item.type != Item::Text) continue;
            int w = std::min<int>(TextWidth(item.font, item.text), item.rc.right - item.rc.left);
            int x = (item.format & DT_CENTER)  ? (item.rc.left + item.rc.right - w) / 2
                    : (item.format & DT_RIGHT) ? item.rc.right - w
                                               : item.rc.left;
            RECT r{x, item.rc.top, x + w, item.rc.bottom};
            if (IsRectEmpty(&box)) box = r;
            else UnionRect(&box, &box, &r);
        }
        return box;
    };

    // 끌어 옮긴 만큼 글자 묶음이 그대로 움직이게 합니다. 글자가 로고 같은 그림을 피하느라 틀 안에서 자리를
    // 바꿀 수 있으므로, 실제 위치가 목표에 맞을 때까지 틀의 이동 거리를 몇 번 고쳐 다시 배치합니다.
    // 단, 글자가 색 띠 밖으로 나가지는 않게 합니다.
    const int dy = std::clamp(doc.titleOffsetY, -16, kMaxTitleOffset);
    RECT home = layoutText(0, 0);
    RECT box = home;
    int movedX = 0;
    int wantX = std::clamp(doc.titleOffsetX, -kMaxTitleOffset, kMaxTitleOffset);
    if (!IsRectEmpty(&home) && (wantX != 0 || dy != 0)) {
        wantX = std::clamp(wantX, std::min(0, kMarginLeft + 8 - static_cast<int>(home.left)),
                           std::max(0, kMarginRight - 8 - static_cast<int>(home.right)));
        int frameX = wantX;
        for (int pass = 0; pass < 4; ++pass) {
            box = layoutText(frameX, dy);
            int error = static_cast<int>(home.left) + wantX - static_cast<int>(box.left);
            if (std::abs(error) <= 1) break;
            frameX += error;
        }
        movedX = static_cast<int>(box.left - home.left);
    }
    titleRect_ = box;
    titleOffsetX_ = movedX;
    titleOffsetY_ = dy;
    const int bandBottom = y_ + 22;

    // 주 색상 띠 아래로 보조색이 6만큼 비어져 나와 강조선처럼 보이게 합니다(먼저 넣은 것이 위에 그려짐).
    y_ = bandBottom;
    InsertBackground(0, startIndex, top, kMarginLeft, kMarginRight, ToColorRef(theme.primary), true);
    y_ = bandBottom + 6;
    InsertBackground(0, startIndex, top, kMarginLeft, kMarginRight, ToColorRef(theme.accent), true);
}

void Renderer::AddGreeting(const Newsletter& doc) {
    const Theme& theme = GetTheme(doc.theme);
    const int padX = 20, padY = 14;
    const int lh = LineHeight(FGreeting, 1.45);
    if (!AtTop()) y_ += 24;
    if (y_ + 2 * padY + 2 * lh > kContentBottom && !AtTop()) NewPage();

    const int startPage = page_;
    const size_t startIndex = pages_[page_].size();
    const int startY = y_;
    y_ += padY;
    for (const auto& para : SplitParagraphs(doc.greeting))
        FlowText(FGreeting, kTextColor, para, kMarginLeft + 4 + padX, kMarginRight - padX, lh);
    y_ += padY;

    InsertBackground(startPage, startIndex, startY, kMarginLeft, kMarginLeft + 4, ToColorRef(theme.primary));
    InsertBackground(startPage, startIndex, startY, kMarginLeft, kMarginRight, ToColorRef(theme.light));
}

void Renderer::AddArticle(const Newsletter& doc, size_t index, bool first) {
    const Article& article = doc.articles[index];
    const Theme& theme = GetTheme(doc.theme);
    const std::wstring category = Trim(article.category);
    const int lh = LineHeight(FBody, 1.55);

    // 기사 제목 서식: 기사에 정한 글꼴·크기·굵기가 없으면 문서의 제목 글꼴·크기를 씁니다.
    const double unitsPerHalfPoint = kPageWidth / 595.28 / 2;
    const double titleUnits = article.titleSize ? std::clamp(article.titleSize, kMinFontSize, kMaxFontSize) *
                                                      unitsPerHalfPoint
                                                : headingUnits_;
    const std::wstring titleFace = Trim(article.titleFont).empty() ? FontOrDefault(doc.headingFont) : Trim(article.titleFont);
    const int titleFont = ExtraFont(titleFace, titleUnits, article.titleBold ? FW_BOLD : FW_NORMAL);
    const int titleH = LineHeight(titleFont, 1.25);
    const int tagH = LineHeight(FTag, 1.0) + 6;
    const UINT align = article.titleAlign == TitleAlign::Center  ? DT_CENTER
                       : article.titleAlign == TitleAlign::Right ? DT_RIGHT
                                                                 : DT_LEFT;

    // 쪽 나누기: 이 기사를 새 쪽에서 시작
    if (article.pageBreakBefore && !AtTop()) NewPage();

    // 머리(분류·제목·밑줄)가 본문 첫 줄과 같은 쪽에 오도록, 들어갈 자리가 없으면 다음 쪽에서 시작합니다.
    if (!AtTop()) y_ += first ? 30 : 36;
    int needed = (category.empty() ? 0 : tagH + 6) + titleH + 8 + 10 + lh;
    if (y_ + needed > kContentBottom && !AtTop()) NewPage();
    articleMarks_.push_back({page_, y_});

    if (!category.empty()) {
        int tagW = TextWidth(FTag, category) + 22;
        Span s = FreeSpan(y_, y_ + tagH, kMarginLeft, kMarginRight, tagW);
        for (int guard = 0; s.empty() && guard < 50; ++guard) {
            y_ = NextFreeY(y_, y_ + tagH, kMarginLeft, kMarginRight);
            if (y_ + needed > kContentBottom) NewPage();
            s = FreeSpan(y_, y_ + tagH, kMarginLeft, kMarginRight, tagW);
        }
        if (s.empty()) s = {kMarginLeft, kMarginRight};
        // 꼬리표도 제목 정렬을 따릅니다.
        int x = align == DT_CENTER ? (s.left + s.right - tagW) / 2 : align == DT_RIGHT ? s.right - tagW : s.left;
        COLORREF tagColor = ToColorRef(CategoryColor(category, doc.theme));
        Add({Item::RoundFill, RECT{x, y_, x + tagW, y_ + tagH}, tagColor, 0, L"", 0});
        Add({Item::Text, RECT{x, y_, x + tagW, y_ + tagH}, RGB(255, 255, 255), FTag, category, DT_CENTER | DT_VCENTER});
        y_ += tagH + 6;
    }

    std::wstring title = Trim(article.title);
    COLORREF titleColor = kHeadingColor;
    if (title.empty()) {
        title = L"(제목 없음)";
        titleColor = kMutedColor;
    }
    FlowText(titleFont, titleColor, title, kMarginLeft, kMarginRight, titleH, align);

    y_ += 6;
    Span rule = FreeSpan(y_, y_ + 2, kMarginLeft, kMarginRight, 40);
    if (!rule.empty())
        Add({Item::Fill, RECT{rule.left, y_, rule.right, y_ + 2}, Mix(theme.primary, {255, 255, 255}, 0.7), 0, L"", 0});
    y_ += 2;

    // 본문: 줄 단위로 흘려 넣으므로 그림 옆을 지나거나 쪽 경계에서 자연스럽게 나뉩니다.
    const COLORREF bulletColor = ToColorRef(CategoryColor(category, doc.theme));
    int pending = 10;
    bool lastWasBullet = false;
    for (const BodyLine& line : ParseBody(article.body)) {
        if (line.kind == BodyLine::Blank) {
            pending += lh / 2;
            lastWasBullet = false;
            continue;
        }
        if (line.kind == BodyLine::PageBreak) {  // [쪽 나누기]
            if (!AtTop()) NewPage();
            pending = 0;
            lastWasBullet = false;
            continue;
        }
        bool bullet = line.kind == BodyLine::Bullet;
        if (bullet && !lastWasBullet) pending += 2;
        if (!AtTop()) y_ += pending;
        pending = 0;
        if (bullet) FlowText(FBody, kTextColor, line.text, kMarginLeft, kMarginRight, lh, DT_LEFT, 24, true, bulletColor);
        else FlowText(FBody, kTextColor, line.text, kMarginLeft, kMarginRight, lh);
        lastWasBullet = bullet;
    }

    AddBooks(doc, index);
}

bool Renderer::Blocked(int top, int bottom, int left, int right) const {
    if (page_ >= static_cast<int>(obstacles_.size())) return false;
    for (const RECT& r : obstacles_[page_]) {
        if (r.top < bottom && r.bottom > top && r.left < right && r.right > left) return true;
    }
    return false;
}

int Renderer::BuildBookCard(const Newsletter& doc, size_t articleIndex, size_t bookIndex, int x, int y, int width,
                            int minHeight, std::vector<Item>* out) const {
    const Article& article = doc.articles[articleIndex];
    const Book& book = article.books[bookIndex];
    const Theme& theme = GetTheme(doc.theme);
    const bool twoColumns = article.bookColumns == 2;
    const int pad = 12;
    std::vector<Item> items;

    // 표지(있으면 왼쪽)
    int textLeft = x + pad;
    int coverBottom = y;
    if (!book.cover.empty() && book.coverWidth > 0 && book.coverHeight > 0) {
        int coverW = twoColumns ? 64 : 84;
        int coverH = std::clamp(coverW * book.coverHeight / book.coverWidth, coverW * 3 / 5, coverW * 2);
        items.push_back({Item::Cover, RECT{x + pad, y + pad, x + pad + coverW, y + pad + coverH}, 0, 0, L"", 0,
                         static_cast<int>(articleIndex), static_cast<int>(bookIndex)});
        textLeft += coverW + 14;
        coverBottom = y + pad + coverH;
    }
    const int textRight = x + width - pad;
    const int textWidth = std::max(40, textRight - textLeft);
    int ty = y + pad;

    auto lines = [&](int font, COLORREF color, const std::wstring& text, double spacing) {
        const int lh = LineHeight(font, spacing);
        for (const auto& para : SplitParagraphs(text)) {
            size_t pos = 0;
            do {
                while (pos < para.size() && para[pos] == L' ') ++pos;
                size_t next = para.size();
                std::wstring line = pos < para.size() ? TakeLine(font, para, pos, textWidth, &next) : L"";
                items.push_back({Item::Text, RECT{textLeft, ty, textRight, ty + lh}, color, font, line, DT_LEFT});
                ty += lh;
                pos = next;
            } while (pos < para.size());
        }
    };

    std::wstring title = Trim(book.title);
    lines(FBookTitle, title.empty() ? kMutedColor : kHeadingColor, title.empty() ? L"(서명 없음)" : L"『" + title + L"』",
          1.3);
    std::wstring meta;
    for (const std::wstring* part : {&book.author, &book.publisher, &book.year}) {
        if (Trim(*part).empty()) continue;
        if (!meta.empty()) meta += L" · ";
        meta += Trim(*part);
    }
    if (!meta.empty()) lines(FMeta, kMutedColor, meta, 1.4);
    if (!Trim(book.callNumber).empty())
        lines(FMeta, ToColorRef(theme.primary), L"청구기호 " + Trim(book.callNumber), 1.4);
    if (!Trim(book.description).empty()) {
        ty += 4;
        lines(twoColumns ? FMeta : FBody, kTextColor, book.description, 1.45);
    }

    int height = std::max({ty, coverBottom, y}) + pad - y;
    height = std::max(height, minHeight);
    if (out) {
        // 카드 바탕(옅은 테마색)을 먼저, 그 위에 표지·글자
        out->push_back({Item::RoundFill, RECT{x, y, x + width, y + height}, ToColorRef(theme.light), 0, L"", 0});
        out->insert(out->end(), items.begin(), items.end());
    }
    return height;
}

// 도서 카드를 1열 또는 2열로 놓습니다. 카드(2열이면 한 줄의 두 카드)는 쪽 사이에서 나뉘지 않고,
// 글 피해 가기 그림과 겹치면 그 아래로 내려갑니다.
void Renderer::AddBooks(const Newsletter& doc, size_t articleIndex) {
    const Article& article = doc.articles[articleIndex];
    if (article.books.empty()) return;
    const int columns = article.bookColumns == 2 ? 2 : 1;
    const int gap = 12;
    const int columnWidth = columns == 2 ? (kContentWidth - gap) / 2 : kContentWidth;

    if (!AtTop()) y_ += 14;
    for (size_t first = 0; first < article.books.size(); first += columns) {
        size_t count = std::min<size_t>(columns, article.books.size() - first);
        int rowHeight = 0;
        for (size_t c = 0; c < count; ++c)
            rowHeight = std::max(rowHeight, BuildBookCard(doc, articleIndex, first + c, 0, 0, columnWidth, 0, nullptr));

        for (int guard = 0; guard < 200; ++guard) {
            if (y_ + rowHeight > kContentBottom && !AtTop()) {
                NewPage();
                continue;
            }
            if (!Blocked(y_, y_ + rowHeight, kMarginLeft, kMarginRight)) break;
            int next = NextFreeY(y_, y_ + rowHeight, kMarginLeft, kMarginRight);
            if (next + rowHeight > kContentBottom) NewPage();
            else y_ = next;
        }
        for (size_t c = 0; c < count; ++c) {
            int x = kMarginLeft + static_cast<int>(c) * (columnWidth + gap);
            BuildBookCard(doc, articleIndex, first + c, x, y_, columnWidth, rowHeight, &pages_[page_]);
        }
        y_ += rowHeight + gap;
    }
    y_ -= gap;
}

void Renderer::AddPublisher(const Newsletter& doc) {
    const int lh = LineHeight(FMeta, 1.5);
    if (!AtTop()) y_ += 44;
    if (y_ + 12 + lh > kContentBottom && !AtTop()) NewPage();
    Span rule = FreeSpan(y_, y_ + 1, kMarginLeft, kMarginRight, 40);
    if (!rule.empty()) Add({Item::Fill, RECT{rule.left, y_, rule.right, y_ + 1}, kRuleColor, 0, L"", 0});
    y_ += 12;
    for (const auto& para : SplitParagraphs(doc.publisher))
        FlowText(FMeta, kMutedColor, para, kMarginLeft, kMarginRight, lh, DT_CENTER);
}

int Renderer::AnchorFor(int page, int y) const {
    int anchor = -1;
    for (size_t i = 0; i < articleMarks_.size(); ++i) {
        const Mark& m = articleMarks_[i];
        if (m.page < page || (m.page == page && m.y <= y)) anchor = static_cast<int>(i);
    }
    return anchor;
}

void Renderer::DrawPage(HDC hdc, int page, const Newsletter& doc, ImageLibrary* images, bool forPrint) const {
    if (page < 0 || page >= PageCount()) return;

    int saved = SaveDC(hdc);
    IntersectClipRect(hdc, 0, 0, kPageWidth, kPageHeight);
    SetBkMode(hdc, TRANSPARENT);

    HBRUSH white = CreateSolidBrush(RGB(255, 255, 255));
    RECT pageRect{0, 0, kPageWidth, kPageHeight};
    FillRect(hdc, &pageRect, white);
    DeleteObject(white);

    auto draw = [&](const Item& item) {
        RECT rc = item.rc;
        switch (item.type) {
            case Item::Fill: {
                HBRUSH b = CreateSolidBrush(item.color);
                FillRect(hdc, &rc, b);
                DeleteObject(b);
                break;
            }
            case Item::Cover: {
                if (!images || item.article < 0 || item.article >= static_cast<int>(doc.articles.size())) break;
                const auto& books = doc.articles[item.article].books;
                if (item.book < 0 || item.book >= static_cast<int>(books.size())) break;
                const Book& b = books[item.book];
                if (!b.cover.empty()) images->Draw(hdc, b.coverId, b.cover, rc, forPrint);
                break;
            }
            case Item::Dot: {
                HBRUSH b = CreateSolidBrush(item.color);
                HGDIOBJ oldBrush = SelectObject(hdc, b);
                HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));
                Ellipse(hdc, rc.left, rc.top, rc.right + 1, rc.bottom + 1);
                SelectObject(hdc, oldPen);
                SelectObject(hdc, oldBrush);
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
                HGDIOBJ oldFont = SelectObject(hdc, Font(item.font));
                SetTextColor(hdc, item.color);
                DrawTextW(hdc, item.text.c_str(), static_cast<int>(item.text.size()), &rc,
                          item.format | DT_SINGLELINE | DT_NOPREFIX | DT_NOCLIP);
                SelectObject(hdc, oldFont);
                break;
            }
        }
    };

    for (const Item& item : pages_[page]) draw(item);

    // 쪽 바닥글
    const int footH = 20;
    const int footTop = kFooterRuleY + 10;
    draw({Item::Fill, RECT{kMarginLeft, kFooterRuleY, kMarginRight, kFooterRuleY + 1}, kRuleColor, 0, L"", 0});
    if (!footerLeft_.empty())
        draw({Item::Text, RECT{kMarginLeft, footTop, kMarginRight - 80, footTop + footH}, kMutedColor, FFooter,
              footerLeft_, DT_LEFT | DT_END_ELLIPSIS});
    std::wstring pageText = std::to_wstring(page + 1) + L" / " + std::to_wstring(PageCount());
    draw({Item::Text, RECT{kMarginRight - 80, footTop, kMarginRight, footTop + footH}, kMutedColor, FFooter, pageText,
          DT_RIGHT});

    // 그림은 글 위에 그립니다(목록 뒤쪽 그림이 더 위).
    if (images) {
        for (const Picture& p : doc.pictures) {
            if (p.page != page) continue;
            RECT rc{p.x, p.y, p.x + p.width, p.y + p.height};
            images->Draw(hdc, p, rc, forPrint);
        }
    }

    RestoreDC(hdc, saved);
}

}  // namespace nl
