#pragma once

#include <functional>
#include <map>
#include <string>

struct HttpRequest {
    std::string method;
    std::string path;                                // 쿼리 제외
    std::map<std::string, std::string> query;       // URL 디코딩된 UTF-8
    std::map<std::string, std::string> headers;     // 키는 소문자
    std::string body;

    std::string Header(const std::string& name) const;
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "text/plain; charset=utf-8";
    std::string body;

    void Json(const std::string& json, int code = 200);
};

class HttpServer {
public:
    using Handler = std::function<void(const HttpRequest&, HttpResponse&)>;

    explicit HttpServer(Handler handler) : handler_(std::move(handler)) {}
    ~HttpServer();

    // 127.0.0.1 에만 바인딩 (외부 접근 차단)
    bool Listen(int port, std::string& error);
    void Serve();   // 블로킹. 연결마다 스레드 하나로 처리한다.

private:
    void HandleConnection(unsigned long long sock);

    Handler handler_;
    unsigned long long listenSocket_ = 0;
    bool listening_ = false;
    bool wsaStarted_ = false;
};
