// 쪽 배경 그림(그림·맞춤 방식·진하기)을 정하는 대화 상자.
#pragma once

#include <windows.h>

#include "BookDialog.h"
#include "Newsletter.h"

class ImageLibrary;

class BackgroundDialog {
public:
    // page < 0이면 모든 쪽 배경을, 아니면 그 쪽(0부터)의 배경을 고칩니다.
    // 쪽 배경일 때 *separate는 '이 쪽만 따로 설정' 여부입니다(끄면 모든 쪽 배경을 따름).
    // [확인]을 누르면 true.
    static bool Run(HWND owner, HINSTANCE instance, HFONT font, int dpi, ImageLibrary* images,
                    const BookDialog::LoadPicture& loadPicture, int page, bool* separate, nl::Background* background);
};
