#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// 디렉토리 하나. 하위 디렉토리만 노드로 저장하고 파일은 합계만 보관한다.
struct Node {
    std::wstring name;              // 루트 노드는 전체 경로 (예: C:\)
    Node* parent = nullptr;
    std::vector<std::unique_ptr<Node>> children;   // 스캔 완료 후 크기 내림차순 정렬
    uint64_t fileSize = 0;          // 이 디렉토리에 직접 들어있는 파일들의 크기
    uint64_t fileCount = 0;
    uint64_t size = 0;              // 하위 전체 포함 크기
    uint64_t totalFiles = 0;
    uint64_t totalDirs = 0;
    uint64_t errors = 0;            // 접근 거부 등으로 읽지 못한 항목 수

    std::wstring Path() const;
};

struct ScanStatus {
    bool running = false;
    bool ready = false;
    std::wstring rootPath;
    std::wstring currentPath;
    std::wstring error;
    uint64_t scannedFiles = 0;
    uint64_t scannedDirs = 0;
    uint64_t scannedBytes = 0;
    double elapsed = 0;
};

class Scanner {
public:
    ~Scanner();

    bool Start(const std::wstring& path, std::wstring& error);
    void Cancel() { cancel_ = true; }
    ScanStatus Status();

    // 스캔 결과. 완료된 트리는 변경되지 않으므로 반환된 포인터를 잠금 없이 읽어도 된다.
    std::shared_ptr<const Node> Root();
    static const Node* Find(const Node* root, const std::wstring& path);

private:
    void Run(std::wstring rootPath);

    std::mutex mu_;
    std::thread thread_;
    bool running_ = false;
    bool started_ = false;
    std::shared_ptr<const Node> root_;
    std::wstring rootPath_;
    std::wstring currentPath_;
    std::wstring error_;
    std::chrono::steady_clock::time_point startTime_;
    std::chrono::steady_clock::time_point endTime_;

    std::atomic<bool> cancel_{false};
    std::atomic<uint64_t> files_{0};
    std::atomic<uint64_t> dirs_{0};
    std::atomic<uint64_t> bytes_{0};
};
