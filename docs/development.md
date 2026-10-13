# 개발 안내

Tempos는 Windows 11 x64용 C++20·Win32·Direct2D·DirectWrite 앱이다. 사용자 조작은 [사용자 매뉴얼](../README.md), 수행한 검사와 제약은 [검증 기록](verification.md)을 기준으로 한다.

## 환경과 빌드

Visual Studio 2022 C++ Build Tools, Windows SDK, CMake/Ninja, PowerShell이 필요하다. 실행 앱에는 .NET이 필요 없으며 FlaUI E2E 도구만 .NET 10을 사용한다.

```powershell
./scripts/build.ps1 -Preset release -Clean -Test
./out/release/Tempos.exe --settings
./scripts/build.ps1 -Preset asan -Clean -Test
```

DPAPI와 UI 검사는 실제 Windows 사용자 컨텍스트에서 실행한다. 샌드박스의 DPAPI 실패를 저장 기능 실패 또는 검사 통과로 바꾸어 기록하지 않는다. 빌드 스크립트는 VS 개발 환경과 UTF-8을 구성한다. 헤더 의존성을 추적하는 Ninja 구성을 유지한다.

## 구현 구조

| 영역 | 코드와 책임 |
| --- | --- |
| 모델·저장 | `model.*`, `storage.*`: 위젯·모니터·날씨 타입, JSON, 원자적 저장, 정상 백업, DPAPI |
| 앱·렌더링 | `app.*`, `render.*`, `accessibility.*`: 네이티브 설정창·트레이·위젯 창, 정적 렌더링, UIA |
| 공유 수집 | `providers.*`: 공급자·위치별 구독 공유, 세대 번호, 제한된 작업 큐 |
| 날씨 | `weather.*`, `open_meteo.cpp`: 기상청과 Open-Meteo 파싱·캐시·재시도 |
| 인증·통신 | `calendar.*`, `http.*`: 조회 전용 Google OAuth·일정, HTTPS 허용 호스트·응답 크기·시간 제한 |
| 자동 시작 | `startup.*`: 현재 사용자 Run 등록과 명령행 인용 |

날씨 선택은 `Settings.weatherProvider`의 전역 값이다. JSON 버전은 1을 유지하고 `kma` / `open-meteo` 문자열을 추가한다. 필드가 없는 기존 파일·레이아웃은 기상청, 새 설정은 Open-Meteo다. 저장 실패 시 설정을 교체하지 않는다.

수집 요청은 생성 당시 공급자를 갖는다. 기상청은 격자, Open-Meteo는 위경도로 공유하며 세대가 지난 응답은 게시하지 않는다. 기상청 키와 캐시를 Open-Meteo 요청에 섞지 않는다. 기상청 캐시는 기존 파일을 유지하고 Open-Meteo 캐시는 별도 DPAPI 파일에 필요한 숫자 필드만 보관한다.

렌더링의 날씨 상태는 공통 enum으로 표현한다. PTY/SKY와 WMO는 각 파서에서 변환한다. Open-Meteo의 현재값을 실측 관측값으로 표시하지 않는다. 정적 날씨 배경을 위해 상시 프레임 루프나 추가 API 호출을 만들지 않는다.

Open-Meteo의 작은 JSON 응답은 압축 협상 없이 받는다. 로컬 Windows의 자동 압축 해제 경로에서 HTTP 200 뒤 본문 수신이 실패했고, 비압축 요청의 실제 파서 통과를 확인했다. HTTPS·호스트 제한·1MiB 응답 상한·시간 제한은 유지한다.

## 데이터와 격리

기본 저장 위치는 `%LocalAppData%\Tempos`다. `--data-dir <폴더>`로 별도 프로필을 열 수 있다. 이 옵션으로 자동 시작을 등록하면 절대 데이터 경로도 명령에 보존한다.

테스트는 `Tempos.TestHost.exe`와 별도 데이터 폴더를 사용한다. 외부 날씨·Google Calendar·OAuth·위치는 항상 목업이며, `--live-system`은 로컬 Windows 수집만 실제로 전환한다. 외부 API 목업과 제어 명령은 배포 앱에 포함하지 않는다. [E2E 기준](e2e.md)에 따라 실연동은 별도로 구분한다. TestHost의 자동 시작 레지스트리는 `HKCU\Software\Tempos\Tests` 아래에 격리되며 Windows의 실제 Run 키에 등록하지 않는다. 배포 앱에는 레지스트리 경로를 바꾸는 실행 옵션을 제공하지 않는다.

## 검사와 산출물

[E2E 실행 안내](e2e.md)의 관련 검사부터 수행한다. 핵심 검사에는 저장 호환, 날씨 파서·캐시·요청 공유·늦은 응답, 자동 시작 명령행과 레지스트리 처리가 포함된다.

```powershell
./out/release/tempos_tests.exe
./out/release/tempos_tests.exe --live-weather
./scripts/package.ps1 -SkipBuild
```

`--live-weather`는 실제 Open-Meteo 요청이다. 일반 핵심 검사는 제어된 응답을 사용한다. 개인정보와 인증키를 로그에 출력하지 않는다. ASan 실행 파일을 별도 터미널에서 실행할 때에는 해당 MSVC의 `bin/Hostx64/x64` 경로를 PATH에 포함하여 ASan 런타임 DLL을 찾게 한다.

- `out/`: 빌드별 실행 파일과 캐시.
- `test-results/`: 실행별 E2E·성능 원본. 사용자 바탕화면이 포함될 수 있어 Git 제외.
- `artifacts/`: 빌드·검사 로그, 배포 ZIP·해시. Git 제외.
- `docs/validation/`: 장기 보존할 검증 요약. 사용자 데이터와 키는 포함하지 않는다.
- `docs/assets/`: 문서용으로 검토한 앱 영역 캡처.

같은 이름의 바이너리를 같은 빌드로 간주하지 않는다. 검사 시각과 SHA-256을 기록한다. 문서 전용 수정은 링크·이미지·앵커와 `git diff --check`로 확인한다. 패키징은 [로컬 검증·배포 안내](local-release-guide.md)를 따른다.
