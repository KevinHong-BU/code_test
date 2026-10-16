// Disk Usage Tree - 로컬 웹 기반 디스크 사용량 트리 뷰어 (C++ / Win32)
//
// 드라이브 루트(예: C:\)부터 모든 하위 디렉토리가 차지하는 용량을 계산하여
// 브라우저에서 트리 형태로 보여줍니다. 웹 UI(web/index.html)는 리소스로 실행 파일에 포함됩니다.
//
// 사용법:
//   DiskUsageTree.exe                 http://127.0.0.1:8765 에서 실행 후 브라우저 자동 열기
//   DiskUsageTree.exe --port 9000     포트 변경
//   DiskUsageTree.exe --no-browser    브라우저 자동 열기 끄기

#include "HttpServer.h"
#include "Scanner.h"
#include "Util.h"
#include "resource.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>

#pragma comment(lib, "shell32.lib")

namespace {

Scanner g_scanner;

std::string Num(uint64_t v) { return std::to_string(v); }

std::string Num(double v, int decimals = 4)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

std::string Str(const std::wstring& w) { return JsonEscape(ToUtf8(w)); }

std::string LoadIndexHtml()
{
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_INDEX_HTML), MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    if (!res) return "<h1>index.html resource not found</h1>";
    HGLOBAL mem = LoadResource(nullptr, res);
    DWORD size = SizeofResource(nullptr, res);
    const char* data = static_cast<const char*>(LockResource(mem));
    return std::string(data, size);
}

// ---- API 핸들러 -----------------------------------------------------------

std::string RootsJson()
{
    std::vector<wchar_t> buf(512);
    DWORD n = GetLogicalDriveStringsW(static_cast<DWORD>(buf.size()), buf.data());
    std::string items;
    std::wstring first;
    for (const wchar_t* d = buf.data(); n && *d; d += wcslen(d) + 1) {
        std::wstring drive = d;
        if (first.empty()) first = drive;
        ULARGE_INTEGER freeAvail, total, totalFree;
        // 미디어가 없는 드라이브(빈 DVD 등)에서 오류 대화상자가 뜨지 않도록 한다
        UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS);
        BOOL ok = GetDiskFreeSpaceExW(drive.c_str(), &freeAvail, &total, &totalFree);
        SetErrorMode(oldMode);
        if (!items.empty()) items += ",";
        items += "{\"path\":" + Str(drive);
        if (ok) {
            items += ",\"total\":" + Num(total.QuadPart) + ",\"used\":" + Num(total.QuadPart - totalFree.QuadPart) +
                     ",\"free\":" + Num(totalFree.QuadPart);
        } else {
            items += ",\"total\":null,\"used\":null,\"free\":null";
        }
        items += "}";
    }
    // 기본값은 C:\ (없으면 첫 번째 드라이브)
    std::wstring def = first;
    if (GetDriveTypeW(L"C:\\") == DRIVE_FIXED) def = L"C:\\";
    return "{\"roots\":[" + items + "],\"default\":" + Str(def) + "}";
}

std::string StatusJson()
{
    ScanStatus s = g_scanner.Status();
    return std::string("{") +
           "\"running\":" + (s.running ? "true" : "false") +
           ",\"rootPath\":" + (s.rootPath.empty() ? "null" : Str(s.rootPath)) +
           ",\"ready\":" + (s.ready ? "true" : "false") +
           ",\"scannedFiles\":" + Num(s.scannedFiles) +
           ",\"scannedDirs\":" + Num(s.scannedDirs) +
           ",\"scannedBytes\":" + Num(s.scannedBytes) +
           ",\"currentPath\":" + Str(s.currentPath) +
           ",\"elapsed\":" + Num(s.elapsed, 1) +
           ",\"error\":" + (s.error.empty() ? "null" : Str(s.error)) + "}";
}

std::string DescribeNode(const Node* node, uint64_t parentSize)
{
    double percent = parentSize ? static_cast<double>(node->size) / static_cast<double>(parentSize) * 100.0 : 0.0;
    bool hasChildren = !node->children.empty() || node->fileCount > 0;
    return "{\"name\":" + Str(node->name) +
           ",\"path\":" + Str(node->Path()) +
           ",\"size\":" + Num(node->size) +
           ",\"files\":" + Num(node->totalFiles) +
           ",\"dirs\":" + Num(node->totalDirs) +
           ",\"errors\":" + Num(node->errors) +
           ",\"hasChildren\":" + (hasChildren ? "true" : "false") +
           ",\"percent\":" + Num(percent) + "}";
}

void HandleChildren(const HttpRequest& req, HttpResponse& res)
{
    auto root = g_scanner.Root();   // 응답을 만드는 동안 트리가 해제되지 않도록 보관
    const Node* node = nullptr;
    if (root) {
        auto it = req.query.find("path");
        node = it == req.query.end() ? root.get() : Scanner::Find(root.get(), FromUtf8(it->second));
    }
    if (!node) {
        res.Json("{\"error\":" + JsonEscape(ToUtf8(L"스캔 결과가 없거나 경로를 찾을 수 없습니다.")) + "}", 404);
        return;
    }

    size_t limit = 200;
    auto lim = req.query.find("limit");
    if (lim != req.query.end()) limit = std::max<size_t>(1, static_cast<size_t>(std::strtoull(lim->second.c_str(), nullptr, 10)));

    std::string children;
    uint64_t hiddenSize = 0;
    size_t hiddenCount = 0;
    for (size_t i = 0; i < node->children.size(); ++i) {
        const Node* c = node->children[i].get();
        if (i < limit) {
            if (!children.empty()) children += ",";
            children += DescribeNode(c, node->size);
        } else {
            hiddenCount++;
            hiddenSize += c->size;
        }
    }
    uint64_t parentSize = node->parent ? node->parent->size : node->size;
    res.Json("{\"node\":" + DescribeNode(node, parentSize) +
             ",\"children\":[" + children + "]" +
             ",\"hiddenCount\":" + Num(static_cast<uint64_t>(hiddenCount)) +
             ",\"hiddenSize\":" + Num(hiddenSize) +
             ",\"filesHere\":{\"size\":" + Num(node->fileSize) + ",\"count\":" + Num(node->fileCount) + "}}");
}

void HandleRequest(const HttpRequest& req, HttpResponse& res)
{
    if (req.method == "GET") {
        if (req.path == "/") {
            res.contentType = "text/html; charset=utf-8";
            res.body = LoadIndexHtml();
        } else if (req.path == "/api/roots") {
            res.Json(RootsJson());
        } else if (req.path == "/api/status") {
            res.Json(StatusJson());
        } else if (req.path == "/api/children") {
            HandleChildren(req, res);
        } else {
            res.status = 404;
            res.body = "Not Found";
        }
        return;
    }

    if (req.method != "POST") {
        res.status = 405;
        return;
    }

    // 다른 사이트에서의 요청(CSRF)을 막기 위해 Origin 확인
    std::string origin = req.Header("origin");
    if (!origin.empty()) {
        size_t p = origin.find("://");
        std::string originHost = p == std::string::npos ? origin : origin.substr(p + 3);
        if (originHost != req.Header("host")) {
            res.Json("{\"error\":\"forbidden\"}", 403);
            return;
        }
    }

    if (req.path == "/api/scan") {
        std::wstring error;
        bool ok = g_scanner.Start(FromUtf8(JsonGetString(req.body, "path")), error);
        res.Json(std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"error\":" +
                     (ok ? "null" : Str(error)) + "}",
                 ok ? 200 : 400);
    } else if (req.path == "/api/cancel") {
        g_scanner.Cancel();
        res.Json("{\"ok\":true}");
    } else if (req.path == "/api/open") {
        // 탐색기에서 폴더 열기
        std::wstring path = FromUtf8(JsonGetString(req.body, "path"));
        DWORD attr = path.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(path.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            ShellExecuteW(nullptr, L"explore", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            res.Json("{\"ok\":true}");
        } else {
            res.Json("{\"ok\":false,\"error\":" + Str(L"열 수 없습니다.") + "}", 400);
        }
    } else {
        res.status = 404;
        res.body = "Not Found";
    }
}

void PrintUsage()
{
    std::printf("Usage: DiskUsageTree.exe [--port N] [--no-browser]\n");
}

}  // namespace

int wmain(int argc, wchar_t* argv[])
{
    SetConsoleOutputCP(CP_UTF8);

    int port = 8765;
    bool openBrowser = true;
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        if (arg == L"--port" && i + 1 < argc) {
            port = _wtoi(argv[++i]);
        } else if (arg == L"--no-browser") {
            openBrowser = false;
        } else if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
            PrintUsage();
            return 0;
        } else {
            PrintUsage();
            return 1;
        }
    }
    if (port <= 0 || port > 65535) {
        std::printf("잘못된 포트입니다.\n");
        return 1;
    }

    HttpServer server(HandleRequest);
    std::string error;
    if (!server.Listen(port, error)) {
        std::printf("오류: %s\n", error.c_str());
        std::printf("아무 키나 누르면 종료합니다...\n");
        std::getchar();
        return 1;
    }

    std::wstring url = L"http://127.0.0.1:" + std::to_wstring(port) + L"/";
    std::printf("Disk Usage Tree 실행 중: %s  (종료: Ctrl+C 또는 창 닫기)\n", ToUtf8(url).c_str());
    std::printf("관리자 권한으로 실행하면 더 많은 폴더를 읽을 수 있습니다.\n");
    if (openBrowser) ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);

    server.Serve();
    return 0;
}
