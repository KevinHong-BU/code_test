// 그림(로고·사진) 파일을 GDI+로 읽어 보관하고 그려 주는 캐시.
// GdiplusStartup 이후에 만들고 GdiplusShutdown 전에 없애야 합니다.
#pragma once

#include <windows.h>

#include <map>
#include <memory>
#include <string>

#include "Newsletter.h"

namespace Gdiplus {
class Bitmap;
}

class ImageLibrary {
public:
    ImageLibrary();
    ~ImageLibrary();
    ImageLibrary(const ImageLibrary&) = delete;
    ImageLibrary& operator=(const ImageLibrary&) = delete;

    // 그림 파일로 읽을 수 있는지 확인하고 원본 크기(픽셀)를 돌려줍니다.
    static bool ProbeSize(const std::string& data, int* width, int* height);

    // 캐시된 그림의 픽셀 크기(EXIF 회전 반영). 읽을 수 없으면 false.
    bool PixelSize(int id, const std::string& data, int* width, int* height);
    bool PixelSize(const nl::Picture& picture, int* width, int* height) {
        return PixelSize(picture.id, picture.data, width, height);
    }

    // 논리 좌표 rect에 그림을 그립니다. forPrint면 원본 해상도, 아니면 화면용 축소본을 씁니다.
    // id는 문서 안에서 고유한 그림 번호(그림·도서 표지 공통), data는 그림 파일 내용입니다.
    void Draw(HDC hdc, int id, const std::string& data, const RECT& rect, bool forPrint);
    void Draw(HDC hdc, const nl::Picture& picture, const RECT& rect, bool forPrint) {
        Draw(hdc, picture.id, picture.data, rect, forPrint);
    }

    // 쪽 배경을 rect(논리 좌표, 보통 쪽 전체)에 맞춤 방식과 진하기대로 그립니다.
    void DrawBackground(HDC hdc, const nl::Background& background, const RECT& rect, bool forPrint);

    // 문서에 더 이상 없는 그림·표지·배경을 캐시에서 지웁니다.
    void Prune(const nl::Newsletter& doc);

private:
    struct Entry;
    Entry* Get(int id, const std::string& data);

    std::map<int, std::unique_ptr<Entry>> cache_;
};
