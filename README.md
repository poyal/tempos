# Tempos

Windows 11 x64용 네이티브 바탕화면 위젯. C++20, Win32, Direct2D와 DirectWrite를 사용하며 웹 런타임을 포함하지 않습니다.

시계 · 날씨 · CPU · GPU · 메모리 · 시스템 정보 · 네트워크 · 디스크 · 캘린더의 9종을 제공합니다. 배터리, 집중 타이머, 메모·체크리스트는 포함하지 않습니다.

## 실행

배포 ZIP을 원하는 폴더에 풀고 `Tempos.exe`를 실행하세요. `data` 폴더를 실행 파일과 함께 유지하세요. 별도의 관리자 권한이나 .NET 설치는 필요하지 않습니다.

- 알림 영역의 Tempos 아이콘을 누르면 설정이 열립니다. 아이콘이 안 보이면 작업표시줄의 `^`를 확인하세요. 숨겨진 아이콘 영역에 둘지는 Windows에서 결정합니다.
- 설정창을 닫으면 위젯은 계속 실행됩니다. 종료하려면 트레이 메뉴 또는 설정 → 앱 → **Tempos 종료**를 사용하세요.
- 설정 → 위젯에서 추가, 제거, 크기, 확대, 테마, 갱신 주기, 모니터, 투명도를 바꿀 수 있습니다.
- **정렬 모드**에서는 그리드가 나타나고 위젯을 드래그할 수 있습니다. 위치 잠금과 클릭 통과도 지원합니다. 전체 투명도 100%로 숨긴 위젯도 정렬 모드에서 다시 찾을 수 있습니다.
- 전체 테마 9개와 위젯별 테마를 지원합니다. 날씨의 자동 배경을 켜면 비·눈·구름·낮·밤에 따라 정적인 배경이 달라집니다.
- 설정 → 표시 옵션에서 12시간제, °F, 네트워크 바이트 단위, 디스크 이진 단위, 월요일 시작, 시스템 정보 항목을 지정합니다.

| 크기 | 기본 크기 (DIP) |
| --- | --- |
| S | 160 × 160 |
| Slim | 336 × 72 |
| M | 336 × 160 |
| L | 336 × 336 |
| XL — 캘린더 전용 | 1008 × 840 |

확대는 100/150/200%이며 형태의 비율을 유지합니다. 화면에 들어가지 않는 크기는 적용하지 않습니다. 모니터별 4 DIP 그리드에 맞추고 위젯 사이 16 DIP 간격을 확보합니다. 모니터가 없어지면 가능한 화면에 임시 배치하며 원래 위치를 보존합니다.

## 데이터 연결

**날씨:** [기상청 API허브](https://apihub.kma.go.kr/)에서 본인의 인증키를 발급받아 설정 → 연동에 입력하세요. 공공데이터포털의 `ServiceKey`와 다른 키입니다. 공식 지역 목록에서 지역을 선택합니다. 기상청의 실황·초단기예보·단기예보를 조합하며 각 위젯에 `자료: 기상청`을 표시합니다. 5분 갱신을 선택해도 외부 수집은 최소 10분 간격으로 제한합니다. 인증 오류·한도·통신 실패 때 재시도 간격을 늘리고, 최근 자료는 지연 상태로 유지합니다. 2시간 지난 관측의 현상 배경은 사용하지 않으며 24시간 지난 캐시는 폐기합니다. 키는 앱에 내장되지 않습니다.

**캘린더:** Google Cloud 프로젝트에서 Calendar API를 사용 설정하고 **데스크톱 앱** OAuth 클라이언트를 만든 뒤 설정 → 연동에 ID와 발급된 보안 비밀을 입력하세요. 브라우저에서 승인한 후 표시할 캘린더를 선택합니다. 개발용 Testing 프로젝트는 테스트 사용자 등록과 갱신 토큰 만료 조건을 확인하세요. [Google 설치형 앱 인증 안내](https://developers.google.com/identity/protocols/oauth2/native-app)

캘린더는 **조회만** 지원합니다. 일정 추가·수정·삭제·참석 응답 기능과 쓰기 권한 요청이 없습니다. 연결 명칭과 인증 상태는 설정에만 표시합니다. 날짜를 누르면 읽기 전용 상세가 열리고, L/XL에는 월 이동이 있습니다. 연결 해제는 로컬 토큰과 일정 캐시를 지웁니다. Google 계정의 권한 철회는 계정 설정에서 별도로 할 수 있습니다.

**시스템:** CPU/GPU는 Windows PDH, 메모리는 Windows 메모리 API를 사용합니다. 네트워크는 설정에서 어댑터를 선택하세요. 디스크는 로컬 볼륨을 선택하며 기본값은 Windows 볼륨입니다. 지원되지 않는 GPU 카운터와 제거된 장치는 0%로 꾸미지 않고 상태를 표시합니다. 온도, 팬 속도, 디스크 SMART, 프로세스별 목록은 이 버전에 포함하지 않습니다.

## 저장과 개인정보

기본 데이터 폴더는 `%LocalAppData%\Tempos`입니다. 설정과 레이아웃은 JSON으로 저장하고 정상 설정의 백업을 유지합니다. 인증키·OAuth 정보·일정 캐시는 Windows 사용자 범위 DPAPI로 보호합니다. 레이아웃 내보내기에는 인증 정보와 일정이 포함되지 않습니다. 다른 PC나 Windows 사용자로 옮기면 다시 연결해야 합니다.

설정 → 앱의 진단 저장은 사용량, 창 배치, 모니터 정보와 수집 개수를 기록합니다. 전체 요청 URL·인증키·일정 제목은 기록하지 않습니다. 앱 자체 서버나 사용 분석 전송은 없습니다.

## 빌드와 테스트

Visual Studio 2022 C++ Build Tools, Windows SDK, CMake/Ninja와 PowerShell을 사용합니다. E2E 실행에만 .NET 10 SDK와 FlaUI.UIA3가 필요합니다.

```powershell
./scripts/build.ps1 -Preset release -Clean -Test
./out/release/Tempos.exe --settings

dotnet restore tests/e2e/Tempos.E2E.csproj --locked-mode
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release -- .
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release -- . --matrix
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release -- . --behavior
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release -- . --interaction
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release -- . --persistence
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release -- . --failures
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release -- . --options
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release -- . --capacity
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release -- . --placement
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release -- . --clock-layout
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release -- . --design
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release -- . --stress
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release -- . --visual
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release -- . --live-smoke

./scripts/build.ps1 -Preset asan -Clean -Test
./scripts/performance.ps1 -WarmupSeconds 300 -SampleSeconds 1800
./scripts/package.ps1
```

DPAPI와 E2E는 같은 실제 Windows 사용자 세션에서 실행해야 합니다. E2E는 실행 창과 바탕화면을 사용하므로 해당 시간에는 다른 조작을 피하세요. 테스트용 실행 파일 `Tempos.TestHost.exe`만 고정 샘플 데이터를 사용하며 배포 ZIP에는 넣지 않습니다. `Tempos.exe --data-dir <폴더>`로 격리된 설정을 사용할 수 있습니다.

`--live-smoke`는 샘플 대신 실제 CPU·GPU·메모리 카운터를 확인하므로 GPU 카운터가 없는 장치에서는 실패할 수 있습니다. 패키징 결과는 `artifacts/Tempos-0.1.0-win-x64.zip`과 SHA-256 파일이며, `--smoke --exe <패키지의 Tempos.exe 경로>`로 실제 배포 실행 파일도 검증할 수 있습니다. 성능 스크립트는 다른 Tempos 프로세스를 종료한 상태에서 실행하세요.

`--persistence`는 9종 추가·적용, 정렬 모드 반복, 설정창 닫기, 숨김·표시, 재시작, 중간 항목 삭제 후 모든 위젯의 저장 ID·실제 창·화면 히트 테스트를 대조합니다. `--failures`는 파일 잠금에 의한 저장 실패, 잘못된 입력·가져오기, 정상 백업 복구를 확인합니다. `--options`는 위젯별 옵션 격리·배율·항상 위·전체 테마를, `--capacity`는 64개 한도와 빈 레이아웃을 검사합니다.

`--placement`는 연결되지 않은 모니터의 레이아웃을 가져와 임시 배치한 뒤, 위젯을 추가해도 기존 위젯이 숨겨지지 않고 공간 부족을 알리는지 검사합니다. 실제 모니터를 분리하지는 않습니다. `--clock-layout`는 시계 4개 크기 × 12/24시간 × 초 표시 유무의 16개 조합을 검증하고 위젯 영역만 캡처합니다.

디자인 검수 기준은 [디자인 가이드](docs/design-guide.md)에 있습니다. `--design`은 테스트 호스트에서 37개 크기별 구성, 9개 테마, 설정 4개 탭, 날씨·장치·캘린더 상태를 캡처하고 창의 크기·배치·표시 여부를 검사합니다. 긴 장치명과 결측 그래프 등은 제작용 데이터이며 실제 센서 측정이 아닙니다. `./scripts/design-review.ps1 -Results <E2E 결과 폴더>`로 비교 이미지를 만든 뒤 별도 육안 검수합니다. 이 모드의 전용 시각 데이터 명령은 배포 실행 파일에 포함되지 않습니다.

기존 설정으로 재현할 때는 `--saved-layout <settings.json 경로> --exe <Tempos.exe 경로>`를 사용하세요. 원본은 테스트 폴더에 복사하고, 각 위젯 적용·9종 추가·정렬 종료·재시작을 검사합니다. 원본 설정 파일은 변경하지 않습니다.

`--stress`는 준비 100회·5분 대기 후 1,000회 위젯 생성·테마/확대/투명도 변경·정렬 시작/종료·제거를 수행하고 5분 안정화합니다. 각 회차의 실제 저장값을 대조하며 메모리 잔여 증가 2MiB와 GDI/USER/핸들 증가를 검사합니다. `stress-handles.json`에는 반복 중 100회마다, 대기 중 30초마다 자원 수와 핸들 종류를 기록합니다. 성능 스크립트의 기본 표본 간격은 1초이며, 기본 `-Budget Five`에서 평균 CPU 0.1%, P95 0.3%, CPU 시간 4ms/초, Private Bytes 32MiB, Working Set 48MiB를 넘으면 실패합니다. `-Budget Nine`은 9종 프로필에 CPU 0.2%, Private Bytes 48MiB, Working Set 72MiB 기준을 적용합니다. `-Budget Clock`은 시계 1개의 CPU 0.02%를, `-Budget None`은 예산 판정 없이 측정만 지원합니다. 프로필의 위젯 수와 실제 표시 여부도 확인합니다.

성능 결과는 `report.json`과 1초 표본 `samples.jsonl`에 남습니다. 5분 준비·30분 측정·1초 표본 조건을 충족하지 못한 실행은 `standardDuration: false`로 구분합니다. 실제 날씨·일정 연결이 없는 측정을 해당 연결까지 포함한 인수 결과로 해석하지 마세요.

검증 결과와 남은 제약은 [개발·검증 기록](docs/development.md), 설계와 연동 근거는 [plan.md](plan.md)에 기록합니다. 낮은 사용량은 측정 조건과 함께 판단하며 모든 PC의 수치나 메모리 누수 0건을 보장하는 문구를 사용하지 않습니다.

## 외부 자료

[nlohmann/json](https://github.com/nlohmann/json)은 MIT 라이선스입니다. 기상청 지역표의 출처와 변환 방법은 [data/README.md](data/README.md), 배포에 포함되는 고지는 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)에 있습니다. 앱 아이콘은 확정된 `design/app-icons/04-pulse.png`에서 생성했습니다.
