# 화면 판서 (ScreenPen)

Windows 화면을 **정지(캡처)한 뒤 그 위에 펜으로 글씨를 쓰며 설명**하는 프로그램입니다.
C# WinForms(.NET 8)로 만들었고, Visual Studio Community에서 바로 열어 실행할 수 있습니다.

## 기능

| 요구 사항 | 구현 내용 |
|---|---|
| 1. 화면 정지 후 판서, 제어 기능 표시 | 실행하면 마우스가 있는 모니터 화면을 캡처해 전체 화면으로 띄우고, 위쪽에 **도구 모음**을 표시합니다. 도구 모음은 `∷` 손잡이로 끌어서 옮기고 `◀` 버튼으로 접을 수 있습니다. |
| 2. 펜 색 바꾸기 | 빨강·파랑·검정·초록·노랑·흰색 6가지 + `+` 버튼으로 원하는 색 선택. 펜 / 형광펜, 굵기(얇게·보통·굵게) 선택 |
| 3. 판서 지우기 | **지우개**(문지른 부분만 지움, 펜 사용 중 오른쪽 버튼 드래그로도 가능) / **영역 지우기**(사각형을 그려 그 안만 지움) / **전체 지우기**. 모두 실행 취소·다시 실행 가능 |
| 4. 화면 저장 | **저장**: 판서한 화면을 PNG/JPG/BMP로 저장 (기본 폴더: `사진\화면판서`). **복사**: 클립보드로 복사해 바로 붙여넣기 |
| 5. 끝내고 돌아가기 | **판서 끄기**: 판서 화면만 닫고 화면 오른쪽 아래에 동그란 **[판서] 버튼**을 띄워 대기. 버튼을 누르면 그 순간의 화면을 다시 정지해 판서 시작 (버튼은 끌어서 이동 가능). **종료**: 프로그램 완전 종료 |

저장하지 않은 판서가 있을 때 판서 끄기/종료를 누르면 저장할지 물어봅니다.
시스템 트레이 아이콘(작업 표시줄 오른쪽)에서도 시작·대기·종료를 할 수 있습니다.

## 단축키

| 키 | 동작 |
|---|---|
| `Ctrl+Shift+D` | (어디서나) 판서 시작 ↔ 판서 끄기 |
| `P` / `H` | 펜 / 형광펜 |
| `1` ~ `6` | 색상 선택 |
| `[` / `]` | 굵기(지우개 선택 시 지우개 크기) 줄이기 / 늘리기 |
| `E` / `A` / `Delete` | 지우개 / 영역 지우기 / 전체 지우기 |
| 오른쪽 버튼 드래그 | 임시 지우개 |
| `Ctrl+Z` / `Ctrl+Y` | 실행 취소 / 다시 실행 |
| `Ctrl+S` / `Ctrl+C` | 저장 / 클립보드 복사 |
| `Esc` | 판서 끄기 (대기) |
| `Alt+F4` | 종료 |

## 빌드 및 실행 (Visual Studio Community)

1. Visual Studio Community 2022(17.8 이상)를 설치할 때 **".NET 데스크톱 개발"** 워크로드를 선택합니다.
   (이미 설치했다면 `Visual Studio Installer > 수정`에서 추가)
2. 저장소를 내 사용자 폴더 아래에 받고 `ScreenPen\ScreenPen.sln`을 엽니다.
   ```
   cd %USERPROFILE%\source\repos
   git clone https://github.com/KevinHong-BU/code_test.git
   start code_test\ScreenPen\ScreenPen.sln
   ```
   > 관리자 권한 명령 프롬프트는 시작 위치가 `C:\Windows\System32`라서, 그대로 클론하면
   > 빌드할 때 "액세스가 거부되었습니다" 오류가 납니다. 일반 명령 프롬프트를 쓰거나 위처럼 먼저 `cd` 하세요.
3. `F5`(디버그 실행) 또는 `Ctrl+F5`(디버그 없이 실행)를 누릅니다.

실행 파일은 `code_test\ScreenPen\ScreenPen\bin\Debug\net8.0-windows\ScreenPen.exe`에 만들어집니다.

### 다른 PC에 배포할 단일 exe 만들기

솔루션 탐색기에서 프로젝트 우클릭 → **게시(Publish)** → 폴더 → 배포 모드 "자체 포함", 파일 게시 옵션 "단일 파일 생성"을 선택합니다.
또는 명령줄에서 (**클론한 `code_test` 폴더 안에서** 실행):

```
cd code_test
dotnet publish ScreenPen\ScreenPen\ScreenPen.csproj -c Release -r win-x64 --self-contained true -p:PublishSingleFile=true
```

> `MSB1009: 프로젝트 파일이 없습니다` 오류가 나면 현재 폴더가 `code_test`인지 확인하세요.
> (`dir` 명령을 쳤을 때 `ScreenPen` 폴더와 `README.md`가 보여야 합니다.)

만들어진 `code_test\ScreenPen\ScreenPen\bin\Release\net8.0-windows\win-x64\publish\ScreenPen.exe` 하나만 복사하면 .NET이 없는 PC에서도 실행됩니다.

### 실행 옵션

- `ScreenPen.exe /standby` : 판서 화면 없이 [판서] 버튼만 띄운 대기 상태로 시작 (Windows 시작 프로그램에 등록할 때 유용)

## 구조

```
ScreenPen/
├── ScreenPen.sln
└── ScreenPen/
    ├── ScreenPen.csproj
    ├── Program.cs             # 시작 지점, 중복 실행 방지
    ├── AppController.cs       # 판서 / 대기 / 종료 전환, 화면 캡처, 트레이 아이콘, 전역 단축키
    ├── OverlayForm.cs         # 정지된 화면 위 판서 (그리기, 지우기, 실행 취소, 저장)
    ├── ToolbarForm.cs         # 판서 화면 위의 제어 도구 모음
    ├── FloatingButtonForm.cs  # 대기 모드의 동그란 [판서] 버튼
    ├── InkStroke.cs           # 획 데이터 (부분 지우기 시 획을 잘라 나눔)
    ├── IconFactory.cs         # 아이콘/펜 그림
    └── NativeMethods.cs       # Win32 API (창 끌기, 전역 단축키)
```

UI는 디자이너 없이 코드로 만들어서 파일 수가 적고 흐름을 따라 읽기 쉽습니다.

## 참고

- 여러 모니터를 쓰는 경우, 판서를 시작할 때 **마우스가 있는 모니터**를 정지·판서합니다.
- 고해상도(배율 125%·150% 등) 화면에서도 캡처 화면이 흐려지지 않도록 Per-Monitor DPI 모드로 동작합니다.
- 펜 태블릿·터치 입력은 마우스 입력으로 처리됩니다 (필압은 지원하지 않음).
