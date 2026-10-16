#pragma once

#include <map>
#include <string>

// UTF-8 <-> UTF-16 변환
std::string ToUtf8(const std::wstring& w);
std::wstring FromUtf8(const std::string& s);

// 경로 유틸리티
std::wstring NormalizePath(const std::wstring& path);   // 절대 경로화, 끝의 '\' 제거 (드라이브 루트 제외)
std::wstring JoinPath(const std::wstring& base, const std::wstring& name);
std::wstring LongPath(const std::wstring& path);        // \\?\ 접두사 (MAX_PATH 제한 회피)
bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b);

// JSON
std::string JsonEscape(const std::string& utf8);        // 따옴표 포함 JSON 문자열 리터럴 생성
std::string JsonGetString(const std::string& json, const std::string& key);  // {"key": "..."} 에서 값 추출

// URL
std::string UrlDecode(const std::string& s);
std::map<std::string, std::string> ParseQuery(const std::string& query);
