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

## 다른 프로젝트

- [`ScreenPen/`](ScreenPen/README.md) — Windows 화면 판서 프로그램 (C# WinForms, Visual Studio Community)
