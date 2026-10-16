// 도서 한 권의 정보(서명·저자·출판사·발행연도·청구기호·소개·표지)를 입력하는 대화 상자.
#pragma once

#include <windows.h>

#include <functional>

#include "Newsletter.h"

class ImageLibrary;

class BookDialog {
public:
    // 그림 파일을 골라 picture에 채우는 함수(파일 대화 상자의 주인 창을 받음). 취소하면 false.
    using LoadPicture = std::function<bool(HWND owner, nl::Picture* picture)>;

    // book을 고쳐 [확인]을 누르면 true. 대화 상자가 떠 있는 동안 owner는 사용할 수 없습니다.
    static bool Run(HWND owner, HINSTANCE instance, HFONT font, int dpi, ImageLibrary* images,
                    const LoadPicture& loadPicture, const wchar_t* caption, nl::Book* book);
};
