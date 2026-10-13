// 데이터 모델·파일 형식·HTML 내보내기 단위 테스트 (Windows API 불필요).
#include <cstdio>
#include <string>

#include "../src/HtmlExport.h"
#include "../src/Newsletter.h"

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        ++g_checks;                                                       \
        if (!(cond)) {                                                    \
            ++g_failures;                                                 \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                 \
    } while (0)

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool Equal(const nl::Newsletter& a, const nl::Newsletter& b) {
    if (a.libraryName != b.libraryName || a.title != b.title || a.issue != b.issue || a.date != b.date ||
        a.publisher != b.publisher || a.greeting != b.greeting || a.theme != b.theme ||
        a.articles.size() != b.articles.size())
        return false;
    for (size_t i = 0; i < a.articles.size(); ++i) {
        const auto& x = a.articles[i];
        const auto& y = b.articles[i];
        if (x.category != y.category || x.title != y.title || x.body != y.body) return false;
    }
    return true;
}

void TestUtf8() {
    std::wstring korean = L"도서관 소식지";
    CHECK(nl::ToUtf8(korean) == "\xEB\x8F\x84\xEC\x84\x9C\xEA\xB4\x80 \xEC\x86\x8C\xEC\x8B\x9D\xEC\xA7\x80");
    CHECK(nl::FromUtf8(nl::ToUtf8(korean)) == korean);

    // BMP 밖 문자(이모지)도 왕복해야 합니다(Windows에서는 서로게이트 쌍).
    std::string bookEmoji = "\xF0\x9F\x93\x9A";
    CHECK(nl::ToUtf8(nl::FromUtf8(bookEmoji)) == bookEmoji);

    // 잘못된 바이트는 U+FFFD로 바뀌고 나머지는 그대로 해석됩니다.
    std::wstring broken = nl::FromUtf8("A\xFF" "B\xE2\x82");
    CHECK(broken == std::wstring(L"A\xFFFD" L"B\xFFFD\xFFFD"));
}

void TestRoundTrip() {
    nl::Newsletter doc = nl::CreateSample();
    doc.theme = 2;
    doc.articles.push_back({L"사용자 분류", L"특수문자 = \\ 테스트",
                            L"첫 줄\n\t탭 들어간 줄\n\\n 은 글자 그대로\n[article]\n# 주석 아님\n\xD55C\xAE00"});
    doc.articles.push_back({L"", L"", L""});

    std::string data = nl::Serialize(doc);
    CHECK(data.compare(0, 3, "\xEF\xBB\xBF") == 0);

    nl::Newsletter loaded;
    std::wstring error;
    CHECK(nl::Deserialize(data, loaded, error));
    CHECK(error.empty());
    CHECK(Equal(doc, loaded));

    // Windows 메모장 등에서 CRLF로 바뀌어도 읽을 수 있어야 합니다.
    std::string crlf;
    for (char c : data) {
        if (c == '\n') crlf += '\r';
        crlf += c;
    }
    nl::Newsletter loaded2;
    CHECK(nl::Deserialize(crlf, loaded2, error));
    CHECK(Equal(doc, loaded2));
}

void TestDeserializeErrors() {
    nl::Newsletter doc;
    std::wstring error;
    CHECK(!nl::Deserialize("hello world", doc, error));
    CHECK(!error.empty());

    error.clear();
    CHECK(!nl::Deserialize("[newsletter]\nformat=99\n", doc, error));
    CHECK(!error.empty());

    // 알 수 없는 키는 무시하고, 잘못된 테마 번호는 0으로 돌립니다.
    error.clear();
    CHECK(nl::Deserialize("[newsletter]\nformat=1\nfuture_key=x\ntheme=77\ntitle=T\n", doc, error));
    CHECK(doc.title == L"T");
    CHECK(doc.theme == 0);
    CHECK(doc.articles.empty());
}

void TestParseBody() {
    auto lines = nl::ParseBody(L"\n\n소개 문장\n- 첫째\n* 둘째\n\n• 셋째\n마지막 \n\n");
    CHECK(lines.size() == 6);
    if (lines.size() == 6) {
        CHECK(lines[0].kind == nl::BodyLine::Text && lines[0].text == L"소개 문장");
        CHECK(lines[1].kind == nl::BodyLine::Bullet && lines[1].text == L"첫째");
        CHECK(lines[2].kind == nl::BodyLine::Bullet && lines[2].text == L"둘째");
        CHECK(lines[3].kind == nl::BodyLine::Blank);
        CHECK(lines[4].kind == nl::BodyLine::Bullet && lines[4].text == L"셋째");
        CHECK(lines[5].kind == nl::BodyLine::Text && lines[5].text == L"마지막");
    }
    CHECK(nl::ParseBody(L"").empty());
    // "-"만 있거나 붙어 있으면 글머리표가 아닙니다.
    CHECK(nl::ParseBody(L"-5도")[0].kind == nl::BodyLine::Text);
}

void TestNewlines() {
    CHECK(nl::NormalizeNewlines(L"a\r\nb\rc\nd") == L"a\nb\nc\nd");
    CHECK(nl::ToCrLf(L"a\nb\r\nc") == L"a\r\nb\r\nc");
    CHECK(nl::Trim(L"  \t가나 \n") == L"가나");
}

void TestHtml() {
    CHECK(nl::HtmlEscape(L"<a href=\"x\">&'") == L"&lt;a href=&quot;x&quot;&gt;&amp;&#39;");

    nl::Newsletter doc = nl::CreateBlank();
    doc.title = L"<script>alert(1)</script>";
    doc.greeting = L"첫 줄\n둘째 줄";
    doc.articles = {{L"공지사항", L"휴관 안내", L"안내 문장\n- 월요일\n- 공휴일\n\n끝 문단"}};
    std::string html = nl::ExportHtml(doc);

    CHECK(Contains(html, "<!DOCTYPE html>"));
    CHECK(Contains(html, "<meta charset=\"utf-8\">"));
    CHECK(!Contains(html, "<script>"));
    CHECK(Contains(html, "&lt;script&gt;"));
    CHECK(Contains(html, nl::ToUtf8(L"첫 줄<br>\n둘째 줄")));
    CHECK(Contains(html, nl::ToUtf8(L"<p>안내 문장</p>\n<ul>\n<li>월요일</li>\n<li>공휴일</li>\n</ul>\n<p>끝 문단</p>")));
    CHECK(Contains(html, "background:#c62828"));  // 공지사항 분류 색
}

void TestSampleAndThemes() {
    nl::Newsletter sample = nl::CreateSample();
    CHECK(!sample.articles.empty());
    CHECK(!sample.date.empty());
    CHECK(nl::ThemeCount() >= 2);
    CHECK(&nl::GetTheme(-1) == &nl::GetTheme(0));
    CHECK(&nl::GetTheme(nl::ThemeCount()) == &nl::GetTheme(0));
    nl::Color unknown = nl::CategoryColor(L"없는 분류", 1);
    nl::Color primary = nl::GetTheme(1).primary;
    CHECK(unknown.r == primary.r && unknown.g == primary.g && unknown.b == primary.b);
}

}  // namespace

int main() {
    TestUtf8();
    TestRoundTrip();
    TestDeserializeErrors();
    TestParseBody();
    TestNewlines();
    TestHtml();
    TestSampleAndThemes();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
