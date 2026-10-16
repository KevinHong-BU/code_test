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
        a.headingFont != b.headingFont || a.bodyFont != b.bodyFont || a.headingSize != b.headingSize ||
        a.bodySize != b.bodySize || a.titleSize != b.titleSize || a.titleAlign != b.titleAlign ||
        a.titleOffsetX != b.titleOffsetX || a.titleOffsetY != b.titleOffsetY ||
        a.articles.size() != b.articles.size())
        return false;
    for (size_t i = 0; i < a.articles.size(); ++i) {
        const auto& x = a.articles[i];
        const auto& y = b.articles[i];
        if (x.category != y.category || x.title != y.title || x.body != y.body || x.titleFont != y.titleFont ||
            x.titleSize != y.titleSize || x.titleBold != y.titleBold || x.titleAlign != y.titleAlign ||
            x.pageBreakBefore != y.pageBreakBefore || x.bookColumns != y.bookColumns ||
            x.books.size() != y.books.size())
            return false;
        for (size_t k = 0; k < x.books.size(); ++k) {
            const auto& p = x.books[k];
            const auto& q = y.books[k];
            if (p.title != q.title || p.author != q.author || p.publisher != q.publisher || p.year != q.year ||
                p.callNumber != q.callNumber || p.description != q.description || p.cover != q.cover ||
                p.coverWidth != q.coverWidth || p.coverHeight != q.coverHeight)
                return false;
        }
    }
    auto sameBackground = [](const nl::Background& x, const nl::Background& y) {
        return x.data == y.data && (x.data.empty() || (x.width == y.width && x.height == y.height &&
                                                       x.fit == y.fit && x.opacity == y.opacity));
    };
    if (!sameBackground(a.background, b.background) || a.pageBackgrounds.size() != b.pageBackgrounds.size())
        return false;
    for (const auto& entry : a.pageBackgrounds) {
        auto it = b.pageBackgrounds.find(entry.first);
        if (it == b.pageBackgrounds.end() || !sameBackground(entry.second, it->second)) return false;
    }
    if (a.pictures.size() != b.pictures.size()) return false;
    for (size_t i = 0; i < a.pictures.size(); ++i) {
        const auto& x = a.pictures[i];
        const auto& y = b.pictures[i];
        if (x.kind != y.kind || x.data != y.data || x.page != y.page || x.x != y.x || x.y != y.y ||
            x.width != y.width || x.height != y.height || x.wrapText != y.wrapText)
            return false;
    }
    return true;
}

// 8바이트 PNG 서명 + 임의 바이트(0과 0xFF 포함)로 된 가짜 그림 데이터
std::string FakePng() {
    std::string data("\x89PNG\r\n\x1a\n", 8);
    for (int i = 0; i < 300; ++i) data += static_cast<char>(i * 37);
    return data;
}

void TestBase64() {
    CHECK(nl::Base64Encode("") == "");
    CHECK(nl::Base64Encode("f") == "Zg==");
    CHECK(nl::Base64Encode("fo") == "Zm8=");
    CHECK(nl::Base64Encode("foo") == "Zm9v");
    CHECK(nl::Base64Encode("foobar") == "Zm9vYmFy");
    std::string out;
    CHECK(nl::Base64Decode("Zm9vYmE=", out) && out == "fooba");
    CHECK(nl::Base64Decode("Zm9v\nYmFy", out) && out == "foobar");
    CHECK(!nl::Base64Decode("Zm9v*", out));
    CHECK(!nl::Base64Decode("Zg==Zg", out));
    std::string bin = FakePng();
    CHECK(nl::Base64Decode(nl::Base64Encode(bin), out) && out == bin);
}

void TestMimeType() {
    CHECK(nl::ImageMimeType(FakePng()) == "image/png");
    CHECK(nl::ImageMimeType(std::string("\xFF\xD8\xFF\xE0rest", 8)) == "image/jpeg");
    CHECK(nl::ImageMimeType("GIF89a...") == "image/gif");
    CHECK(nl::ImageMimeType("BM....") == "image/bmp");
    CHECK(nl::ImageMimeType("hello").empty());
}

// 방향 태그 하나만 든 최소 EXIF JPEG를 만듭니다.
std::string ExifJpeg(bool little, int orientation) {
    auto put = [little](std::string& out, unsigned long v, int bytes) {
        for (int i = 0; i < bytes; ++i) {
            int shift = little ? 8 * i : 8 * (bytes - 1 - i);
            out += static_cast<char>((v >> shift) & 0xFF);
        }
    };
    std::string tiff = little ? "II" : "MM";
    put(tiff, 42, 2);
    put(tiff, 8, 4);       // 첫 IFD 위치
    put(tiff, 1, 2);       // 항목 1개
    put(tiff, 0x0112, 2);  // 방향 태그
    put(tiff, 3, 2);       // SHORT
    put(tiff, 1, 4);       // 개수
    put(tiff, static_cast<unsigned long>(orientation), 2);
    put(tiff, 0, 2);       // 값 칸 나머지
    put(tiff, 0, 4);       // 다음 IFD 없음
    std::string app1 = std::string("Exif\0\0", 6) + tiff;
    size_t len = app1.size() + 2;
    std::string jpeg("\xFF\xD8\xFF\xE0\0\x04\0\0", 8);  // 짧은 APP0 구획을 먼저 둠
    jpeg += std::string("\xFF\xE1", 2) + static_cast<char>(len >> 8) + static_cast<char>(len & 0xFF) + app1;
    jpeg += std::string("\xFF\xD9", 2);
    return jpeg;
}

void TestJpegOrientation() {
    CHECK(nl::JpegOrientation(ExifJpeg(true, 6)) == 6);
    CHECK(nl::JpegOrientation(ExifJpeg(false, 8)) == 8);
    CHECK(nl::JpegOrientation(ExifJpeg(false, 3)) == 3);
    CHECK(nl::JpegOrientation(ExifJpeg(true, 42)) == 1);  // 잘못된 값
    CHECK(nl::JpegOrientation(FakePng()) == 1);
    CHECK(nl::JpegOrientation(std::string("\xFF\xD8\xFF\xE1\0", 5)) == 1);  // 잘린 파일
    std::string truncated = ExifJpeg(true, 6);
    truncated.resize(truncated.size() / 2);
    int value = nl::JpegOrientation(truncated);
    CHECK(value >= 1 && value <= 8);
}

void TestPictures() {
    nl::Newsletter doc = nl::CreateSample();
    nl::Picture logo;
    logo.kind = nl::Picture::Logo;
    logo.id = doc.NextPictureId();
    logo.data = FakePng();
    logo.x = 500;
    logo.y = 70;
    logo.width = 160;
    logo.height = 90;
    doc.pictures.push_back(logo);
    nl::Picture photo = logo;
    photo.kind = nl::Picture::Photo;
    photo.id = doc.NextPictureId();
    photo.page = 1;
    photo.x = 60;
    photo.wrapText = false;
    doc.pictures.push_back(photo);
    CHECK(photo.id == 2);

    std::string data = nl::Serialize(doc);
    CHECK(Contains(data, "format=2"));
    nl::Newsletter loaded;
    std::wstring error;
    CHECK(nl::Deserialize(data, loaded, error));
    CHECK(Equal(doc, loaded));
    CHECK(loaded.pictures.size() == 2 && loaded.pictures[0].id != loaded.pictures[1].id);

    // 손상된 그림 데이터는 오류로 알립니다.
    std::string broken = data;
    size_t at = broken.find("data=");
    broken.insert(at + 5, "*");
    CHECK(!nl::Deserialize(broken, loaded, error));
    CHECK(!error.empty());

    // 크기가 없는 그림은 버립니다.
    error.clear();
    CHECK(nl::Deserialize("[newsletter]\nformat=2\n[picture]\nkind=photo\nwidth=0\nheight=10\ndata=Zm9v\n", loaded,
                          error));
    CHECK(loaded.pictures.empty());

    // 그림 기능 이전(format=1) 파일도 그대로 열립니다.
    CHECK(nl::Deserialize("[newsletter]\nformat=1\ntitle=T\n[article]\ntitle=A\n", loaded, error));
    CHECK(loaded.articles.size() == 1 && loaded.pictures.empty());
}

void TestFonts() {
    CHECK(nl::FontOrDefault(L"") == nl::kDefaultFont);
    CHECK(nl::FontOrDefault(L"  ") == nl::kDefaultFont);
    CHECK(nl::FontOrDefault(L" 나눔명조 ") == L"나눔명조");

    // 기본 글꼴이면 파일에 적지 않고, 고른 글꼴은 그대로 다시 읽힙니다.
    nl::Newsletter doc = nl::CreateSample();
    CHECK(!Contains(nl::Serialize(doc), "Font="));
    doc.headingFont = L"나눔명조";
    doc.bodyFont = L"Noto Sans KR";
    nl::Newsletter loaded;
    std::wstring error;
    CHECK(nl::Deserialize(nl::Serialize(doc), loaded, error));
    CHECK(loaded.headingFont == L"나눔명조" && loaded.bodyFont == L"Noto Sans KR");

    // HTML: 고른 글꼴 뒤에 대체 글꼴이 붙고, CSS를 깨뜨릴 수 있는 글자는 빠집니다.
    doc.headingFont = L"Evil'}</style><script>";
    std::string html = nl::ExportHtml(doc);
    CHECK(Contains(html, nl::ToUtf8(L"font-family:'Noto Sans KR','Malgun Gothic','맑은 고딕'")));
    CHECK(Contains(html, "font-family:'Evil/stylescript','Malgun Gothic'"));
    CHECK(!Contains(html, "<script>"));
}

void TestFontSizes() {
    int v = 0;
    CHECK(nl::ParseFontSize(L"11", &v) && v == 22);
    CHECK(nl::ParseFontSize(L" 10.5 ", &v) && v == 21);
    CHECK(nl::ParseFontSize(L"10.5pt", &v) && v == 21);
    CHECK(nl::ParseFontSize(L"12.3", &v) && v == 25);  // 0.5pt 단위로 반올림 -> 12.5
    CHECK(nl::ParseFontSize(L"72", &v) && v == 144);
    v = 7;
    CHECK(!nl::ParseFontSize(L"", &v) && v == 7);
    CHECK(!nl::ParseFontSize(L".", &v));
    CHECK(!nl::ParseFontSize(L"5", &v));      // 너무 작음
    CHECK(!nl::ParseFontSize(L"73", &v));     // 너무 큼
    CHECK(!nl::ParseFontSize(L"1.2.3", &v));
    CHECK(!nl::ParseFontSize(L"-11", &v));
    CHECK(!nl::ParseFontSize(L"abc", &v));
    CHECK(!nl::ParseFontSize(L"99999999999", &v));
    CHECK(nl::FormatFontSize(22) == L"11");
    CHECK(nl::FormatFontSize(21) == L"10.5");

    // 크기·제목 배치가 저장되고 다시 읽힘
    nl::Newsletter doc = nl::CreateSample();
    doc.headingSize = 40;
    doc.bodySize = 25;
    doc.titleSize = 72;
    doc.titleAlign = nl::TitleAlign::Right;
    doc.titleOffsetX = -35;
    doc.titleOffsetY = 12;
    std::string data = nl::Serialize(doc);
    CHECK(Contains(data, "bodySize=12.5"));
    CHECK(Contains(data, "titleAlign=right"));
    nl::Newsletter loaded;
    std::wstring error;
    CHECK(nl::Deserialize(data, loaded, error));
    CHECK(Equal(doc, loaded));

    // 이전 파일(값 없음)은 기본값, 잘못된 값은 무시
    CHECK(nl::Deserialize("[newsletter]\nformat=2\nbodySize=abc\ntitleSize=500\ntitleOffsetX=99999\n", loaded,
                          error));
    CHECK(loaded.bodySize == nl::kDefaultBodySize && loaded.titleSize == nl::kDefaultTitleSize);
    CHECK(loaded.headingSize == nl::kDefaultHeadingSize && loaded.titleAlign == nl::TitleAlign::Left);
    CHECK(loaded.titleOffsetX == nl::kMaxTitleOffset);

    // HTML: 기본 크기면 원래 px 그대로, 바꾸면 비율대로. 제목 정렬 반영.
    std::string html = nl::ExportHtml(nl::CreateSample());
    CHECK(Contains(html, "font-size:16.0px") && Contains(html, "font-size:34.0px"));
    CHECK(Contains(html, "text-align:left"));
    html = nl::ExportHtml(doc);
    CHECK(Contains(html, "font-size:18.2px"));  // 16 * 25/22
    CHECK(Contains(html, "font-size:45.3px"));  // 34 * 72/54
    CHECK(Contains(html, "text-align:right"));
}

void TestBooksAndPageBreaks() {
    // 본문 안의 쪽 나누기 표시
    auto lines = nl::ParseBody(L"앞\n[쪽 나누기]\n  [쪽나누기]  \n뒤 [쪽 나누기]");
    CHECK(lines.size() == 4);
    if (lines.size() == 4) {
        CHECK(lines[1].kind == nl::BodyLine::PageBreak && lines[2].kind == nl::BodyLine::PageBreak);
        CHECK(lines[3].kind == nl::BodyLine::Text);  // 다른 글자와 같은 줄이면 쪽 나누기가 아님
    }

    // 도서·기사 서식이 저장되고 다시 읽힘
    nl::Newsletter doc = nl::CreateSample();
    nl::Article& a = doc.articles[2];
    CHECK(a.books.size() == 4 && a.bookColumns == 2);
    a.titleFont = L"나눔명조";
    a.titleSize = 44;
    a.titleBold = false;
    a.titleAlign = nl::TitleAlign::Center;
    a.pageBreakBefore = true;
    a.books[1].description = L"여러 줄\n소개 = \\ 특수문자";
    a.books[1].cover = FakePng();
    a.books[1].coverWidth = 300;
    a.books[1].coverHeight = 440;
    a.books[1].coverId = doc.NextPictureId();
    CHECK(doc.NextPictureId() == a.books[1].coverId + 1);  // 표지 번호도 고려

    std::string data = nl::Serialize(doc);
    nl::Newsletter loaded;
    std::wstring error;
    CHECK(nl::Deserialize(data, loaded, error));
    CHECK(Equal(doc, loaded));
    CHECK(loaded.articles[2].books[1].coverId > 0);

    // 도서가 기사 밖에 있으면 오류, 크기 정보 없는 표지는 버림
    CHECK(!nl::Deserialize("[newsletter]\nformat=2\n[book]\ntitle=x\n", loaded, error));
    CHECK(nl::Deserialize("[newsletter]\nformat=2\n[article]\ntitle=A\n[book]\ntitle=B\ncover=Zm9v\n", loaded, error));
    CHECK(loaded.articles.size() == 1 && loaded.articles[0].books.size() == 1);
    CHECK(loaded.articles[0].books[0].cover.empty());

    // HTML: 2열 도서 카드, 표지, 기사 제목 서식, 쪽 나누기
    a.body = L"첫 문단\n[쪽 나누기]\n둘째 문단";
    std::string html = nl::ExportHtml(doc);
    CHECK(Contains(html, "<div class=\"books cols-2\">"));
    CHECK(Contains(html, "class=\"cover\" alt=\"\" src=\"data:image/png;base64,"));
    CHECK(Contains(html, nl::ToUtf8(L"<div class=\"book-call\">청구기호 440-이44ㅊ</div>")));
    CHECK(Contains(html, nl::ToUtf8(L"여러 줄<br>\n소개 = \\ 특수문자")));
    CHECK(Contains(html, "<section class=\"article new-page\">"));
    CHECK(Contains(html, "<div class=\"page-break\"></div>"));
    CHECK(Contains(html, "<div style=\"text-align:center\">"));
    CHECK(Contains(html, nl::ToUtf8(L"<h2 style=\"font-family:'나눔명조'")));
    CHECK(Contains(html, "font-size:28.9px;font-weight:normal;"));  // 21 * 44/32 = 28.875
}

void TestBackgrounds() {
    nl::Newsletter doc = nl::CreateSample();
    CHECK(doc.BackgroundFor(0) == nullptr);

    nl::Background all;
    all.data = FakePng();
    all.width = 200;
    all.height = 100;
    all.fit = nl::BackgroundFit::Tile;
    all.opacity = 50;
    all.id = doc.NextPictureId();
    doc.background = all;
    CHECK(doc.NextPictureId() == all.id + 1);
    CHECK(doc.BackgroundFor(0) == &doc.background && doc.BackgroundFor(7) == &doc.background);

    nl::Background cover = all;
    cover.fit = nl::BackgroundFit::Contain;
    cover.opacity = 15;
    cover.id = doc.NextPictureId();
    doc.pageBackgrounds[0] = cover;           // 1쪽은 따로
    doc.pageBackgrounds[2] = nl::Background{};  // 3쪽은 배경 없음
    CHECK(doc.BackgroundFor(0) == &doc.pageBackgrounds[0]);
    CHECK(doc.BackgroundFor(1) == &doc.background);
    CHECK(doc.BackgroundFor(2) == nullptr);

    std::string data = nl::Serialize(doc);
    CHECK(Contains(data, "page=all") && Contains(data, "fit=tile") && Contains(data, "fit=contain"));
    nl::Newsletter loaded;
    std::wstring error;
    CHECK(nl::Deserialize(data, loaded, error));
    CHECK(Equal(doc, loaded));
    CHECK(loaded.BackgroundFor(2) == nullptr && loaded.BackgroundFor(1) != nullptr);
    CHECK(loaded.background.id != loaded.pageBackgrounds[0].id);

    // 잘못된 값: 쪽 번호가 이상하면 버리고, 진하기는 범위 안으로
    CHECK(nl::Deserialize("[newsletter]\nformat=2\n[background]\npage=-3\nwidth=10\nheight=10\ndata=Zm9v\n"
                          "[background]\npage=all\nopacity=900\nwidth=10\nheight=10\ndata=Zm9v\n",
                          loaded, error));
    CHECK(loaded.pageBackgrounds.empty());
    CHECK(loaded.background.opacity == nl::kMaxOpacity && !loaded.background.data.empty());

    // HTML: 모든 쪽 배경만, 진하기는 흰 막으로
    std::string html = nl::ExportHtml(doc);
    CHECK(Contains(html, "rgba(255,255,255,0.50)"));
    CHECK(Contains(html, "0 0/25% auto repeat"));
    CHECK(!Contains(nl::ExportHtml(nl::CreateSample()), "linear-gradient"));
}

void TestHtmlPictures() {
    nl::Newsletter doc = nl::CreateBlank();
    doc.articles = {{L"공지사항", L"첫 기사", L"본문"}, {L"기타", L"둘째 기사", L"본문"}};
    nl::Picture logo;
    logo.kind = nl::Picture::Logo;
    logo.data = FakePng();
    logo.width = 120;
    logo.height = 60;
    nl::Picture photo = logo;
    photo.kind = nl::Picture::Photo;
    photo.x = 500;  // 오른쪽에 놓인 작은 사진 -> 오른쪽으로 띄움
    photo.width = 200;
    photo.anchor = 1;
    doc.pictures = {logo, photo};
    std::string html = nl::ExportHtml(doc);
    std::string b64 = nl::Base64Encode(logo.data);

    size_t logoAt = html.find("class=\"logo\"");
    size_t photoAt = html.find("class=\"photo\"");
    size_t secondArticle = html.find(nl::ToUtf8(L"둘째 기사"));
    CHECK(logoAt != std::string::npos && logoAt < html.find("<h1>"));
    CHECK(photoAt != std::string::npos && photoAt > secondArticle);
    CHECK(Contains(html, "src=\"data:image/png;base64," + b64 + "\""));
    CHECK(Contains(html, "float:right"));
    CHECK(Contains(html, "width:29%"));  // 200 / 688
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
    TestBase64();
    TestMimeType();
    TestJpegOrientation();
    TestPictures();
    TestHtmlPictures();
    TestFonts();
    TestFontSizes();
    TestBooksAndPageBreaks();
    TestBackgrounds();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
