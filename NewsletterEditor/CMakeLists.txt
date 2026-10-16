# Visual Studio 솔루션(NewsletterEditor.sln) 대신 CMake로도 빌드할 수 있습니다.
#   cmake -S . -B build && cmake --build build --config Release
# Windows가 아닌 환경에서는 데이터 모델 단위 테스트만 빌드됩니다.
cmake_minimum_required(VERSION 3.16)
project(NewsletterEditor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8 /W4)
endif()

add_library(newsletter_core STATIC src/Newsletter.cpp src/HtmlExport.cpp)
target_include_directories(newsletter_core PUBLIC src)

if(WIN32)
    if(MINGW)
        enable_language(RC)
    endif()
    set(APP_SOURCES
        src/main.cpp
        src/MainWindow.cpp
        src/PreviewWindow.cpp
        src/Renderer.cpp
        src/ImageLibrary.cpp
        res/NewsletterEditor.rc)
    if(MSVC)
        # MSVC 링커가 매니페스트를 넣습니다(.rc에서는 MinGW일 때만 넣음).
        list(APPEND APP_SOURCES res/NewsletterEditor.manifest)
    endif()
    add_executable(NewsletterEditor WIN32 ${APP_SOURCES})
    target_compile_definitions(NewsletterEditor PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(NewsletterEditor PRIVATE newsletter_core comctl32 comdlg32 gdi32 gdiplus ole32 shell32)
    if(MINGW)
        target_link_options(NewsletterEditor PRIVATE -municode -static)
    endif()
endif()

enable_testing()
add_executable(NewsletterTests tests/NewsletterTests.cpp)
target_link_libraries(NewsletterTests PRIVATE newsletter_core)
add_test(NAME NewsletterTests COMMAND NewsletterTests)
