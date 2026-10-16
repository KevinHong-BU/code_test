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

// 쪽 위에 자유롭게 놓는 그림(로고·사진).
// 위치와 크기는 쪽 좌표(논리 단위, 한 쪽 = 800 x 1131)로 저장합니다.
struct Picture {
    enum Kind { Photo, Logo };
    Kind kind = Photo;
    int id = 0;            // 문서 안에서 고유한 번호(그림 캐시 키)
    std::string data;      // 원본 그림 파일 내용(PNG, JPEG, GIF, BMP)
    int page = 0;          // 0부터 시작하는 쪽 번호
    int x = 0, y = 0;      // 쪽 왼쪽 위 기준 위치
    int width = 0, height = 0;
    bool wrapText = true;  // true면 본문 글자가 그림을 피해 배치됨
    int anchor = -1;       // HTML 내보내기용 기준 기사 번호(-1이면 맨 위). 파일에 저장하지 않음.
};

enum class TitleAlign { Left, Center, Right };

// 반 포인트 단위 기본 크기와 허용 범위
constexpr int kDefaultHeadingSize = 32;  // 16pt
constexpr int kDefaultBodySize = 22;     // 11pt
constexpr int kDefaultTitleSize = 54;    // 27pt
constexpr int kMinFontSize = 12, kMaxFontSize = 144;  // 6pt ~ 72pt
constexpr int kMaxTitleOffset = 600;

struct Newsletter {
    std::wstring libraryName;  // 도서관 이름
    std::wstring title;        // 소식지 제목
    std::wstring issue;        // 호수 (예: "제 42호")
    std::wstring date;         // 발행일
    std::wstring publisher;    // 발행처·연락처 (맨 아래에 표시)
    std::wstring greeting;     // 머리말
    int theme = 0;             // 색상 테마 번호
    std::wstring headingFont;  // 제호·기사 제목·분류 꼬리표 글꼴 (비어 있으면 기본 글꼴)
    std::wstring bodyFont;     // 본문·머리말·발행처 등 나머지 글꼴 (비어 있으면 기본 글꼴)
    // 글자 크기는 0.5pt 단위 정수(반 포인트)로 저장합니다. 예: 22 = 11pt
    int headingSize = kDefaultHeadingSize;  // 기사 제목 (분류 꼬리표도 비례)
    int bodySize = kDefaultBodySize;        // 본문 (머리말·발행처·바닥글도 비례)
    int titleSize = kDefaultTitleSize;      // 맨 위 소식지 제목(제호)
    // 제호(도서관명·소식지 제목·호수/발행일 묶음)의 정렬과, 미리보기에서 끌어 옮긴 거리(쪽 좌표 단위)
    TitleAlign titleAlign = TitleAlign::Left;
    int titleOffsetX = 0, titleOffsetY = 0;
    std::vector<Article> articles;
    std::vector<Picture> pictures;  // 뒤에 있을수록 위에 그려짐

    int NextPictureId() const;
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

// "10", "10.5", "10.5pt" 같은 글자 크기를 반 포인트 단위로 읽습니다(0.5pt로 반올림).
// 숫자가 아니거나 허용 범위(6~72pt) 밖이면 false.
bool ParseFontSize(const std::wstring& text, int* halfPoints);
// 반 포인트 값을 "11", "10.5" 같은 글자로 만듭니다.
std::wstring FormatFontSize(int halfPoints);

// 글꼴을 고르지 않았을 때 쓰는 기본 글꼴(Windows 기본 한글 글꼴).
extern const wchar_t kDefaultFont[];
// 비어 있으면 기본 글꼴 이름을 돌려줍니다.
std::wstring FontOrDefault(const std::wstring& face);

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

std::string Base64Encode(const std::string& bytes);
bool Base64Decode(const std::string& text, std::string& out);
// 그림 파일 형식의 MIME 형식("image/png" 등). 알 수 없으면 빈 문자열.
std::string ImageMimeType(const std::string& data);
// JPEG의 EXIF 방향 값(1~8). 정보가 없거나 JPEG가 아니면 1(회전 없음).
int JpegOrientation(const std::string& data);

std::string ToUtf8(const std::wstring& s);
std::wstring FromUtf8(const std::string& s);

// .lnews 파일 내용(UTF-8, BOM 포함)으로 변환합니다.
std::string Serialize(const Newsletter& doc);
// 실패하면 false를 돌려주고 error에 이유를 적습니다.
bool Deserialize(const std::string& data, Newsletter& out, std::wstring& error);

}  // namespace nl
