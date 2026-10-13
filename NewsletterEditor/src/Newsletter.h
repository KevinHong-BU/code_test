// 도서관 소식지(뉴스레터) 데이터 모델과 파일 형식.
// Windows API에 의존하지 않으므로 다른 플랫폼에서도 단위 테스트할 수 있습니다.
#pragma once

#include <string>
#include <vector>

namespace nl {

// 소식지에 실리는 기사 하나.
// body의 줄바꿈은 항상 '\n' 하나로 저장합니다(편집 컨트롤의 "\r\n"은 UI에서 변환).
struct Article {
    std::wstring category;
    std::wstring title;
    std::wstring body;
};

struct Newsletter {
    std::wstring libraryName;  // 도서관 이름
    std::wstring title;        // 소식지 제목
    std::wstring issue;        // 호수 (예: "제 42호")
    std::wstring date;         // 발행일
    std::wstring publisher;    // 발행처·연락처 (맨 아래에 표시)
    std::wstring greeting;     // 머리말
    int theme = 0;             // 색상 테마 번호
    std::vector<Article> articles;
};

struct Color {
    unsigned char r, g, b;
};

struct Theme {
    const wchar_t* name;
    Color primary;  // 제호 띠, 강조색
    Color accent;   // 보조 강조선
    Color light;    // 머리말 상자 배경
};

int ThemeCount();
const Theme& GetTheme(int index);  // 범위를 벗어나면 0번 테마
const std::vector<std::wstring>& DefaultCategories();
Color CategoryColor(const std::wstring& category, int theme);

Newsletter CreateBlank();
Newsletter CreateSample();
Article CreateArticle(const std::wstring& category);

// 오늘 날짜를 "2026년 10월 13일" 형식으로 돌려줍니다.
std::wstring TodayString();

// 본문 한 줄의 종류. "- ", "* ", "• ", "· "로 시작하면 글머리표 항목입니다.
struct BodyLine {
    enum Kind { Text, Bullet, Blank } kind;
    std::wstring text;
};
std::vector<BodyLine> ParseBody(const std::wstring& body);

std::wstring Trim(const std::wstring& s);
std::wstring NormalizeNewlines(const std::wstring& s);  // "\r\n", "\r" -> "\n"
std::wstring ToCrLf(const std::wstring& s);             // "\n" -> "\r\n"

std::string ToUtf8(const std::wstring& s);
std::wstring FromUtf8(const std::string& s);

// .lnews 파일 내용(UTF-8, BOM 포함)으로 변환합니다.
std::string Serialize(const Newsletter& doc);
// 실패하면 false를 돌려주고 error에 이유를 적습니다.
bool Deserialize(const std::string& data, Newsletter& out, std::wstring& error);

}  // namespace nl
