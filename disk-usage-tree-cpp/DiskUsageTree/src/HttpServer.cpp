#include "HttpServer.h"

#include "Util.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <thread>

#pragma comment(lib, "ws2_32.lib")

namespace {

constexpr size_t kMaxHeaderBytes = 64 * 1024;
constexpr size_t kMaxBodyBytes = 1024 * 1024;

std::string ToLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string Trim(const std::string& s)
{
    size_t b = s.find_first_not_of(" \t");
    size_t e = s.find_last_not_of(" \t\r");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

const char* StatusText(int code)
{
    switch (code) {
    case 200: return "OK";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    default:  return "Internal Server Error";
    }
}

bool SendAll(SOCKET s, const std::string& data)
{
    size_t sent = 0;
    while (sent < data.size()) {
        int n = send(s, data.data() + sent, static_cast<int>(std::min<size_t>(data.size() - sent, 1 << 20)), 0);
        if (n == SOCKET_ERROR) return false;
        sent += n;
    }
    return true;
}

void SendResponse(SOCKET s, const HttpResponse& res)
{
    std::string head = "HTTP/1.1 " + std::to_string(res.status) + " " + StatusText(res.status) + "\r\n";
    head += "Content-Type: " + res.contentType + "\r\n";
    head += "Content-Length: " + std::to_string(res.body.size()) + "\r\n";
    head += "Cache-Control: no-store\r\n";
    head += "X-Content-Type-Options: nosniff\r\n";
    head += "Connection: close\r\n\r\n";
    if (SendAll(s, head)) SendAll(s, res.body);
}

}  // namespace

std::string HttpRequest::Header(const std::string& name) const
{
    auto it = headers.find(ToLower(name));
    return it == headers.end() ? std::string() : it->second;
}

void HttpResponse::Json(const std::string& json, int code)
{
    status = code;
    contentType = "application/json; charset=utf-8";
    body = json;
}

HttpServer::~HttpServer()
{
    if (listening_) closesocket(static_cast<SOCKET>(listenSocket_));
    if (wsaStarted_) WSACleanup();
}

bool HttpServer::Listen(int port, std::string& error)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        error = "WSAStartup 실패";
        return false;
    }
    wsaStarted_ = true;

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        error = "소켓 생성 실패";
        return false;
    }
    // 같은 포트를 다른 프로세스가 가로채지 못하도록 배타적 바인딩
    BOOL on = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&on), sizeof(on));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR ||
        listen(s, SOMAXCONN) == SOCKET_ERROR) {
        error = "포트 " + std::to_string(port) + " 에 바인딩할 수 없습니다 (이미 사용 중일 수 있음).";
        closesocket(s);
        return false;
    }
    listenSocket_ = s;
    listening_ = true;
    return true;
}

void HttpServer::Serve()
{
    for (;;) {
        SOCKET client = accept(static_cast<SOCKET>(listenSocket_), nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            if (WSAGetLastError() == WSAEINTR) continue;
            break;
        }
        std::thread([this, client] { HandleConnection(client); }).detach();
    }
}

void HttpServer::HandleConnection(unsigned long long sockValue)
{
    SOCKET s = static_cast<SOCKET>(sockValue);
    DWORD timeout = 10000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    std::string data;
    char buf[8192];
    size_t headerEnd = std::string::npos;
    while (headerEnd == std::string::npos) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) {
            closesocket(s);
            return;
        }
        data.append(buf, n);
        headerEnd = data.find("\r\n\r\n");
        if (headerEnd == std::string::npos && data.size() > kMaxHeaderBytes) {
            closesocket(s);
            return;
        }
    }

    HttpRequest req;
    HttpResponse res;

    // 요청 라인: METHOD /path?query HTTP/1.1
    size_t lineEnd = data.find("\r\n");
    std::string requestLine = data.substr(0, lineEnd);
    size_t sp1 = requestLine.find(' ');
    size_t sp2 = requestLine.find(' ', sp1 + 1);
    if (sp1 == std::string::npos || sp2 == std::string::npos) {
        res.status = 400;
        SendResponse(s, res);
        closesocket(s);
        return;
    }
    req.method = requestLine.substr(0, sp1);
    std::string target = requestLine.substr(sp1 + 1, sp2 - sp1 - 1);
    size_t q = target.find('?');
    req.path = target.substr(0, q);
    if (q != std::string::npos) req.query = ParseQuery(target.substr(q + 1));

    // 헤더
    size_t pos = lineEnd + 2;
    while (pos < headerEnd) {
        size_t e = data.find("\r\n", pos);
        std::string line = data.substr(pos, e - pos);
        size_t colon = line.find(':');
        if (colon != std::string::npos) req.headers[ToLower(Trim(line.substr(0, colon)))] = Trim(line.substr(colon + 1));
        pos = e + 2;
    }

    // 본문
    size_t contentLength = 0;
    std::string cl = req.Header("content-length");
    if (!cl.empty()) contentLength = static_cast<size_t>(std::strtoull(cl.c_str(), nullptr, 10));
    if (contentLength > kMaxBodyBytes) {
        res.status = 413;
        SendResponse(s, res);
        closesocket(s);
        return;
    }
    req.body = data.substr(headerEnd + 4);
    while (req.body.size() < contentLength) {
        int n = recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;
        req.body.append(buf, n);
    }
    req.body.resize(std::min(req.body.size(), contentLength));

    try {
        handler_(req, res);
    } catch (...) {
        res = HttpResponse();
        res.status = 500;
    }
    SendResponse(s, res);
    shutdown(s, SD_SEND);
    closesocket(s);
}
