# 내 스케줄 (MySchedule)

개인 일정 관리를 위한 안드로이드 앱입니다. Kotlin + Jetpack Compose(Material 3) + Room으로 만들었습니다.

## 기능

- **월간 캘린더**: 날짜별 일정을 색상 점으로 표시, 좌우 스와이프/화살표로 월 이동, `오늘` 버튼
- **일별 일정 목록**: 선택한 날짜의 일정을 시간순으로 표시(종일 일정이 먼저), 남은 일정 개수 표시
- **일정 추가/수정/삭제**: 제목, 날짜, 종일 여부, 시작·종료 시간, 색상, 메모
- **완료 체크**: 목록에서 체크박스로 완료 처리(취소선 표시)
- **알림**: 정시 / 10분 전 / 30분 전 / 1시간 전 / 1일 전 (종일 일정은 오전 9시 기준)
  - 재부팅 후에도 알림이 자동으로 다시 등록됩니다.
- 시스템 다크 모드 및 Android 12+ 다이내믹 컬러 지원

## 빌드 및 실행

요구 사항: Android Studio (Ladybug 이상 권장), JDK 17+, Android SDK 35

1. Android Studio에서 이 폴더를 엽니다 (`File > Open`).
2. Gradle 동기화가 끝나면 기기/에뮬레이터를 선택하고 ▶ Run.

명령줄:

```bash
./gradlew assembleDebug        # app/build/outputs/apk/debug/app-debug.apk 생성
./gradlew installDebug         # 연결된 기기에 설치
./gradlew testDebugUnitTest    # 단위 테스트
```

최소 지원 버전: Android 8.0 (API 26)

## 구조

```
app/src/main/java/com/kevinhong/schedule/
├── ScheduleApp.kt              # Application: DB, 저장소, 알림 채널 초기화
├── data/
│   ├── Schedule.kt             # 일정 엔티티 + 알림 시각 계산
│   ├── ScheduleDao.kt          # Room DAO
│   ├── ScheduleDatabase.kt
│   └── ScheduleRepository.kt   # DB 저장과 알림 등록을 함께 처리
├── reminder/
│   ├── ReminderScheduler.kt    # AlarmManager로 알림 예약/취소
│   ├── ReminderReceiver.kt     # 알림 표시
│   └── BootReceiver.kt         # 재부팅 시 알림 재등록
└── ui/
    ├── MainActivity.kt         # 내비게이션
    ├── ScheduleViewModel.kt
    ├── CalendarScreen.kt       # 캘린더 + 일별 목록
    ├── EditScheduleScreen.kt   # 일정 추가/수정
    └── theme/Theme.kt
```

## 권한

- `POST_NOTIFICATIONS`: Android 13+에서 앱 첫 실행 시 요청합니다.
- `SCHEDULE_EXACT_ALARM`: 정확한 시간 알림용. Android 14+에서는 기본 거부 상태이며, 이 경우
  알림은 시스템 사정에 따라 약간 늦게 울릴 수 있습니다. 정확한 알림을 원하면
  `설정 > 앱 > 내 스케줄 > 알람 및 리마인더`에서 허용하세요.

---

# 카메라 프롬프터 (웹)

`web/prompter/index.html` — 설치 없이 브라우저에서 동작하는 카메라 프롬프터입니다. 파일 하나로 되어 있습니다.

- 카메라 화면 위(렌즈 가까운 쪽)에 대본이 자동으로 스크롤되어, 렌즈를 보며 읽을 수 있습니다.
- 녹화 버튼으로 영상 녹화 → 미리보기 → 저장/공유. **녹화 파일에는 대본 글자가 찍히지 않습니다.**
- 녹화 전 카운트다운, 녹화 시작 시 자동 스크롤, 전면/후면 카메라 전환, 해상도(720p/1080p/4K)·화면 비율 선택
- 글자 크기·폭·속도·배경 어둡기·영역 높이 조절, 읽는 위치 표시선, 좌우/상하 반전(프롬프터 유리용)
- 드래그로 위치 이동, 탭으로 재생/정지, 텍스트 파일 불러오기, 예상 낭독 시간 표시
- 대본과 설정은 브라우저에 자동 저장, 재생·녹화 중 화면 꺼짐 방지
- 단축키: `Space` 재생/정지, `↑↓` 속도, `←→` 이동, `R` 녹화, `E` 편집, `F` 카메라 전환, `+/-` 글자 크기

카메라는 보안상 **HTTPS 또는 localhost**에서만 켜집니다. 로컬에서 실행:

```bash
cd web/prompter && python3 -m http.server 8000   # http://localhost:8000
```

휴대폰에서 쓰려면 GitHub Pages 등 HTTPS 주소로 배포해서 여세요.
