// 편집기 주 창: 왼쪽은 입력 양식, 오른쪽은 실시간 미리보기.
#pragma once

#include <windows.h>

#include <string>

#include "ImageLibrary.h"
#include "Newsletter.h"
#include "PreviewWindow.h"

class MainWindow {
public:
    explicit MainWindow(HINSTANCE instance);
    ~MainWindow();

    bool Create(int showCommand);
    HWND hwnd() const { return hwnd_; }
    // 메시지 루프에서 호출합니다. 처리했으면 true(TranslateMessage/DispatchMessage 생략).
    bool PreTranslateMessage(MSG* msg);

    void NewDocument(bool sample);
    bool OpenPath(const std::wstring& path);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // 화면 구성
    void CreateControls();
    void CreateMenuBar();
    void LayoutControls();
    HWND AddLabel(const wchar_t* text, DWORD extraStyle = 0);
    HWND AddEdit(int id, bool multiline);
    HWND AddButton(int id, const wchar_t* text);
    int S(int value) const { return MulDiv(value, dpi_, 96); }

    // 문서 <-> 화면 동기화
    void LoadDocumentToUi();
    void SelectArticle(int index);
    void RefreshArticleItem(int index);
    void UpdateArticleButtons();
    void OnCommand(int id, int code, HWND control);
    void OnInfoChanged(int id);
    void OnArticleChanged(int id, int code);

    // 기사 편집
    void AddArticle(bool duplicate);
    void DeleteArticle();
    void MoveArticle(int delta);

    // 그림
    bool LoadPictureFile(const wchar_t* title, nl::Picture* picture);
    void InsertLogo();
    void InsertPhoto();
    void ReplacePicture();
    void UpdatePictureCommands();

    // 파일
    bool ConfirmDiscard();
    bool Save();
    bool SaveAs();
    bool WriteDocument(const std::wstring& path);
    void Open();
    void ExportHtml();
    void Print();

    void MarkDirty();
    void SetClean();
    void UpdateTitle();
    void UpdateStatus();
    std::wstring SuggestedFileName() const;

    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    HACCEL accel_ = nullptr;
    HFONT uiFont_ = nullptr;
    int dpi_ = 96;

    nl::Newsletter doc_;
    std::wstring path_;
    bool dirty_ = false;
    bool loading_ = false;  // 프로그램이 컨트롤 값을 채우는 중에는 변경 알림을 무시
    int current_ = -1;      // 선택된 기사 번호

    ImageLibrary images_;  // GdiplusStartup 이후에 만들어져야 함(main.cpp)
    PreviewWindow preview_;

    HWND infoGroup_ = nullptr, articleGroup_ = nullptr;
    HWND libraryLabel_ = nullptr, library_ = nullptr;
    HWND issueLabel_ = nullptr, issue_ = nullptr;
    HWND titleLabel_ = nullptr, title_ = nullptr;
    HWND dateLabel_ = nullptr, date_ = nullptr, today_ = nullptr;
    HWND themeLabel_ = nullptr, theme_ = nullptr;
    HWND publisherLabel_ = nullptr, publisher_ = nullptr;
    HWND greetingLabel_ = nullptr, greeting_ = nullptr;
    HWND list_ = nullptr;
    HWND addButton_ = nullptr, duplicateButton_ = nullptr, deleteButton_ = nullptr;
    HWND upButton_ = nullptr, downButton_ = nullptr;
    HWND categoryLabel_ = nullptr, category_ = nullptr;
    HWND articleTitleLabel_ = nullptr, articleTitle_ = nullptr;
    HWND bodyLabel_ = nullptr, body_ = nullptr, bodyHint_ = nullptr;
    HWND logoButton_ = nullptr, photoButton_ = nullptr, deletePictureButton_ = nullptr, pictureHint_ = nullptr;
    HWND status_ = nullptr;
};
