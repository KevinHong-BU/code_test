// 편집기 주 창: 왼쪽은 입력 양식, 오른쪽은 실시간 미리보기.
#pragma once

#include <windows.h>

#include <initializer_list>
#include <map>
#include <string>
#include <vector>

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

    // 글꼴 고르기
    void LoadSystemFonts();
    HWND AddFontCombo(int id);
    void SelectFontInCombo(HWND combo, const std::wstring& face);
    void DrawFontItem(const DRAWITEMSTRUCT* item);
    HFONT PreviewFont(const std::wstring& face);

    // 글자 크기·제목 배치
    HWND AddSizeCombo(int id, std::initializer_list<const wchar_t*> sizes);
    void OnSizeChanged(int id, int code);
    void SetTitleAlign(nl::TitleAlign align);
    void SyncFormatControls();

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
    HWND headingFontLabel_ = nullptr, headingFont_ = nullptr, bodyFontLabel_ = nullptr, bodyFont_ = nullptr;
    HWND headingSizeLabel_ = nullptr, headingSize_ = nullptr, bodySizeLabel_ = nullptr, bodySize_ = nullptr;
    HWND titleLayoutLabel_ = nullptr, titleAlign_ = nullptr, titleSizeLabel_ = nullptr, titleSize_ = nullptr;
    HWND titleReset_ = nullptr;
    HWND status_ = nullptr;

    struct FontInfo {
        std::wstring name;
        bool hangul = false;  // 한글을 지원하는 글꼴
        bool symbol = true;   // 기호 글꼴(Wingdings 등): 이름을 그 글꼴로 그리면 읽을 수 없음
    };
    std::vector<FontInfo> systemFonts_;
    std::map<std::wstring, HFONT> previewFonts_;  // 글꼴 목록에서 이름을 그 글꼴로 보여 줄 때 쓰는 캐시
    std::map<std::wstring, bool> canDrawOwnName_;  // 글꼴이 자기 이름의 글자를 모두 가지고 있는지
};
