#pragma once

#include <string>

#include "Newsletter.h"

namespace nl {

std::wstring HtmlEscape(const std::wstring& s);

// 소식지를 하나의 독립된 HTML 문서(UTF-8)로 만듭니다.
// 이메일 본문이나 홈페이지 게시용으로 쓸 수 있도록 CSS를 문서 안에 포함합니다.
std::string ExportHtml(const Newsletter& doc);

}  // namespace nl
