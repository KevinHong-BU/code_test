#include "Newsletter.h"

#include <algorithm>
#include <ctime>
#include <cwctype>

namespace nl {

namespace {

const Theme kThemes[] = {
    {L"숲 (초록)", {31, 111, 80}, {242, 169, 59}, {232, 244, 237}},
    {L"바다 (파랑)", {25, 90, 160}, {255, 183, 77}, {230, 240, 251}},
    {L"노을 (주황)", {191, 84, 36}, {66, 133, 120}, {252, 239, 230}},
    {L"단풍 (자주)", {128, 40, 80}, {230, 160, 60}, {248, 234, 240}},
    {L"흑백 (인쇄용)", {45, 45, 45}, {150, 150, 150}, {240, 240, 240}},
};
const int kMonoTheme = 4;

struct CategoryInfo {
    const wchar_t* name;
    Color color;
};

const CategoryInfo kCategories[] = {
    {L"공지사항", {198, 40, 40}},
    {L"신착 도서", {21, 101, 192}},
    {L"추천 도서", {46, 125, 50}},
    {L"행사·프로그램", {230, 81, 0}},
    {L"이용 안내", {0, 121, 107}},
    {L"기타", {97, 97, 97}},
};

const char kBom[] = "\xEF\xBB\xBF";

std::wstring Escape(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t c : s) {
        switch (c) {
            case L'\\': out += L"\\\\"; break;
            case L'\n': out += L"\\n"; break;
            case L'\t': out += L"\\t"; break;
            case L'\r': break;
            default: out += c;
        }
    }
    return out;
}

std::wstring Unescape(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            wchar_t n = s[++i];
            if (n == L'n') out += L'\n';
            else if (n == L't') out += L'\t';
            else out += n;  // "\\" 및 알 수 없는 이스케이프는 문자 그대로
        } else {
            out += s[i];
        }
    }
    return out;
}

void AppendCodePoint(std::string& out, unsigned long cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

void AppendWide(std::wstring& out, unsigned long cp) {
    if (sizeof(wchar_t) == 2 && cp >= 0x10000) {
        cp -= 0x10000;
        out += static_cast<wchar_t>(0xD800 + (cp >> 10));
        out += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
    } else {
        out += static_cast<wchar_t>(cp);
    }
}

int ToInt(const std::wstring& value, int fallback) {
    try {
        return std::stoi(value);
    } catch (...) {
        return fallback;
    }
}

const char kBase64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

bool StartsWith(const std::wstring& s, const wchar_t* prefix) {
    return s.compare(0, std::wstring(prefix).size(), prefix) == 0;
}

}  // namespace

const wchar_t kDefaultFont[] = L"맑은 고딕";
const wchar_t kPageBreakMarker[] = L"[쪽 나누기]";

std::wstring FontOrDefault(const std::wstring& face) {
    std::wstring trimmed = Trim(face);
    return trimmed.empty() ? kDefaultFont : trimmed;
}

bool ParseFontSize(const std::wstring& text, int* halfPoints) {
    std::wstring t = Trim(text);
    if (t.size() >= 2 && (t.compare(t.size() - 2, 2, L"pt") == 0 || t.compare(t.size() - 2, 2, L"PT") == 0))
        t = Trim(t.substr(0, t.size() - 2));
    if (t.empty()) return false;
    // 숫자와 소수점 하나만 허용합니다(로캘에 상관없이 '.'을 소수점으로 봄).
    long whole = 0, frac = 0, fracDigits = 0;
    bool dot = false, digits = false;
    for (wchar_t c : t) {
        if (c == L'.' && !dot) {
            dot = true;
        } else if (c >= L'0' && c <= L'9') {
            digits = true;
            if (!dot) {
                whole = whole * 10 + (c - L'0');
                if (whole > 1000) return false;
            } else if (fracDigits < 3) {
                frac = frac * 10 + (c - L'0');
                ++fracDigits;
            }
        } else {
            return false;
        }
    }
    if (!digits) return false;
    double value = whole;
    if (fracDigits) value += frac / (fracDigits == 1 ? 10.0 : fracDigits == 2 ? 100.0 : 1000.0);
    int half = static_cast<int>(value * 2 + 0.5);
    if (half < kMinFontSize || half > kMaxFontSize) return false;
    *halfPoints = half;
    return true;
}

std::wstring FormatFontSize(int halfPoints) {
    std::wstring s = std::to_wstring(halfPoints / 2);
    if (halfPoints % 2) s += L".5";
    return s;
}

int Newsletter::NextPictureId() const {
    int id = 0;
    for (const Picture& p : pictures) id = std::max(id, p.id);
    for (const Article& a : articles)
        for (const Book& b : a.books) id = std::max(id, b.coverId);
    id = std::max(id, background.id);
    for (const auto& entry : pageBackgrounds) id = std::max(id, entry.second.id);
    return id + 1;
}

const Background* Newsletter::BackgroundFor(int page) const {
    auto it = pageBackgrounds.find(page);
    const Background& bg = it != pageBackgrounds.end() ? it->second : background;
    return bg.data.empty() ? nullptr : &bg;
}

std::string Base64Encode(const std::string& bytes) {
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        unsigned v = (static_cast<unsigned char>(bytes[i]) << 16) | (static_cast<unsigned char>(bytes[i + 1]) << 8) |
                     static_cast<unsigned char>(bytes[i + 2]);
        out += kBase64Chars[(v >> 18) & 63];
        out += kBase64Chars[(v >> 12) & 63];
        out += kBase64Chars[(v >> 6) & 63];
        out += kBase64Chars[v & 63];
    }
    if (i < bytes.size()) {
        unsigned v = static_cast<unsigned char>(bytes[i]) << 16;
        if (i + 1 < bytes.size()) v |= static_cast<unsigned char>(bytes[i + 1]) << 8;
        out += kBase64Chars[(v >> 18) & 63];
        out += kBase64Chars[(v >> 12) & 63];
        out += i + 1 < bytes.size() ? kBase64Chars[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

bool Base64Decode(const std::string& text, std::string& out) {
    out.clear();
    out.reserve(text.size() / 4 * 3);
    unsigned buffer = 0;
    int bits = 0;
    size_t padding = 0;
    for (char c : text) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62;
        else if (c == '/') v = 63;
        else if (c == '=') { ++padding; continue; }
        else if (c == ' ' || c == '\r' || c == '\n' || c == '\t') continue;
        else return false;
        if (padding) return false;  // '=' 뒤에 다른 글자가 올 수 없음
        buffer = (buffer << 6) | static_cast<unsigned>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((buffer >> bits) & 0xFF);
        }
    }
    return padding <= 2;
}

std::string ImageMimeType(const std::string& data) {
    auto starts = [&data](const char* magic, size_t n) { return data.size() >= n && data.compare(0, n, magic, n) == 0; };
    if (starts("\x89PNG\r\n\x1a\n", 8)) return "image/png";
    if (starts("\xFF\xD8\xFF", 3)) return "image/jpeg";
    if (starts("GIF87a", 6) || starts("GIF89a", 6)) return "image/gif";
    if (starts("BM", 2)) return "image/bmp";
    return "";
}

int JpegOrientation(const std::string& data) {
    auto byte = [&data](size_t i) { return static_cast<unsigned>(static_cast<unsigned char>(data[i])); };
    if (data.size() < 4 || byte(0) != 0xFF || byte(1) != 0xD8) return 1;
    // JPEG 구획(segment)을 따라가며 APP1 "Exif" 구획을 찾습니다.
    size_t pos = 2;
    while (pos + 4 <= data.size() && byte(pos) == 0xFF) {
        unsigned marker = byte(pos + 1);
        size_t length = (byte(pos + 2) << 8) | byte(pos + 3);
        if (marker == 0xDA || marker == 0xD9 || length < 2) break;  // 영상 데이터 시작 또는 끝
        size_t start = pos + 4, end = std::min(data.size(), pos + 2 + length);
        if (marker == 0xE1 && end - start >= 14 && data.compare(start, 6, std::string("Exif\0\0", 6)) == 0) {
            size_t tiff = start + 6;
            bool little = data.compare(tiff, 2, "II") == 0;
            auto u16 = [&](size_t i) -> unsigned {
                if (i + 2 > end) return 0;
                return little ? (byte(i) | (byte(i + 1) << 8)) : ((byte(i) << 8) | byte(i + 1));
            };
            auto u32 = [&](size_t i) -> size_t {
                if (i + 4 > end) return 0;
                return little ? (byte(i) | (byte(i + 1) << 8) | (byte(i + 2) << 16) | (static_cast<size_t>(byte(i + 3)) << 24))
                              : ((static_cast<size_t>(byte(i)) << 24) | (byte(i + 1) << 16) | (byte(i + 2) << 8) | byte(i + 3));
            };
            size_t ifd = tiff + u32(tiff + 4);
            unsigned count = u16(ifd);
            for (unsigned k = 0; k < count; ++k) {
                size_t entry = ifd + 2 + k * 12;
                if (entry + 12 > end) break;
                if (u16(entry) == 0x0112) {
                    unsigned value = u16(entry + 8);
                    return value >= 1 && value <= 8 ? static_cast<int>(value) : 1;
                }
            }
            return 1;
        }
        pos = pos + 2 + length;
    }
    return 1;
}

int ThemeCount() { return static_cast<int>(sizeof(kThemes) / sizeof(kThemes[0])); }

const Theme& GetTheme(int index) {
    if (index < 0 || index >= ThemeCount()) index = 0;
    return kThemes[index];
}

const std::vector<std::wstring>& DefaultCategories() {
    static const std::vector<std::wstring> list = [] {
        std::vector<std::wstring> v;
        for (const auto& c : kCategories) v.push_back(c.name);
        return v;
    }();
    return list;
}

Color CategoryColor(const std::wstring& category, int theme) {
    if (theme == kMonoTheme) return {70, 70, 70};
    for (const auto& c : kCategories) {
        if (category == c.name) return c.color;
    }
    return GetTheme(theme).primary;
}

Article CreateArticle(const std::wstring& category) {
    Article a;
    a.category = category;
    a.title = L"새 기사";
    return a;
}

Newsletter CreateBlank() {
    Newsletter doc;
    doc.libraryName = L"OO도서관";
    doc.title = L"도서관 소식지";
    doc.issue = L"제 1호";
    doc.date = TodayString();
    doc.articles.push_back(CreateArticle(DefaultCategories()[0]));
    return doc;
}

Newsletter CreateSample() {
    Newsletter doc;
    doc.libraryName = L"푸른숲 시립도서관";
    doc.title = L"푸른숲 도서관 소식";
    doc.issue = L"제 42호";
    doc.date = TodayString();
    doc.publisher =
        L"발행: 푸른숲 시립도서관 | 주소: OO시 OO구 숲길로 123 | 문의: 02-123-4567\n"
        L"이용 시간: 화~금 09:00~22:00, 토·일 09:00~18:00 (매주 월요일 휴관)";
    doc.greeting =
        L"책 읽기 좋은 계절, 가을이 왔습니다. 이번 호에서는 가을 독서 행사와 새로 들어온 책, "
        L"그리고 사서가 고른 추천 도서를 소개합니다. 도서관에서 여유로운 하루를 보내세요.";
    doc.theme = 0;

    auto article = [&doc](const wchar_t* category, const wchar_t* title, const wchar_t* body) -> Article& {
        Article a;
        a.category = category;
        a.title = title;
        a.body = body;
        doc.articles.push_back(a);
        return doc.articles.back();
    };
    auto book = [](const wchar_t* title, const wchar_t* author, const wchar_t* publisher, const wchar_t* year,
                   const wchar_t* call, const wchar_t* description) {
        Book b;
        b.title = title;
        b.author = author;
        b.publisher = publisher;
        b.year = year;
        b.callNumber = call;
        b.description = description;
        return b;
    };

    article(L"공지사항", L"10월 휴관일 안내",
            L"- 정기 휴관: 매주 월요일\n"
            L"- 임시 휴관: 10월 9일(목) 한글날\n"
            L"\n"
            L"휴관일에도 정문 옆 무인 반납함은 정상 운영합니다.");
    article(L"행사·프로그램", L"가을맞이 작가와의 만남",
            L"그림책 작가를 초대해 창작 이야기를 듣고 함께 그림책을 만들어 봅니다.\n"
            L"\n"
            L"- 일시: 10월 25일(토) 오후 2시\n"
            L"- 장소: 2층 다목적실\n"
            L"- 대상: 초등학생과 보호자 30명 (선착순)\n"
            L"- 신청: 도서관 홈페이지 또는 1층 안내데스크");
    Article& newBooks = article(L"신착 도서", L"이달의 신착 도서", L"9월에 새로 들어온 책 가운데 일부를 소개합니다.");
    newBooks.bookColumns = 2;
    newBooks.books = {
        book(L"바람이 머무는 서가", L"김하늘", L"숲속출판", L"2026", L"813.7-김92ㅂ", L"작은 마을 도서관 사서의 일 년"),
        book(L"처음 만나는 우주 이야기", L"이별빛", L"하늘책방", L"2026", L"440-이44ㅊ", L"어린이를 위한 천문학 입문"),
        book(L"우리 동네 숲 산책", L"박나무", L"초록길", L"2025", L"480.9-박192ㅇ", L"계절별 숲 관찰 안내서"),
        book(L"하루 한 장 글쓰기", L"정연필", L"문장공방", L"2026", L"802-정64ㅎ", L"매일 쓰는 짧은 글 연습"),
    };
    Article& picks = article(L"추천 도서", L"사서가 추천하는 가을 책", L"");
    picks.books = {book(L"바람이 머무는 서가", L"김하늘", L"숲속출판", L"2026", L"813.7-김92ㅂ",
                        L"작은 마을 도서관을 지키는 사서의 일 년을 따뜻하게 그린 소설입니다. 계절마다 바뀌는 "
                        L"서가 풍경과 이용자들의 이야기가 잔잔한 울림을 줍니다. 가을밤, 차 한 잔과 함께 읽기를 권합니다.")};
    article(L"이용 안내", L"모바일 회원증을 이용하세요",
            L"도서관 앱에서 모바일 회원증을 발급받으면 실물 카드 없이도 "
            L"대출과 열람실 이용이 가능합니다.\n"
            L"- 앱 설치 후 로그인 → [내 정보] → [모바일 회원증]");
    return doc;
}

std::wstring TodayString() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return std::to_wstring(tm.tm_year + 1900) + L"년 " + std::to_wstring(tm.tm_mon + 1) + L"월 " +
           std::to_wstring(tm.tm_mday) + L"일";
}

std::wstring Trim(const std::wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::iswspace(s[b])) ++b;
    while (e > b && std::iswspace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::wstring NormalizeNewlines(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\r') {
            out += L'\n';
            if (i + 1 < s.size() && s[i + 1] == L'\n') ++i;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::wstring ToCrLf(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + s.size() / 16);
    for (wchar_t c : NormalizeNewlines(s)) {
        if (c == L'\n') out += L'\r';
        out += c;
    }
    return out;
}

std::vector<BodyLine> ParseBody(const std::wstring& body) {
    std::vector<BodyLine> lines;
    std::wstring text = NormalizeNewlines(body);
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = Trim(text.substr(start, end - start));
        if (line.empty()) {
            lines.push_back({BodyLine::Blank, L""});
        } else if (line == kPageBreakMarker || line == L"[쪽나누기]") {
            lines.push_back({BodyLine::PageBreak, L""});
        } else if (StartsWith(line, L"- ") || StartsWith(line, L"* ") || StartsWith(line, L"• ") ||
                   StartsWith(line, L"· ")) {
            lines.push_back({BodyLine::Bullet, Trim(line.substr(2))});
        } else {
            lines.push_back({BodyLine::Text, line});
        }
        start = end + 1;
    }
    // 앞뒤의 빈 줄은 의미가 없으므로 버립니다.
    while (!lines.empty() && lines.back().kind == BodyLine::Blank) lines.pop_back();
    size_t first = 0;
    while (first < lines.size() && lines[first].kind == BodyLine::Blank) ++first;
    lines.erase(lines.begin(), lines.begin() + first);
    return lines;
}

std::string ToUtf8(const std::wstring& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned long cp = static_cast<unsigned long>(s[i]);
        if (sizeof(wchar_t) == 2 && cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size()) {
            unsigned long lo = static_cast<unsigned long>(s[i + 1]);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
            }
        }
        if ((cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) cp = 0xFFFD;
        AppendCodePoint(out, cp);
    }
    return out;
}

std::wstring FromUtf8(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned long cp;
        int extra;
        if (c < 0x80) { cp = c; extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else { AppendWide(out, 0xFFFD); ++i; continue; }

        bool ok = i + extra < s.size();
        for (int k = 1; ok && k <= extra; ++k) {
            unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) ok = false;
            else cp = (cp << 6) | (cc & 0x3F);
        }
        static const unsigned long kMin[] = {0, 0x80, 0x800, 0x10000};
        if (!ok || cp < kMin[extra] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            AppendWide(out, 0xFFFD);
            ++i;
            continue;
        }
        AppendWide(out, cp);
        i += extra + 1;
    }
    return out;
}

std::string Serialize(const Newsletter& doc) {
    std::wstring w;
    auto put = [&w](const wchar_t* key, const std::wstring& value) {
        w += key;
        w += L'=';
        w += Escape(value);
        w += L'\n';
    };
    w += L"# 도서관 소식지 편집기 문서\n";
    w += L"[newsletter]\n";
    put(L"format", L"2");
    put(L"library", doc.libraryName);
    put(L"title", doc.title);
    put(L"issue", doc.issue);
    put(L"date", doc.date);
    put(L"publisher", doc.publisher);
    put(L"greeting", doc.greeting);
    put(L"theme", std::to_wstring(doc.theme));
    if (!Trim(doc.headingFont).empty()) put(L"headingFont", Trim(doc.headingFont));
    if (!Trim(doc.bodyFont).empty()) put(L"bodyFont", Trim(doc.bodyFont));
    put(L"headingSize", FormatFontSize(doc.headingSize));
    put(L"bodySize", FormatFontSize(doc.bodySize));
    put(L"titleSize", FormatFontSize(doc.titleSize));
    put(L"titleAlign", doc.titleAlign == TitleAlign::Center  ? L"center"
                       : doc.titleAlign == TitleAlign::Right ? L"right"
                                                             : L"left");
    if (doc.titleOffsetX || doc.titleOffsetY) {
        put(L"titleOffsetX", std::to_wstring(doc.titleOffsetX));
        put(L"titleOffsetY", std::to_wstring(doc.titleOffsetY));
    }
    for (const Article& a : doc.articles) {
        w += L"\n[article]\n";
        put(L"category", a.category);
        put(L"title", a.title);
        put(L"body", a.body);
        if (!Trim(a.titleFont).empty()) put(L"titleFont", Trim(a.titleFont));
        if (a.titleSize) put(L"titleSize", FormatFontSize(a.titleSize));
        if (!a.titleBold) put(L"titleBold", L"0");
        if (a.titleAlign != TitleAlign::Left)
            put(L"titleAlign", a.titleAlign == TitleAlign::Center ? L"center" : L"right");
        if (a.pageBreakBefore) put(L"pageBreak", L"1");
        if (a.bookColumns == 2) put(L"bookColumns", L"2");
        for (const Book& b : a.books) {
            w += L"\n[book]\n";
            put(L"title", b.title);
            put(L"author", b.author);
            put(L"publisher", b.publisher);
            put(L"year", b.year);
            put(L"callNumber", b.callNumber);
            put(L"description", b.description);
            if (!b.cover.empty()) {
                put(L"coverWidth", std::to_wstring(b.coverWidth));
                put(L"coverHeight", std::to_wstring(b.coverHeight));
                w += L"cover=";
                w += FromUtf8(Base64Encode(b.cover));
                w += L'\n';
            }
        }
    }
    for (const Picture& p : doc.pictures) {
        w += L"\n[picture]\n";
        put(L"kind", p.kind == Picture::Logo ? L"logo" : L"photo");
        put(L"page", std::to_wstring(p.page));
        put(L"x", std::to_wstring(p.x));
        put(L"y", std::to_wstring(p.y));
        put(L"width", std::to_wstring(p.width));
        put(L"height", std::to_wstring(p.height));
        put(L"wrap", p.wrapText ? L"1" : L"0");
        // 그림 내용은 한 줄짜리 Base64로 저장해 파일 하나에 모두 담습니다.
        w += L"data=";
        w += FromUtf8(Base64Encode(p.data));
        w += L'\n';
    }
    auto putBackground = [&](const std::wstring& page, const Background& bg) {
        w += L"\n[background]\n";
        put(L"page", page);
        if (bg.data.empty()) return;  // 이 쪽은 배경 없음
        const wchar_t* fit = bg.fit == BackgroundFit::Stretch   ? L"stretch"
                             : bg.fit == BackgroundFit::Contain ? L"contain"
                             : bg.fit == BackgroundFit::Tile    ? L"tile"
                                                                : L"cover";
        put(L"fit", fit);
        put(L"opacity", std::to_wstring(bg.opacity));
        put(L"width", std::to_wstring(bg.width));
        put(L"height", std::to_wstring(bg.height));
        w += L"data=";
        w += FromUtf8(Base64Encode(bg.data));
        w += L'\n';
    };
    if (!doc.background.data.empty()) putBackground(L"all", doc.background);
    for (const auto& entry : doc.pageBackgrounds) putBackground(std::to_wstring(entry.first), entry.second);
    return kBom + ToUtf8(w);
}

bool Deserialize(const std::string& data, Newsletter& out, std::wstring& error) {
    std::string bytes = data;
    if (bytes.compare(0, 3, kBom) == 0) bytes.erase(0, 3);
    std::wstring text = NormalizeNewlines(FromUtf8(bytes));

    Newsletter doc;
    enum { None, Header, InArticle, InPicture, InBook, InBackground } section = None;
    Background* currentBackground = nullptr;
    Background ignoredBackground;  // 잘못된 쪽 번호의 배경은 읽고 버림
    bool sawHeader = false;
    size_t start = 0;
    int lineNo = 0;
    while (start < text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) end = text.size();
        std::wstring line = text.substr(start, end - start);
        start = end + 1;
        ++lineNo;

        std::wstring trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == L'#') continue;
        if (trimmed == L"[newsletter]") {
            section = Header;
            sawHeader = true;
            continue;
        }
        if (trimmed == L"[article]") {
            if (!sawHeader) break;
            section = InArticle;
            doc.articles.emplace_back();
            continue;
        }
        if (trimmed == L"[book]") {
            if (!sawHeader || doc.articles.empty()) {
                error = std::to_wstring(lineNo) + L"번째 줄: 도서가 기사 밖에 있습니다.";
                return false;
            }
            section = InBook;
            doc.articles.back().books.emplace_back();
            continue;
        }
        if (trimmed == L"[background]") {
            if (!sawHeader) break;
            section = InBackground;
            currentBackground = nullptr;
            continue;
        }
        if (trimmed == L"[picture]") {
            if (!sawHeader) break;
            section = InPicture;
            doc.pictures.emplace_back();
            doc.pictures.back().id = static_cast<int>(doc.pictures.size());
            continue;
        }
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos || section == None) {
            error = std::to_wstring(lineNo) + L"번째 줄을 해석할 수 없습니다.";
            return false;
        }
        std::wstring key = Trim(line.substr(0, eq));
        std::wstring value = Unescape(line.substr(eq + 1));

        if (section == Header) {
            if (key == L"format") {
                if (Trim(value) != L"1" && Trim(value) != L"2") {
                    error = L"지원하지 않는 파일 버전입니다: " + value;
                    return false;
                }
            } else if (key == L"library") doc.libraryName = value;
            else if (key == L"title") doc.title = value;
            else if (key == L"issue") doc.issue = value;
            else if (key == L"date") doc.date = value;
            else if (key == L"publisher") doc.publisher = value;
            else if (key == L"greeting") doc.greeting = value;
            else if (key == L"headingFont") doc.headingFont = Trim(value);
            else if (key == L"bodyFont") doc.bodyFont = Trim(value);
            else if (key == L"headingSize") ParseFontSize(value, &doc.headingSize);  // 잘못된 값이면 기본값 유지
            else if (key == L"bodySize") ParseFontSize(value, &doc.bodySize);
            else if (key == L"titleSize") ParseFontSize(value, &doc.titleSize);
            else if (key == L"titleAlign") {
                std::wstring a = Trim(value);
                doc.titleAlign = a == L"center" ? TitleAlign::Center : a == L"right" ? TitleAlign::Right : TitleAlign::Left;
            }
            else if (key == L"titleOffsetX") doc.titleOffsetX = std::clamp(ToInt(value, 0), -kMaxTitleOffset, kMaxTitleOffset);
            else if (key == L"titleOffsetY") doc.titleOffsetY = std::clamp(ToInt(value, 0), -kMaxTitleOffset, kMaxTitleOffset);
            else if (key == L"theme") {
                doc.theme = ToInt(value, 0);
                if (doc.theme < 0 || doc.theme >= ThemeCount()) doc.theme = 0;
            }
            // 알 수 없는 키는 이후 버전과의 호환을 위해 무시합니다.
        } else if (section == InPicture) {
            Picture& p = doc.pictures.back();
            if (key == L"kind") p.kind = Trim(value) == L"logo" ? Picture::Logo : Picture::Photo;
            else if (key == L"page") p.page = std::max(0, ToInt(value, 0));
            else if (key == L"x") p.x = ToInt(value, 0);
            else if (key == L"y") p.y = ToInt(value, 0);
            else if (key == L"width") p.width = ToInt(value, 0);
            else if (key == L"height") p.height = ToInt(value, 0);
            else if (key == L"wrap") p.wrapText = Trim(value) != L"0";
            else if (key == L"data" && !Base64Decode(ToUtf8(Trim(value)), p.data)) {
                error = std::to_wstring(lineNo) + L"번째 줄의 그림 데이터가 손상되었습니다.";
                return false;
            }
        } else if (section == InBackground) {
            if (key == L"page") {
                std::wstring v = Trim(value);
                int page = ToInt(v, -1);
                if (v == L"all") {
                    doc.background = Background{};
                    currentBackground = &doc.background;
                } else if (page >= 0 && page < 1000) {
                    currentBackground = &doc.pageBackgrounds[page];
                } else {
                    currentBackground = &ignoredBackground;
                }
                continue;
            }
            Background& bg = currentBackground ? *currentBackground : ignoredBackground;
            if (key == L"fit") {
                std::wstring v = Trim(value);
                bg.fit = v == L"stretch"   ? BackgroundFit::Stretch
                         : v == L"contain" ? BackgroundFit::Contain
                         : v == L"tile"    ? BackgroundFit::Tile
                                           : BackgroundFit::Cover;
            } else if (key == L"opacity") {
                bg.opacity = std::clamp(ToInt(value, 30), kMinOpacity, kMaxOpacity);
            } else if (key == L"width") {
                bg.width = std::max(0, ToInt(value, 0));
            } else if (key == L"height") {
                bg.height = std::max(0, ToInt(value, 0));
            } else if (key == L"data" && !Base64Decode(ToUtf8(Trim(value)), bg.data)) {
                error = std::to_wstring(lineNo) + L"번째 줄의 배경 그림 데이터가 손상되었습니다.";
                return false;
            }
        } else if (section == InBook) {
            Book& b = doc.articles.back().books.back();
            if (key == L"title") b.title = value;
            else if (key == L"author") b.author = value;
            else if (key == L"publisher") b.publisher = value;
            else if (key == L"year") b.year = value;
            else if (key == L"callNumber") b.callNumber = value;
            else if (key == L"description") b.description = value;
            else if (key == L"coverWidth") b.coverWidth = std::max(0, ToInt(value, 0));
            else if (key == L"coverHeight") b.coverHeight = std::max(0, ToInt(value, 0));
            else if (key == L"cover" && !Base64Decode(ToUtf8(Trim(value)), b.cover)) {
                error = std::to_wstring(lineNo) + L"번째 줄의 표지 그림 데이터가 손상되었습니다.";
                return false;
            }
        } else {
            Article& a = doc.articles.back();
            if (key == L"category") a.category = value;
            else if (key == L"title") a.title = value;
            else if (key == L"body") a.body = value;
            else if (key == L"titleFont") a.titleFont = Trim(value);
            else if (key == L"titleSize") ParseFontSize(value, &a.titleSize);
            else if (key == L"titleBold") a.titleBold = Trim(value) != L"0";
            else if (key == L"titleAlign") {
                std::wstring v = Trim(value);
                a.titleAlign = v == L"center" ? TitleAlign::Center : v == L"right" ? TitleAlign::Right : TitleAlign::Left;
            }
            else if (key == L"pageBreak") a.pageBreakBefore = Trim(value) == L"1";
            else if (key == L"bookColumns") a.bookColumns = Trim(value) == L"2" ? 2 : 1;
        }
    }
    if (!sawHeader) {
        error = L"도서관 소식지 파일이 아닙니다.";
        return false;
    }
    // 표지 그림 캐시 번호를 그림 번호와 겹치지 않게 매기고, 크기 정보가 없는 표지는 버립니다.
    int nextId = static_cast<int>(doc.pictures.size()) + 1;
    for (Article& a : doc.articles) {
        for (Book& b : a.books) {
            if (b.cover.empty() || b.coverWidth < 1 || b.coverHeight < 1) b.cover.clear();
            else b.coverId = nextId++;
        }
    }
    // 배경 그림에도 캐시 번호를 매기고, 크기 정보가 없는 그림은 버립니다(쪽별 항목은 '배경 없음'으로 남김).
    auto fixBackground = [&nextId](Background& bg) {
        if (bg.data.empty() || bg.width < 1 || bg.height < 1) bg.data.clear();
        else bg.id = nextId++;
    };
    fixBackground(doc.background);
    for (auto& entry : doc.pageBackgrounds) fixBackground(entry.second);
    // 내용이 없거나 크기가 잘못된 그림은 버립니다.
    doc.pictures.erase(std::remove_if(doc.pictures.begin(), doc.pictures.end(),
                                      [](const Picture& p) { return p.data.empty() || p.width < 1 || p.height < 1; }),
                       doc.pictures.end());
    out = std::move(doc);
    return true;
}

}  // namespace nl
