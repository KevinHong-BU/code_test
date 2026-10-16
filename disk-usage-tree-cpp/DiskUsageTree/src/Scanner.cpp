#include "Scanner.h"

#include "Util.h"

#include <windows.h>

#include <algorithm>
#include <utility>

using Clock = std::chrono::steady_clock;

std::wstring Node::Path() const
{
    std::vector<const Node*> chain;
    for (const Node* n = this; n; n = n->parent) chain.push_back(n);
    std::wstring path = chain.back()->name;
    for (auto it = chain.rbegin() + 1; it != chain.rend(); ++it) path = JoinPath(path, (*it)->name);
    return path;
}

Scanner::~Scanner()
{
    cancel_ = true;
    if (thread_.joinable()) thread_.join();
}

bool Scanner::Start(const std::wstring& input, std::wstring& error)
{
    std::lock_guard<std::mutex> lock(mu_);
    if (running_) {
        error = L"이미 스캔이 진행 중입니다.";
        return false;
    }
    std::wstring path = NormalizePath(input);
    DWORD attr = path.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(LongPath(path).c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        error = L"디렉토리가 아닙니다: " + input;
        return false;
    }
    // 이전 스캔 스레드는 running_ 이 false 가 된 뒤라 곧바로 종료된다.
    if (thread_.joinable()) thread_.join();

    root_.reset();
    rootPath_ = path;
    currentPath_ = path;
    error_.clear();
    files_ = dirs_ = bytes_ = 0;
    cancel_ = false;
    running_ = true;
    started_ = true;
    startTime_ = Clock::now();
    thread_ = std::thread(&Scanner::Run, this, path);
    return true;
}

void Scanner::Run(std::wstring rootPath)
{
    auto root = std::make_shared<Node>();
    root->name = rootPath;

    std::vector<Node*> order;                               // 방문 순서 (역순 = 후위순회)
    std::vector<std::pair<Node*, std::wstring>> stack;      // 반복 DFS (재귀 깊이 제한 없음)
    stack.emplace_back(root.get(), rootPath);
    std::wstring error;

    WIN32_FIND_DATAW fd;
    while (!stack.empty()) {
        if (cancel_) {
            error = L"사용자가 스캔을 취소했습니다.";
            break;
        }
        Node* node = stack.back().first;
        std::wstring dir = std::move(stack.back().second);
        stack.pop_back();
        order.push_back(node);
        {
            std::lock_guard<std::mutex> lock(mu_);
            currentPath_ = dir;
        }

        HANDLE h = FindFirstFileExW(JoinPath(LongPath(dir), L"*").c_str(), FindExInfoBasic, &fd,
                                    FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND) node->errors++;
            continue;
        }
        do {
            const wchar_t* n = fd.cFileName;
            if (n[0] == L'.' && (n[1] == 0 || (n[1] == L'.' && n[2] == 0))) continue;

            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                // 정션/심볼릭 링크는 따라가지 않는다 (중복 계산·무한루프 방지)
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
                auto child = std::make_unique<Node>();
                child->name = n;
                child->parent = node;
                stack.emplace_back(child.get(), JoinPath(dir, n));
                node->children.push_back(std::move(child));
                dirs_++;
            } else {
                uint64_t size = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
                node->fileSize += size;
                node->fileCount++;
                files_++;
                bytes_ += size;
            }
        } while (FindNextFileW(h, &fd));
        if (GetLastError() != ERROR_NO_MORE_FILES) node->errors++;
        FindClose(h);
    }

    if (error.empty()) {
        // 하위에서 위로 크기 합산
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            Node* node = *it;
            node->size += node->fileSize;
            node->totalFiles += node->fileCount;
            if (Node* p = node->parent) {
                p->size += node->size;
                p->totalFiles += node->totalFiles;
                p->totalDirs += node->totalDirs + 1;
                p->errors += node->errors;
            }
            std::sort(node->children.begin(), node->children.end(),
                      [](const std::unique_ptr<Node>& a, const std::unique_ptr<Node>& b) { return a->size > b->size; });
        }
    } else {
        root.reset();
    }

    std::lock_guard<std::mutex> lock(mu_);
    root_ = root;
    error_ = error;
    endTime_ = Clock::now();
    running_ = false;
}

ScanStatus Scanner::Status()
{
    std::lock_guard<std::mutex> lock(mu_);
    ScanStatus s;
    s.running = running_;
    s.ready = root_ != nullptr;
    s.rootPath = rootPath_;
    s.currentPath = running_ ? currentPath_ : L"";
    s.error = error_;
    s.scannedFiles = files_;
    s.scannedDirs = dirs_;
    s.scannedBytes = bytes_;
    if (started_) {
        auto end = running_ ? Clock::now() : endTime_;
        s.elapsed = std::chrono::duration<double>(end - startTime_).count();
    }
    return s;
}

std::shared_ptr<const Node> Scanner::Root()
{
    std::lock_guard<std::mutex> lock(mu_);
    return root_;
}

const Node* Scanner::Find(const Node* root, const std::wstring& input)
{
    std::wstring path = NormalizePath(input);
    const std::wstring& rootName = root->name;
    if (path.size() < rootName.size() || !EqualsIgnoreCase(path.substr(0, rootName.size()), rootName)) return nullptr;

    std::wstring rest = path.substr(rootName.size());
    // "C:\Users" 루트에 "C:\UsersX" 가 매칭되지 않도록 경계 확인
    if (!rest.empty() && rest[0] != L'\\' && rootName.back() != L'\\') return nullptr;

    const Node* cur = root;
    size_t i = 0;
    while (i < rest.size()) {
        while (i < rest.size() && rest[i] == L'\\') i++;
        if (i >= rest.size()) break;
        size_t j = rest.find(L'\\', i);
        if (j == std::wstring::npos) j = rest.size();
        std::wstring part = rest.substr(i, j - i);

        const Node* next = nullptr;
        for (const auto& c : cur->children) {
            if (EqualsIgnoreCase(c->name, part)) {
                next = c.get();
                break;
            }
        }
        if (!next) return nullptr;
        cur = next;
        i = j;
    }
    return cur;
}
