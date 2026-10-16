# Disk Usage Tree (C++ / Visual Studio)

Windows 11에서 드라이브 루트(예: `C:\`)부터 **모든 하위 디렉토리가 차지하는 용량**을
계산하여 브라우저에서 **트리 형태**로 보여주는 로컬 웹 프로그램입니다.
`../disk-usage-tree` (Python 버전)를 C++ / Win32 API로 옮긴 버전입니다.

- 외부 라이브러리 없음: Win32 API + Winsock + C++17 표준 라이브러리만 사용
- 웹 UI(`web/index.html`)는 리소스로 실행 파일에 포함되어 **exe 하나로 동작**
- Release 빌드는 정적 런타임(/MT)이라 VC++ 재배포 패키지 없이 다른 PC에서도 실행 가능
- 서버는 `127.0.0.1`에서만 동작하여 외부에서 접근할 수 없음
- 260자를 넘는 긴 경로(`\\?\` 접두사)와 한글 경로 지원
- 정션/심볼릭 링크는 따라가지 않음 (중복 계산·무한 루프 방지)

## 빌드 방법 (Visual Studio Community)

1. Visual Studio Community 2022 이상을 설치할 때 **"C++를 사용한 데스크톱 개발"** 워크로드를 선택합니다.
2. `DiskUsageTree.sln`을 더블클릭해서 엽니다.
   - Visual Studio 2026에서 열면 "프로젝트 대상 변경(Retarget)" 창이 뜰 수 있습니다. **확인**을 눌러 최신 도구 집합으로 바꾸면 됩니다.
3. 상단 도구 모음에서 구성을 **Release / x64**로 선택합니다.
4. **빌드 → 솔루션 빌드** (`Ctrl+Shift+B`)를 실행합니다.
5. `bin\Release\DiskUsageTree.exe`가 만들어집니다.

Visual Studio 안에서 바로 실행하려면 `Ctrl+F5`(디버그하지 않고 시작)를 누릅니다.

## 실행 방법

1. `DiskUsageTree.exe`를 더블클릭합니다.
   `C:\Windows`처럼 보호된 폴더까지 읽으려면 **마우스 오른쪽 버튼 → 관리자 권한으로 실행**을 선택하세요.
2. 브라우저가 `http://127.0.0.1:8765/`로 자동으로 열립니다.
3. 드라이브를 선택하거나 경로를 입력한 뒤 **[스캔 시작]**을 누릅니다.
4. 스캔이 끝나면 ▶를 클릭하거나 폴더 이름을 더블클릭해 하위 폴더를 펼칩니다.
   행에 마우스를 올리면 나오는 [열기] 버튼으로 탐색기에서 해당 폴더를 엽니다.

명령줄 옵션:

```bat
DiskUsageTree.exe                 :: 기본 포트 8765
DiskUsageTree.exe --port 9000     :: 포트 변경
DiskUsageTree.exe --no-browser    :: 브라우저 자동 열기 끄기
```

종료하려면 콘솔 창을 닫거나 `Ctrl+C`를 누릅니다.

## 프로젝트 구조

```
disk-usage-tree-cpp/
├── DiskUsageTree.sln
└── DiskUsageTree/
    ├── DiskUsageTree.vcxproj      # 프로젝트 (x64 Debug/Release, C++17, /utf-8)
    ├── DiskUsageTree.rc           # web/index.html 을 RCDATA 리소스로 포함
    ├── web/index.html             # 트리 UI (HTML/CSS/JS)
    └── src/
        ├── main.cpp               # 진입점, API 라우팅
        ├── Scanner.h / .cpp       # 백그라운드 디렉토리 스캔 및 크기 합산
        ├── HttpServer.h / .cpp    # Winsock 기반 최소 HTTP 서버
        ├── Util.h / .cpp          # UTF-8 변환, 경로, JSON, URL 유틸리티
        └── resource.h
```

| API | 설명 |
| --- | --- |
| `GET /api/roots` | 드라이브 목록과 사용량 |
| `POST /api/scan` `{"path": "C:\\"}` | 백그라운드 스캔 시작 |
| `GET /api/status` | 스캔 진행 상황 |
| `POST /api/cancel` | 스캔 취소 |
| `GET /api/children?path=...&limit=200` | 폴더의 하위 폴더 목록(크기순) |
| `POST /api/open` `{"path": "..."}` | 탐색기에서 폴더 열기 |

## 참고

- 표시되는 크기는 파일의 논리 크기 합계(탐색기의 "크기" 기준)입니다.
  압축된 파일이나 OneDrive 온라인 전용 파일은 실제 디스크 사용량과 다를 수 있습니다.
- 권한이 없어 읽지 못한 항목은 ⚠ 숫자로 표시되며 크기 합계에서 빠집니다.
- `web/index.html`을 수정한 뒤 다시 빌드하면 exe에 반영됩니다.
