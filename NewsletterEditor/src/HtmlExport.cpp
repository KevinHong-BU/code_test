#include "HtmlExport.h"

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
    h += L"body{margin:0;background:#e9ebee;font-family:'Malgun Gothic','맑은 고딕','Apple SD Gothic Neo',"
         L"sans-serif;color:#222;line-height:1.7}\n";
    h += L".paper{max-width:760px;margin:24px auto;background:#fff;padding:40px 48px;"
         L"box-shadow:0 2px 10px rgba(0,0,0,.15)}\n";
    h += L".masthead{background:" + Css(theme.primary) + L";color:#fff;border-radius:8px;padding:22px 28px;"
         L"border-bottom:6px solid " + Css(theme.accent) + L"}\n";
    h += L".library{font-size:15px;opacity:.85}\n";
    h += L".masthead h1{margin:4px 0 10px;font-size:34px;line-height:1.25}\n";
    h += L".meta{font-size:13px;opacity:.9}\n";
    h += L".greeting{background:" + Css(theme.light) + L";border-left:4px solid " + Css(theme.primary) +
         L";padding:14px 18px;margin:24px 0 8px;border-radius:0 6px 6px 0}\n";
    h += L".article{margin-top:30px}\n";
    h += L".tag{display:inline-block;color:#fff;font-size:12px;font-weight:bold;border-radius:10px;"
         L"padding:1px 10px;margin-bottom:4px}\n";
    h += L".article h2{margin:2px 0 8px;font-size:21px;padding-bottom:6px;border-bottom:2px solid " +
         Css(theme.light) + L"}\n";
    h += L".article p{margin:8px 0}\n.article ul{margin:8px 0;padding-left:22px}\n";
    h += L".footer{margin-top:40px;padding-top:12px;border-top:1px solid #ccc;color:#666;font-size:13px;"
         L"text-align:center}\n";
    h += L"@media print{body{background:#fff}.paper{box-shadow:none;margin:0;max-width:none}"
         L".masthead,.tag,.greeting{-webkit-print-color-adjust:exact;print-color-adjust:exact}}\n";
    h += L"</style>\n</head>\n<body>\n<div class=\"paper\">\n";

    h += L"<header class=\"masthead\">\n";
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

    for (const Article& a : doc.articles) {
        h += L"<section class=\"article\">\n";
        if (!Trim(a.category).empty()) {
            h += L"<span class=\"tag\" style=\"background:" + Css(CategoryColor(Trim(a.category), doc.theme)) +
                 L"\">" + HtmlEscape(Trim(a.category)) + L"</span>\n";
        }
        h += L"<h2>" + HtmlEscape(Trim(a.title)) + L"</h2>\n";
        h += BodyHtml(a.body);
        h += L"</section>\n";
    }

    if (!Trim(doc.publisher).empty())
        h += L"<footer class=\"footer\">" + MultiLine(doc.publisher) + L"</footer>\n";
    h += L"</div>\n</body>\n</html>\n";
    return ToUtf8(h);
}

}  // namespace nl
