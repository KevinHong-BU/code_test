// 도서관 소식지(뉴스레터) 편집기 - 진입점
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <objidl.h>

#include <algorithm>

// gdiplus.h는 min/max 매크로를 기대하므로 NOMINMAX 환경에서는 std 버전을 쓰게 합니다.
namespace Gdiplus {
using std::max;
using std::min;
}  // namespace Gdiplus
#include <gdiplus.h>

#include "MainWindow.h"

int RunApplication(HINSTANCE instance, int showCommand);

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    // 그림(로고·사진)을 읽고 그리는 데 GDI+를 씁니다. 창(과 그림 캐시)은 종료 전에 먼저 없어져야 합니다.
    Gdiplus::GdiplusStartupInput gdiplusInput;
    ULONG_PTR gdiplusToken = 0;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);
    int exitCode = RunApplication(instance, showCommand);
    Gdiplus::GdiplusShutdown(gdiplusToken);
    return exitCode;
}

int RunApplication(HINSTANCE instance, int showCommand) {
    MainWindow window(instance);
    if (!window.Create(showCommand)) {
        MessageBoxW(nullptr, L"창을 만들 수 없습니다.", L"도서관 소식지 편집기", MB_OK | MB_ICONERROR);
        return 1;
    }

    // 명령줄로 파일을 받으면(탐색기에서 .lnews 파일을 끌어다 놓은 경우 등) 그 파일을 엽니다.
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool opened = argc > 1 && window.OpenPath(argv[1]);
    if (argv) LocalFree(argv);
    if (!opened) window.NewDocument(true);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (window.PreTranslateMessage(&msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
