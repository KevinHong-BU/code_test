// 도서관 소식지(뉴스레터) 편집기 - 진입점
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#include "MainWindow.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

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
