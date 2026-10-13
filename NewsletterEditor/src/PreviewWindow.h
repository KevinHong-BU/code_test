// 소식지를 A4 쪽 모양으로 보여 주는 실시간 미리보기 컨트롤.
#pragma once

#include <windows.h>

#include <string>

#include "Newsletter.h"
#include "Renderer.h"

class PreviewWindow {
public:
    static bool Register(HINSTANCE instance);

    HWND Create(HWND parent, int id, HINSTANCE instance);
    HWND hwnd() const { return hwnd_; }

    // doc은 이 창보다 오래 살아 있어야 합니다.
    void SetDocument(const nl::Newsletter* doc);
    // 문서 내용이 바뀌었을 때 다시 배치하고 그립니다.
    void Refresh();

    int PageCount() const { return renderer_.PageCount(); }

    void ZoomIn();
    void ZoomOut();
    void ZoomFit();
    int ZoomPercent() const;  // 현재 표시 배율(%)

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    double Scale() const;
    void SetZoom(double zoom);
    void UpdateScrollBars();
    void ScrollTo(int x, int y);
    void OnScroll(int bar, int code);
    void OnPaint();
    void NotifyParent();

    HWND hwnd_ = nullptr;
    const nl::Newsletter* doc_ = nullptr;
    nl::Renderer renderer_;
    double zoom_ = 0.0;  // 0이면 창 너비에 맞춤
    int scrollX_ = 0;
    int scrollY_ = 0;
};

// 미리보기의 쪽 수나 배율이 바뀌면 부모 창에 보내는 메시지.
#define WM_APP_PREVIEW_CHANGED (WM_APP + 1)
