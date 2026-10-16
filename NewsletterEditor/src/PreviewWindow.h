// 소식지를 A4 쪽 모양으로 보여 주는 실시간 미리보기 컨트롤.
// 그림(로고·사진)을 마우스로 고르고, 끌어서 옮기고, 손잡이로 크기를 바꿀 수 있습니다.
#pragma once

#include <windows.h>

#include <string>

#include "Newsletter.h"
#include "Renderer.h"

class ImageLibrary;

class PreviewWindow {
public:
    static bool Register(HINSTANCE instance);

    HWND Create(HWND parent, int id, HINSTANCE instance);
    HWND hwnd() const { return hwnd_; }

    // doc과 images는 이 창보다 오래 살아 있어야 합니다. 미리보기는 doc의 그림 위치·크기를 직접 고칩니다.
    void SetDocument(nl::Newsletter* doc, ImageLibrary* images);
    // 문서 내용이 바뀌었을 때 다시 배치하고 그립니다.
    void Refresh();

    int PageCount() const { return renderer_.PageCount(); }
    const nl::Renderer& renderer() const { return renderer_; }

    void ZoomIn();
    void ZoomOut();
    void ZoomFit();
    int ZoomPercent() const;  // 현재 표시 배율(%)

    // 그림 넣기: 다음에 미리보기에서 끄는 영역에 picture를 넣습니다(그냥 누르면 기본 크기).
    void BeginPlacing(const nl::Picture& picture);
    bool IsPlacing() const { return placing_; }
    // Esc: 넣기 취소, 아니면 선택 해제.
    void CancelInteraction();

    int SelectedPicture() const { return selected_; }
    bool IsTitleSelected() const { return titleSelected_; }
    void SelectPicture(int index);  // 그 그림이 보이도록 스크롤

    // 선택한 그림 편집
    void DeleteSelected();
    void ResetAspectRatio();
    void ToggleWrap();
    void BringToFront();
    void SendToBack();

private:
    enum class Drag { None, Move, Resize, Place, Title };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    double Scale() const;
    int PageWidthPx() const;
    int PageHeightPx() const;
    int PageLeftPx() const;
    int PageTopPx(int page) const;
    int PageAt(int deviceY) const;
    POINT ToDevice(int page, double x, double y) const;
    void ToLogical(POINT pt, int page, double* x, double* y) const;
    RECT PictureDeviceRect(const nl::Picture& p) const;
    int HitPicture(POINT pt) const;
    int HitHandle(POINT pt) const;  // 선택한 그림의 손잡이 번호(0~7) 또는 -1
    RECT TitleDeviceRect() const;
    bool HitTitle(POINT pt) const;
    void MoveTitle(int dx, int dy);  // 제호를 (원래 위치 + dx, dy)로 옮김

    void SetZoom(double zoom);
    void UpdateScrollBars();
    void ScrollTo(int x, int y);
    void OnScroll(int bar, int code);
    void OnPaint();
    void DrawOverlay(HDC hdc);
    void NotifyParent();
    void NotifyEdited();

    void OnMouseDown(POINT pt, WPARAM keys);
    void OnMouseMove(POINT pt, WPARAM keys);
    void OnMouseUp(POINT pt);
    void OnContextMenu(POINT screenPt);
    bool OnSetCursor();
    void Nudge(int dx, int dy);
    void FinishPlacing(POINT pt);

    HWND hwnd_ = nullptr;
    HFONT bannerFont_ = nullptr;
    nl::Newsletter* doc_ = nullptr;
    ImageLibrary* images_ = nullptr;
    nl::Renderer renderer_;
    double zoom_ = 0.0;  // 0이면 창 너비에 맞춤
    int scrollX_ = 0;
    int scrollY_ = 0;

    int selected_ = -1;
    bool titleSelected_ = false;  // 맨 위 제호 묶음이 선택됨
    int titleOrigX_ = 0, titleOrigY_ = 0;
    double titleStartX_ = 0, titleStartY_ = 0;
    bool placing_ = false;
    nl::Picture pending_;  // 넣을 그림(placing_일 때)

    Drag drag_ = Drag::None;
    int handle_ = -1;
    nl::Picture original_;  // 끌기 시작할 때의 그림 상태
    double grabX_ = 0, grabY_ = 0;
    int placePage_ = 0;
    POINT placeStart_{}, placeNow_{};
};

// 미리보기의 쪽 수·배율·선택이 바뀌면 부모 창에 보내는 메시지.
#define WM_APP_PREVIEW_CHANGED (WM_APP + 1)
// 미리보기에서 문서(그림)를 고쳤을 때 부모 창에 보내는 메시지.
#define WM_APP_DOC_EDITED (WM_APP + 2)
