#include "Util.h"

#include <windows.h>

#include <cstdio>
#include <vector>

std::string ToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring FromUtf8(const std::string& s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::wstring NormalizePath(const std::wstring& path)
{
    if (path.empty()) return {};
    std::wstring input = path;
    for (auto& c : input) {
        if (c == L'/') c = L'\\';
    }
    // "C:" 는 현재 디렉토리가 아니라 드라이브 루트로 취급
    if (input.size() == 2 && input[1] == L':') input += L'\\';

    DWORD n = GetFullPathNameW(input.c_str(), 0, nullptr, nullptr);
    if (n == 0) return {};
    std::vector<wchar_t> buf(n);
    n = GetFullPathNameW(input.c_str(), n, buf.data(), nullptr);
    std::wstring full(buf.data(), n);

    // "C:\" 는 그대로 두고, 그 외에는 끝의 '\' 제거
    while (full.size() > 3 && full.back() == L'\\') full.pop_back();
    return full;
}

std::wstring JoinPath(const std::wstring& base, const std::wstring& name)
{
    if (base.empty()) return name;
    if (base.back() == L'\\' || base.back() == L'/') return base + name;
    return base + L'\\' + name;
}

std::wstring LongPath(const std::wstring& path)
{
    if (path.rfind(L"\\\\?\\", 0) == 0) return path;
    if (path.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + path.substr(2);  // \\server\share
    return L"\\\\?\\" + path;
}

bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b)
{
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
                                b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

std::string JsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
    return out;
}

static void AppendUtf8(std::string& out, unsigned cp)
{
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

static bool ParseHex4(const std::string& s, size_t i, unsigned& v)
{
    if (i + 4 > s.size()) return false;
    v = 0;
    for (size_t k = i; k < i + 4; ++k) {
        char c = s[k];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return false;
    }
    return true;
}

std::string JsonGetString(const std::string& json, const std::string& key)
{
    // 단순한 평면 객체 {"key": "value", ...} 만 지원
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return {};
    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (pos == std::string::npos || json[pos] != '"') return {};

    std::string out;
    for (size_t i = pos + 1; i < json.size(); ++i) {
        char c = json[i];
        if (c == '"') return out;
        if (c != '\\') { out += c; continue; }
        if (++i >= json.size()) break;
        switch (json[i]) {
        case '"':  out += '"'; break;
        case '\\': out += '\\'; break;
        case '/':  out += '/'; break;
        case 'b':  out += '\b'; break;
        case 'f':  out += '\f'; break;
        case 'n':  out += '\n'; break;
        case 'r':  out += '\r'; break;
        case 't':  out += '\t'; break;
        case 'u': {
            unsigned cp;
            if (!ParseHex4(json, i + 1, cp)) return {};
            i += 4;
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 < json.size() && json[i + 1] == '\\' && json[i + 2] == 'u') {
                unsigned lo;
                if (ParseHex4(json, i + 3, lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    i += 6;
                }
            }
            AppendUtf8(out, cp);
            break;
        }
        default: return {};
        }
    }
    return {};
}

static int HexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string UrlDecode(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') {
            out += ' ';
        } else if (s[i] == '%' && i + 2 < s.size() && HexValue(s[i + 1]) >= 0 && HexValue(s[i + 2]) >= 0) {
            out += static_cast<char>(HexValue(s[i + 1]) * 16 + HexValue(s[i + 2]));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::map<std::string, std::string> ParseQuery(const std::string& query)
{
    std::map<std::string, std::string> result;
    size_t start = 0;
    while (start <= query.size()) {
        size_t amp = query.find('&', start);
        if (amp == std::string::npos) amp = query.size();
        std::string part = query.substr(start, amp - start);
        if (!part.empty()) {
            size_t eq = part.find('=');
            if (eq == std::string::npos) result[UrlDecode(part)] = "";
            else result[UrlDecode(part.substr(0, eq))] = UrlDecode(part.substr(eq + 1));
        }
        start = amp + 1;
    }
    return result;
}
