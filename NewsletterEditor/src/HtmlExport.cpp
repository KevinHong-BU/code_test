#include "HtmlExport.h"

#include <algorithm>
#include <cstdio>

namespace nl {

namespace {

std::wstring Css(Color c) {
    wchar_t buf[8];
    std::swprintf(buf, 8, L"#%02x%02x%02x", c.r, c.g, c.b);
    return buf;
}

// 줄바꿈을 <br>로 바꿔 여러 줄 텍스트를 그대로 보여 줍니다.
std::wstring MultiLine(const std::wstring& s) {
    std::wstring out;
    std::wstring text = HtmlEscape(NormalizeNewlines(Trim(s)));
    for (wchar_t c : text) {
        if (c == L'\n') out += L"<br>\n";
        else out += c;
    }
    return out;
}

std::wstring BodyHtml(const std::wstring& body) {
    std::wstring out;
    enum { Closed, Para, List } state = Closed;
    auto close = [&] {
        if (state == Para) out += L"</p>\n";
        else if (state == List) out += L"</ul>\n";
        state = Closed;
    };
    for (const BodyLine& line : ParseBody(body)) {
        switch (line.kind) {
            case BodyLine::Blank:
                close();
                break;
            case BodyLine::Bullet:
                if (state != List) {
                    close();
                    out += L"<ul>\n";
                    state = List;
                }
                out += L"<li>" + HtmlEscape(line.text) + L"</li>\n";
                break;
            case BodyLine::Text:
                if (state == Para) {
                    out += L"<br>\n";
                } else {
                    close();
                    out += L"<p>";
                    state = Para;
                }
                out += HtmlEscape(line.text);
                break;
        }
    }
    close();
    return out;
}

const int kContentWidth = 688;  // Renderer의 본문 폭(논리 단위)과 같음

std::wstring ImageTag(const Picture& p, const std::wstring& cls, const std::wstring& style) {
    std::string mime = ImageMimeType(p.data);
    if (mime.empty()) return L"";
    return L"<img class=\"" + cls + L"\" style=\"" + style + L"\" alt=\"\" src=\"data:" + FromUtf8(mime) + L";base64," +
           FromUtf8(Base64Encode(p.data)) + L"\">\n";
}

// 쪽 위에 자유롭게 놓인 사진을, HTML의 흐름 배치에 맞게 기준 기사 안에 띄워(float) 넣습니다.
// 폭은 본문 폭에 대한 비율로, 좌우는 그림 중심이 쪽 가운데의 어느 쪽에 있는지로 정합니다.
std::wstring PhotoHtml(const Picture& p) {
    int percent = std::clamp(p.width * 100 / kContentWidth, 10, 100);
    std::wstring style = L"width:" + std::to_wstring(percent) + L"%;";
    if (percent > 60) style += L"display:block;margin:12px auto;";
    else if (p.x + p.width / 2 < 400) style += L"float:left;margin:4px 16px 8px 0;";
    else style += L"float:right;margin:4px 0 8px 16px;";
    return ImageTag(p, L"photo", style);
}

// 기본 크기일 때의 px 값을 문서 글자 크기 비율로 늘이거나 줄입니다.
std::wstring Px(double basePx, int size, int defaultSize) {
    double px = basePx * size / defaultSize;
    wchar_t buf[32];
    std::swprintf(buf, 32, L"%.1fpx", px);
    return buf;
}

// CSS font-family 값: 고른 글꼴 다음에, 그 글꼴이 없는 컴퓨터를 위한 대체 글꼴을 붙입니다.
std::wstring FontStack(const std::wstring& face) {
    std::wstring name;
    for (wchar_t c : FontOrDefault(face)) {
        if (c != L'\'' && c != L'"' && c != L'\\' && c != L'<' && c != L'>' && c != L';' && c != L'{' && c != L'}')
            name += c;
    }
    return L"'" + name + L"','Malgun Gothic','맑은 고딕','Apple SD Gothic Neo',sans-serif";
}

}  // namespace

std::wstring HtmlEscape(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) {
        switch (c) {
            case L'&': out += L"&amp;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            case L'"': out += L"&quot;"; break;
            case L'\'': out += L"&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string ExportHtml(const Newsletter& doc) {
    const Theme& theme = GetTheme(doc.theme);
    std::wstring docTitle = Trim(doc.title).empty() ? L"도서관 소식지" : Trim(doc.title);
    if (!Trim(doc.issue).empty()) docTitle += L" " + Trim(doc.issue);

    std::wstring h;
    h += L"<!DOCTYPE html>\n<html lang=\"ko\">\n<head>\n<meta charset=\"utf-8\">\n";
    h += L"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
    h += L"<title>" + HtmlEscape(docTitle) + L"</title>\n<style>\n";
    h += L"body{margin:0;background:#e9ebee;font-family:" + FontStack(doc.bodyFont) +
         L";font-size:" + Px(16, doc.bodySize, kDefaultBodySize) + L";color:#222;line-height:1.7}\n";
    h += L".masthead h1,.article h2,.tag{font-family:" + FontStack(doc.headingFont) + L"}\n";
    h += L".paper{max-width:760px;margin:24px auto;background:#fff;padding:40px 48px;"
         L"box-shadow:0 2px 10px rgba(0,0,0,.15)}\n";
    h += L".masthead{background:" + Css(theme.primary) + L";color:#fff;border-radius:8px;padding:22px 28px;"
         L"border-bottom:6px solid " + Css(theme.accent) + L"}\n";
    const wchar_t* titleAlign = doc.titleAlign == TitleAlign::Center  ? L"center"
                                : doc.titleAlign == TitleAlign::Right ? L"right"
                                                                      : L"left";
    h += L".masthead .library,.masthead h1,.masthead .meta{text-align:" + std::wstring(titleAlign) + L"}\n";
    h += L".library{font-size:" + Px(15, doc.bodySize, kDefaultBodySize) + L";opacity:.85}\n";
    h += L".masthead h1{margin:4px 0 10px;font-size:" + Px(34, doc.titleSize, kDefaultTitleSize) +
         L";line-height:1.25}\n";
    h += L".meta{font-size:" + Px(13, doc.bodySize, kDefaultBodySize) + L";opacity:.9}\n";
    h += L".greeting{background:" + Css(theme.light) + L";border-left:4px solid " + Css(theme.primary) +
         L";padding:14px 18px;margin:24px 0 8px;border-radius:0 6px 6px 0}\n";
    h += L".article{margin-top:30px}\n";
    h += L".tag{display:inline-block;color:#fff;font-size:" + Px(12, doc.headingSize, kDefaultHeadingSize) +
         L";font-weight:bold;border-radius:10px;"
         L"padding:1px 10px;margin-bottom:4px}\n";
    h += L".article h2{margin:2px 0 8px;font-size:" + Px(21, doc.headingSize, kDefaultHeadingSize) +
         L";padding-bottom:6px;border-bottom:2px solid " +
         Css(theme.light) + L"}\n";
    // overflow:hidden: 왼쪽에 띄운 사진 옆에서도 글머리표가 사진과 겹치지 않게 함
    h += L".article p{margin:8px 0}\n.article ul{margin:8px 0;padding-left:22px;overflow:hidden}\n";
    h += L".masthead::after{content:'';display:block;clear:both}\n.logo{float:right;max-width:40%;"
         L"margin-left:16px}\n";
    h += L".article::after{content:'';display:block;clear:both}\n.photo{border-radius:4px}\n";
    h += L".footer{margin-top:40px;padding-top:12px;border-top:1px solid #ccc;color:#666;font-size:" +
         Px(13, doc.bodySize, kDefaultBodySize) + L";"
         L"text-align:center}\n";
    h += L"@media print{body{background:#fff}.paper{box-shadow:none;margin:0;max-width:none}"
         L".masthead,.tag,.greeting{-webkit-print-color-adjust:exact;print-color-adjust:exact}}\n";
    h += L"</style>\n</head>\n<body>\n<div class=\"paper\">\n";

    h += L"<header class=\"masthead\">\n";
    for (const Picture& p : doc.pictures) {
        if (p.kind == Picture::Logo)
            h += ImageTag(p, L"logo", L"height:" + std::to_wstring(std::clamp(p.height, 24, 160)) + L"px");
    }
    if (!Trim(doc.libraryName).empty())
        h += L"<div class=\"library\">" + HtmlEscape(Trim(doc.libraryName)) + L"</div>\n";
    h += L"<h1>" + HtmlEscape(Trim(doc.title)) + L"</h1>\n";
    std::wstring meta;
    if (!Trim(doc.issue).empty()) meta = HtmlEscape(Trim(doc.issue));
    if (!Trim(doc.date).empty()) {
        if (!meta.empty()) meta += L" &nbsp;·&nbsp; ";
        meta += HtmlEscape(Trim(doc.date));
    }
    if (!meta.empty()) h += L"<div class=\"meta\">" + meta + L"</div>\n";
    h += L"</header>\n";

    if (!Trim(doc.greeting).empty())
        h += L"<div class=\"greeting\">" + MultiLine(doc.greeting) + L"</div>\n";

    auto photosFor = [&doc](int anchor) {
        std::wstring out;
        for (const Picture& p : doc.pictures) {
            int a = p.anchor < static_cast<int>(doc.articles.size()) ? p.anchor : static_cast<int>(doc.articles.size()) - 1;
            if (p.kind == Picture::Photo && a == anchor) out += PhotoHtml(p);
        }
        return out;
    };
    std::wstring topPhotos = photosFor(-1);
    if (!topPhotos.empty()) h += L"<div class=\"article\">\n" + topPhotos + L"</div>\n";

    for (size_t i = 0; i < doc.articles.size(); ++i) {
        const Article& a = doc.articles[i];
        h += L"<section class=\"article\">\n";
        if (!Trim(a.category).empty()) {
            h += L"<span class=\"tag\" style=\"background:" + Css(CategoryColor(Trim(a.category), doc.theme)) +
                 L"\">" + HtmlEscape(Trim(a.category)) + L"</span>\n";
        }
        h += L"<h2>" + HtmlEscape(Trim(a.title)) + L"</h2>\n";
        h += photosFor(static_cast<int>(i));
        h += BodyHtml(a.body);
        h += L"</section>\n";
    }

    if (!Trim(doc.publisher).empty())
        h += L"<footer class=\"footer\">" + MultiLine(doc.publisher) + L"</footer>\n";
    h += L"</div>\n</body>\n</html>\n";
    return ToUtf8(h);
}

}  // namespace nl
