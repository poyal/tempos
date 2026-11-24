# E2E 실행과 격리

Tempos E2E는 FlaUI.UIA3로 실제 Win32 앱을 조작하고 저장 JSON·프로세스·창 배치·접근성 출력을 대조한다. [검증 기록](verification.md)에는 실제 실행한 범위만 남긴다.

## 기본 기준: 외부 API는 목업

2026-10-13부터 **일반 E2E는 외부 API를 호출하지 않는다.** `Tempos.TestHost.exe`에 HTTP 목업을 주입하며 실제 Windows 창·파서·캐시·DPAPI 저장·재시작 경로를 검사한다. API 결과를 곧바로 화면 데이터로 바꾸는 방식으로 파서 검사를 대체하지 않는다.

| 대상 | 검사 경계 |
| --- | --- |
| Open-Meteo·기상청 | 목업 HTTP 응답 → 실제 날씨 파서·캐시·위젯 |
| Google Calendar·OAuth | 목업 토큰·목록·일정 응답 → 실제 인증 상태·페이지 합치기·개인 캐시. 브라우저 승인은 앱 소유 루프백 콜백으로 모의 |
| 현재 위치 | 목업 좌표·권한 오류·지원 지역 밖 응답 → 실제 지역 선택·적용·재시작. OS 측위는 호출하지 않음 |
| Win32 UI·파일·DPAPI·프로세스 | 실제 Windows 동작 |
| CPU·GPU·메모리·디스크·전체 네트워크 카운터 | `--windows-real`과 `--live-smoke`에서는 실제 로컬 수집. 외부 API 목업은 유지 |
| 디자인용 데이터 | TestHost의 예제 공급자를 사용할 수 있지만 실측/API 검증으로 표시하지 않음 |

목업은 등록된 요청만 처리하고 알 수 없는 경로는 실패시킨다. 실제 서버로 대체 요청하는 경로는 없다. 목업 HTTP 클라이언트는 WinHTTP 세션을 만들지 않는다. 일반 E2E 종료 시 요청 기록에 미등록 요청이 없는지도 확인한다. 실제 API 키·Google 계정·위치 권한이 없어도 실행할 수 있어야 한다.

시나리오는 API 특성에 따라 정상·빈 목록·페이지 나눔·잘못된 응답·401/403·시간 초과·429·5xx·캐시 유지/만료·늦은 응답·저장/재시작을 포함한다. 단순 성공 응답만으로 완료 처리하지 않는다. 모의 OAuth는 앱의 콜백 검증·토큰 교환·갱신을 확인하며 Google의 실제 로그인·동의 화면은 검증하지 않는다.

## 실행 준비

README 화면 재촬영에는 `--ui-review`(위젯 37개 크기)와 `--readme`(설정·연동·백업·CPU 온도의 실제 조작)를 사용한다. 두 모드는 예제 수집기와 목업 API만 허용한다. CPU 센서 설치는 TestHost의 결과 목업을 사용하고, 시작 등록은 테스트 레지스트리에서 수행한다. 원본은 실행별 `test-results`에 저장한다. 검토한 앱 영역만 `scripts/release-screenshots.cjs <실행 목록.json> --output docs/assets/<날짜별 폴더> --version <버전>`으로 수집한다. `scripts/verify-docs.cjs`는 모든 보존 갤러리의 37개 크기, 필수 사용법 목록, 링크·앵커·PNG 해시를 확인한다. [최신 촬영 조건](screenshots.md#manual-20261017).

Windows 사용자 세션, .NET 10 SDK, Release 빌드가 필요하다. ASan 앱 검사에는 MSVC ASan 런타임 DLL 경로도 필요하다. 먼저 [개발 안내](development.md)에 따라 빌드한다.

```powershell
dotnet restore tests/e2e/Tempos.E2E.csproj --locked-mode
dotnet build tests/e2e/Tempos.E2E.csproj -c Release --no-restore
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release --no-build -- . --weather
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release --no-build -- . --api-mock
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release --no-build -- . --windows-real
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release --no-build -- . --startup
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release --no-build -- . --persistence
```

로컬 `.tools/dotnet`을 사용하는 경우 해당 `dotnet.exe`와 `DOTNET_ROOT`, `DOTNET_CLI_HOME`, `NUGET_PACKAGES`를 구성한다. 테스트 모드는 한 번에 하나를 지정하고 데스크톱 검사는 순차 실행한다.

현재 Tempos E2E는 창을 표시하고 바탕화면을 캡처한다. **숨김 E2E는 구현되어 있지 않다.** 작업 중인 화면과 겹치지 않는 사용자 세션에서 수행한다. Passport의 Electron 숨김 모드를 Tempos에도 구현된 것으로 설명하지 않는다.

## 검사 모드

| 옵션 | 확인하는 동작 |
| --- | --- |
| 없음 | 기본 9종 추가·설정·저장·재시작 |
| `--windows-real` | 실제 Windows 사용자 세션·CPU/GPU/메모리·전체 네트워크·로컬 디스크·그래프·OS 입력 시도·저장·재시작. 날씨·현재 위치는 목업 |
| `--temperature` | 센서 응답 경계 목업·설치 취소/실패/재부팅 요구·설정 저장/재시작·내보내기 격리·미지원/오래됨/복구·날씨/CPU/GPU/메모리 S/Slim/M/L×100/150/200% 예제 캡처·CPU/GPU/메모리/디스크 M의 흰색/주황색/빨간색/오래된 값 픽셀 확인 |
| `--api-mock` | OAuth 승인 거부·교환·갱신, 캘린더 목록·페이지 합치기·빈 응답·401/403/429/503·시간 초과·비정상 응답·캐시·연결 해제 |
| `--weather` | 공급자 전환, 키 보존, 신규/구버전 기본값, 요청 공유, 저장 실패, 정상·강제 종료 복원, 레이아웃 가져오기, 오류 응답·WMO 상태·늦은 응답, 위젯 출처 문구 제거 |
| `--workspace-editor` | v1 프로필 자동 이전과 재실행, 통합 편집·미리보기·되돌리기, 미적용 변경 3가지 선택, 복제, 배치 전환·잠긴 파일·재시작, 단일 배치 가져오기·내보내기, 좁은 창과 테마 예제 캡처. 실제 Win32 입력·파일, 외부 API 목업 |
| `--ui-review` | 9종 선택별 설정, 클릭 무동작·읽기 전용 접근성, 현재 위치 한 번 선택·저장, 전체 네트워크, 37개 크기, 전체 디스크·자동 페이지 순환 |
| `--startup` | 격리된 레지스트리 등록·해제, 재시작 체크 복원, 공백·한글 데이터 경로, 등록 명령 실행 |
| `--persistence` | 위젯 ID·설정·실제 창·숨김·중간 삭제·종료 후 복원 |
| `--edit-mode` | 정렬 12회 전환, 실제 화면 입력 대상·위젯 픽셀, 항상 위 복원·재시작. `--saved-layout`과 함께 쓰면 원본의 복사본으로 검사 |
| `--isolation` | 사용자 프로파일 접근 거부, 독립 테스트 창·단일 실행 식별자, 중복 실행 대상 확인 |
| `--options` / `--failures` | 위젯별 옵션·테마·확대 / 파일 잠금 저장 실패·입력·백업 복구 |
| `--capacity` / `--placement` | 64개 한도·빈 레이아웃 / 모니터 임시 배치·공간 부족 |
| `--matrix` / `--clock-layout` | 지원 크기·배율 / 시계 16개 표시 조합 |
| `--behavior` / `--interaction` | 표시·공유 수집·트레이 / Win32 드래그 메시지·잠금 |
| `--design` / `--visual` | 실제 창 캡처·상태별 배치 / 시각·바탕화면 검사 |
| `--stress` | 준비 100회·갱신 5회·5분, 1,000회 변경, 전후 5분 구간의 중앙·최대 자원 수 비교. 최소값·끝점·앱 스레드·시작 모듈·숨은 창도 기록 |
| `--geometry-matrix` | 9종·37개 지원 크기 × 100/150/200%, 실제 창 크기·격자·화면 초과 거부·종류별 재시작 |
| `--render-cache` | 실제 픽셀로 9개 테마·투명도·배율·재시작을 비교. 디자인 예제로 고정 제목·아이콘과 동적 사용률·색·모델명·메모리 수치의 캐시 갱신 확인 |
| `--legacy-grid` | 기존 4 DIP Slim 150% 배치 가져오기, 크기 보정 후 가까운 칸으로 복원, 적용 전후 재시작과 저장 안정성 |
| `--drag-performance` | 위젯 1개/64개, 준비 100회와 측정 1,000회 드래그, 이동·저장·왕복 p95, 위치 누적 오차·무변경 저장 방지 |
| `--shell-actions` | 트레이 명령, 시스템 메뉴 닫기, 편집 해제, 숨김/복원, 새로고침, 격리된 데이터 폴더 열기·소유 창 정리 |
| `--soak` | 실제 2시간 동안 종류별 추가·설정·드래그·닫기·숨김·삭제·주기적 프로세스 재시작, 메모리·핸들·GDI/USER 추적 |
| `--live-smoke` | 실제 CPU·GPU·메모리 수집. 외부 API는 목업 유지 |
| `--saved-layout <파일>` | 지정 JSON을 복사하여 적용·추가·정렬·재시작 검사 |
| `--smoke` | TestHost 초기 실행·지역표·종료 |

`--exe`로 다른 빌드의 `Tempos.TestHost.exe`를 지정할 수 있다. 기본 E2E에 `Tempos.exe`를 주면 프로세스를 실행하기 전에 거부한다. 시작·재시작마다 진단의 API 모드도 확인한다. `--weather`, `--api-mock`, `--ui-review`, `--design`, `--startup`은 목업 전용이다.

`--windows-real`은 TestHost의 로컬 수집만 실제로 전환한다. 날씨는 기본 지역에 대한 목업 21°C, 현재 위치는 부산 목업 좌표를 사용한다. OS `SendInput`은 실제 포인터 이동과 저장 위치 변경까지 확인하며, 입력 데스크톱이 이를 허용하지 않으면 SKIP으로 남긴다. 메시지 기반 `--interaction`의 통과로 대체하지 않는다.

`--temperature`의 센서 값은 `sensor-scenario.json`의 직렬화된 응답을 실제 온도 파서에 주입한다. 설치 결과는 `sensor-setup-scenario.json`으로 모의하며 TestHost는 실제 센서 설치 프로그램을 실행하지 않는다. 별도 센서 단위 검사는 가짜 수집기와 실제 로컬 이름 있는 파이프를 사용한다. `--windows-real`은 기존 서비스/Windows 드라이버의 실제 온도만 검사하고 없으면 SKIP으로 기록한다. 센서 서비스·PawnIO 설치/제거는 기존 사용자 앱이 있는 이 작업 환경에서 실행하지 않는다.

실제 API 요청은 사용자가 별도로 요청할 때 **실연동 검사**로 실행한다. `--integration-live-api`와 명시적인 배포 `Tempos.exe` 경로가 모두 필요하다. 결과의 `testClass`는 `live-api-integration`이며 일반 E2E와 합산하지 않는다. 이 옵션 자체를 사용자 승인으로 취급하지 않는다.

```powershell
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release --no-build -- . --windows-real --exe out/release/Tempos.TestHost.exe
```

실연동에서 현재 위치까지 검사하려면 Windows 위치 조회·선택 지역 좌표 전송에 대한 사용자 승인 후에만 `--allow-location-to-open-meteo`를 추가한다. 일반 E2E에서는 이 옵션을 거부한다. 전역 위치 권한을 켜거나 실제 계정을 자동 연결하지 않는다.

## 전체 회귀 실행

`--interaction`, `--drag-performance`, `--soak`의 합성 드래그는 TestHost 전용 `WM_COPYDATA` 배치로 실제 위젯의 DOWN/MOVE/UP 처리기를 한 UI 작업에서 실행한다. 캡처 사이에 실제 포인터 메시지가 끼어드는 자동화 경쟁을 방지하며, 결과 좌표를 주입하지 않는다. 대상 HWND는 해당 TestHost 소유인지 양쪽에서 확인한다. 이 제어 경로는 배포 앱에 포함하지 않는다. 성능 결과는 내부 이동·드롭 시간과 프로세스 간 왕복 시간을 구분하며 실제 마우스 입력 지연으로 표현하지 않는다.

전체 회귀 검사는 다음처럼 순차 실행한다. 첫 실패에서 멈추고 기존 결과를 보존하며, 모드별 원본 경로·통과·실패·SKIP·실행 파일 해시를 `full-validation-*/manifest.json`에 기록한다. `-LongRunning`은 1,000회 스트레스와 실제 2시간 지속 검사를 추가한다. 설치·제거와 실연동 API는 이 목록에 포함하지 않는다.

```powershell
./scripts/full-validation.ps1
./scripts/full-validation.ps1 -LongRunning
./scripts/performance.ps1 -WarmupSeconds 300 -SampleSeconds 1800 -Name regression
# 설정창을 닫은 상태에서 미리보기 그리기 횟수와 위젯 예산을 함께 검사
./scripts/performance.ps1 -WarmupSeconds 30 -SampleSeconds 60 -Name editor-closed -SettingsClosed
```

성능·2시간 지속 검사는 해당 실행 스레드에만 일시적인 절전 방지 요청을 두며 종료 시 해제한다. 전원 계획·화면 잠금 정책을 변경하지 않는다. 입력 데스크톱이 잠기거나 샘플 사이에 비정상적으로 긴 중단이 생기면 실패 처리하여 절전 시간을 실제 UI 검증 시간으로 합산하지 않는다. 성능 보고서에는 실제 누적 측정 시간과 최대 샘플 간격을 기록한다.

정식 성능 측정 구간에는 다른 빌드·프로파일링·UI 테스트를 함께 실행하지 않는다. 원인 분석용 계측 빌드는 별도 실행 파일로 구분하고 정식 통과 수에 넣지 않는다. 중단하거나 다른 검사와 겹친 실행의 원본은 보존하되 완료된 성능 결과로 사용하지 않는다.

### 개인용 Windows 2대의 성능 기준 · 2026-11-14

적용 정책은 `personal-two-windows-v1`이다. 지원 대상은 현재 PC와 다른 Windows PC **총 2대**이며, 두 PC를 각각 검사한다. 현재 PC는 Intel Core i7-8700B / 논리 프로세서 12개, 다른 PC는 AMD Ryzen 7 9700X / 논리 프로세서 16개 / 메모리 약 32GB다. 다른 PC는 기존 `windows-test` SSH 연결로 확인했으며, 사용자 전송·실행 승인 후 동일 TestHost를 임시 폴더에 복사해 로그인된 데스크톱에서 검사했다. 이 정책은 제품 운영 예산이지 Windows의 보편적인 정상/불량 경계가 아니다. 새 기준을 등록했다고 두 PC의 검증이 완료되는 것은 아니다.

| 항목 | 기본 M 위젯 5개 | 전체 M 위젯 9개 |
| --- | --- | --- |
| 전체 논리 프로세서 기준 평균 CPU | ≤ 0.1% | ≤ 0.2% |
| 1초 표본 CPU p95 | ≤ 0.3% | ≤ 0.6% |
| 평균 CPU 시간 | ≤ 10 ms/초 | ≤ 20 ms/초 |
| 최대 Private Bytes | ≤ 32 MiB | ≤ 48 MiB |
| 최대 Working Set | ≤ 48 MiB | ≤ 72 MiB |
| CPU 절약 목표, 필수 통과와 별도 | ≤ 4 ms/초 | 별도 목표 미지정 |

CPU 시간 10 ms/초는 단일 코어 시간의 1%, 20 ms/초는 2%에 해당한다. 전체 CPU 비율과 CPU 시간 상한을 **동시에** 적용하여 코어 수가 많다는 이유로 큰 CPU 비용이 가려지지 않도록 한다. 9개 구성에는 더 큰 작업량을 위한 예산을 배정했다. 이 수치들은 지원 범위에 맞춰 선택한 초기 운영 정책이며, 정확히 이 지점에서 버벅임이나 배터리 문제가 시작된다는 뜻은 아니다. 기존 메모리 상한은 유지한다. 4 ms/초 초과는 `optimizationTargetsMet=false`로 남기되 그것만으로 필수 예산을 실패시키지 않는다.

기능과 반응성은 별도의 필수 검사다. 격자 정렬·오른쪽 경계 드래그·설정 닫기·위젯 안팎 조작·저장·재시작이 정상이어야 한다. 기존 `--drag-performance`의 **내부 이동 p95 ≤16.7ms, 저장을 포함한 드롭/합성 배치 왕복 p95 ≤100ms**를 유지한다. 이 수치를 실제 마우스 입력부터 화면 표시까지의 지연으로 해석하지 않는다. `--windows-real`의 실제 입력 확인, 반복 자원 검사와 2시간 지속 검사를 함께 사용한다. 숨김·세션 잠금의 수집/그리기 중단도 검사한다. 전력·발열·배터리 영향은 앱 CPU만으로 통과 판정하지 않으며 별도 전력 측정 전까지 미검증이다.

각 PC에서 동일 실행 파일·위젯 종류/크기·갱신 주기·배율·모니터 구성·전원 조건을 기록한다. 원격 접속을 사용하면 원격/로컬 세션을 구분한다. 다른 PC의 결과와 합산 평균을 내지 않는다. 초기 비교 기준은 PC별로 5분 준비 + 3분 측정을 3회 수집해 중앙값과 범위를 보관하며, 새 버전이 기존 범위를 벗어나면 같은 조건으로 재측정해 원인을 검토한다. 두 PC의 실제 편차가 확보되기 전에는 임의의 상대 악화율을 필수 실패 기준으로 추가하지 않는다. 최종 성능 확인은 각 PC에서 **5분 준비 + 실제 30분**을 완료하고 필수 예산을 만족해야 한다.

`report.json`의 `policyVersion`, `device`, `checks`, `optimizationTargets`, `standardDuration`, `devicePerformanceQualified`를 함께 읽는다. `passed`는 해당 실행의 필수 예산/표시 검사 결과다. 짧은 검사나 예산 없는 `None` 프로필은 `devicePerformanceQualified=false`이며 전체 제품 또는 두 PC의 합격을 뜻하지 않는다. 기존 정책의 실패 기록과 중단 기록은 그대로 보존한다. 과거 4 ms/초 초과 결과를 새 정책의 통과 결과로 덮어쓰지 않는다.

측정 로그는 읽기 공유를 허용한 단일 writer로 저장하고 매 표본을 flush한다. 중간 확인은 콘솔 진행률을 우선 사용한다. 로그 저장 오류로 중단된 실행은 측정 길이를 채운 검사로 인정하지 않는다. 정책·로그 공유 회귀 검사는 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/performance_policy_tests.ps1`로 실행한다.

정책 적용 시 경계값·잘못된 수치·목표 분리·로그 공유 25개 검사와 1,000개 표본의 동시 저장/읽기를 통과했다. 현재 PC에서 격리 TestHost로 5.087초 연결 검사를 실행해 새 정책, 필수 상한, 별도 절약 목표, 장치 정보가 보고서에 기록되는 것을 확인했다(`test-results/performance-policy-v1-smoke-20261114-132957/report.json`). 이는 측정 도구 연결 검사이며 `standardDuration=false`, `devicePerformanceQualified=false`다. 새 정책으로 두 PC의 장시간 검증을 완료했다는 의미가 아니다. 앱 실행 파일과 설치본은 변경하지 않았다.

기준을 반응 시간·화면 부드러움·자원 효율로 구분하는 근거는 [Microsoft 성능 목표 지침](https://learn.microsoft.com/en-us/windows/apps/develop/performance/planning-measuring-performance), 숨김 상태의 CPU 깨움과 전력을 별도로 확인하는 근거는 [백그라운드 전력 지침](https://learn.microsoft.com/en-us/windows/apps/develop/performance/power)이다. 위 표의 수치는 이 프로젝트에서 선택한 값이다.

#### 두 PC의 기본 5개 위젯 실측 · 2026-11-14

각 PC에서 300초 준비 후 실제 30분을 완료했다. 같은 실행 파일 SHA-256 `2B4AEDCC7E5049CF9F13BBCF01162849EDE360577A2D39E9C65C170776C0019F`와 기본 M 위젯 5개를 사용했다. 외부 API는 HTTP 경계 목업, 로컬 Windows 수집과 창 표시는 실제다. 두 장치의 결과는 합산하지 않았다.

| 장치 | 실제 측정 초 | CPU ms/초 | 평균 / p95 CPU | 최대 Private / Working Set MiB | 필수 성능 기준 | 4 ms/초 절약 목표 |
| --- | --- | --- | --- | --- | --- | --- |
| 현재 PC · i7-8700B | 1800.960 | 4.980 | 0.0415% / 0.1301% | 18.70 / 43.20 | 통과 | 미달 |
| 다른 Windows · Ryzen 7 9700X | 1800.409 | 1.866 | 0.0117% / 0.0963% | 23.69 / 34.49 | 통과 | 충족 |

두 실행 모두 시작·종료 시 위젯 5개가 표시됐고 설치본 프로세스·원본 설정·백업·시작 등록 보호 검사를 통과했다. 원격 검사는 세션 1의 로그인된 데스크톱에서 실행했으며, 검사 후 TestHost 종료 및 테스트 전용 임시 예약 작업 제거를 확인했다. 설치본과 앱 실행 파일은 변경하지 않았다. 현재 PC의 디스플레이 DPI는 96, 다른 PC에는 DPI 144/120 디스플레이가 연결돼 있었다. 이는 해당 구성의 성능 측정이며 원격 PC의 모니터 간 이동·배율 전환 검사를 완료했다는 뜻은 아니다.

현재 PC에서는 성능 검사 종료 후 `--drag-performance`와 `--windows-real`도 실행했다. 74개 체크 통과·실패 0개·실제 CPU/GPU 온도 2개 SKIP이다. 위젯 1개 이동/드롭/배치 왕복 p95는 0.862/64.686/66.719ms, 64개는 0.308/2.051/2.693ms다. 실제 센서 값이 없는 온도 검사는 통과로 바꾸지 않았다.

[두 PC 결과와 원본 경로](validation/2026-11-14-personal-two-windows.json)에 원격 파일 해시·정리 기록과 한계를 보존했다. 이번 완료 범위는 **두 PC의 기본 5개 위젯 성능 및 현재 PC의 추가 반응성/Windows 검사**다. 다른 PC의 전체 기능 검사, 9개 구성의 두 PC 장시간 검사, 새 정책에서 PC별 3회 짧은 반복 기준선, 전력/배터리 검증은 이번 실행에 포함하지 않았다. 과거 실패·중단 결과는 수정하지 않았다.

기능별 검사 연결은 [기능 추적표](validation/feature-matrix.md), 이번 변경의 실행 증거는 [격자 회귀 검증](validation/2026-11-14-grid-regression.md)에 기록한다.

## 데이터와 프로세스 수명

### About·업데이트

`--updates`는 한글·공백을 포함한 실행별 데이터 폴더에서 About의 버전·링크·자동 확인 설정, 같은/이전/새 버전, 누락·위조된 설치 주소/해시, 요청 제한·오류, 중복 요청, 취소·종료 중 정리를 검사한다. GitHub 릴리즈 메타데이터와 설치 파일/CDN 응답도 HTTP 경계 목업이다. 실제 버전 파서·파일 저장·SHA-256 검사·설정 저장을 통과하며, 외부 브라우저/메일 실행은 고정된 링크 대상을 기록하는 경계로 대체한다.

v0.1.6부터 `--updates`는 실행별 `installed` 폴더에 이전 TestHost를 복사하고 다른 버전 리소스를 가진 `Tempos.UpdateCandidate.exe`를 패치 ZIP으로 제공한다. 릴리즈 목록·메타데이터·서명·ZIP은 HTTP 목업에서 읽으며 고정된 시험용 Ed25519 키만 사용한다. 파일 교체·프로세스·UI 기동 확인·복구는 실제 Windows에서 진행한다. 사용자가 추가한 파일, 설정 보존, 중단 후 다음 실행의 복구, 새 버전의 추가·삭제 파일도 대조한다. TestHost 설치 등록은 실행 폴더의 시험 파일을 사용한다. 실제 HKCU 등록과 제거 프로그램은 기존 설치가 없는 Windows 사용자 환경에서 별도 검사한다.

개발 소스의 업데이트 확인은 [GitHub 정식 최신 릴리즈 API](https://docs.github.com/en/rest/releases/releases#get-the-latest-release)의 공개 메타데이터와 SHA-256 digest를 사용한다. 테스트는 계정 토큰 없이 목업으로 실행한다. 다운로드 리디렉션은 HTTPS의 공식 저장소와 GitHub release-assets 호스트로 제한하고, 정확한 크기·SHA-256 검증 뒤에만 설치를 허용한다. 검사 후 파일 변조를 막도록 도우미에서도 다시 검증하고 설치 종료까지 쓰기·교체를 막는 읽기 핸들을 유지한다. 실제 GitHub 다운로드/설치 검사는 별도 실연동·설치 범위이며 기본 검사에 섞지 않는다.

```powershell
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release --no-build -- . --updates
```

실행마다 `test-results/e2e-<시각>/`을 만들고 `--data-dir`로 전달한다. 실제 사용자 설정은 초기화하지 않는다. `--saved-layout`도 원본을 복사한다. 종료·재시작 사례는 실제 PID 종료를 기다리고 새 앱을 실행한다. 강제 종료 검사는 테스트가 소유한 PID만 종료한다.

v0.1.1부터 TestHost 자체도 별도 `--data-dir`이 없거나 `%LocalAppData%\Tempos` 및 하위 경로를 지정하면 종료 코드 64로 거부한다. 경로는 정규화하고 대소문자를 통일해 비교한다. 테스트 관리 창 클래스와 단일 실행 식별자는 프로파일별로 분리하며 중복 실행은 같은 프로파일의 창만 표시한다. 일반 E2E는 진단에 기록된 데이터 경로가 자신의 실행 폴더인지 검사한다.

매 E2E 실행 전후에 설치 레이아웃·백업 파일의 SHA-256과 사용자 자동 시작 값을 비교한다. 달라지면 실패로 기록하며 자동으로 복구하거나 사용자 변경을 덮어쓰지 않는다. 실행 도중 사용자가 직접 구성을 변경해도 이 비교는 실패할 수 있다. 설치된 앱은 종료하거나 다시 설치하지 않으며 설치·제거 검사는 기존 설치가 있으면 사전 조건에서 중단한다.

TestHost의 자동 시작은 `HKCU\Software\Tempos\Tests\<데이터 경로 식별자>`에 격리한다. E2E 종료 시 자신이 만든 키를 정리한다. 실제 Windows Run 등록이나 사용자 시작 앱 설정을 테스트용으로 바꾸지 않는다. 등록 명령 실행을 실제 Windows 로그인 성공으로 기록하지 않는다.

날씨 목업은 `weather-scenario.json`, OAuth·캘린더 목업은 `api-scenario.json`, 위치 목업은 `location-scenario.json`을 사용한다. HTTP 계약과 브라우저 콜백 목업은 `src/api_fixture.h`에 있으며 TestHost·핵심 검사에만 포함한다. 요청 기록은 `api-mock-requests.jsonl`에 경로·시나리오·매칭 여부만 남기고 쿼리 좌표·키·토큰은 기록하지 않는다. 배포 실행 파일은 목업을 포함하지 않고 원래 외부 API 동작을 유지한다.

## 결과 판정

<a id="installer"></a>
### 설치 파일 수명주기 검사

`--installer --exe artifacts/Tempos-0.1.0-win-x64.exe`는 배포 설치 EXE를 실제 Windows에서 검사하는 별도 모드다. 결과는 `testClass=installer-lifecycle`, `apiMode=not-exercised`, `systemMode=clock-only`로 기록하고 일반 API 목업 E2E 통과 수와 합산하지 않는다.

기존 설치 등록·바로가기·실행 중인 Tempos가 있으면 시작 전에 중단한다. 한글과 공백이 있는 작업 폴더에 설치하고 모든 설치 파일 해시, HKCU의 설치된 앱 등록, 시작 메뉴·바탕화면 바로가기, 실행 중 덮어쓰기 방지, 같은 폴더 재설치, 제거 후 잔여 파일·등록 정리를 확인한다. 제거는 파일 목록에 있는 항목과 비어 있는 디렉터리만 삭제하며 관련 없는 파일은 남긴다.

실제 배포 앱의 실행 확인에는 별도 `--data-dir`과 시계 하나만 있는 설정을 사용한다. 날씨·캘린더·OAuth·위치 요청과 실연동 검사는 실행하지 않는다. 번들 지역표를 저장소 밖 작업 경로에서도 찾는지 확인한다. 기본 앱 프로파일은 읽거나 복사하지 않고 설정 파일의 전후 해시만 비교한다. 설치 완료 화면의 자동 실행은 테스트에서 해제한다.

설치본은 실제 현재 사용자 등록을 검사해야 하므로 이 모드에 한해 HKCU의 설치 항목·바로가기를 생성한 뒤 제거한다. 시작 등록의 제거 조건을 검사할 때 기존 `Run\\Tempos` 값·타입을 보관하고 `finally`에서 복구한다. 이 절차는 TestHost의 격리된 자동 시작 E2E와 구분하며 실제 Windows 재로그인 검사를 대신하지 않는다. 실패 시 소유한 PID와 설치 폴더만 정리한다.

검사 창 캡처에는 개인정보 없는 예시 설치 경로를 표시한 후 실제 시험 경로를 복원해 설치한다. README는 검토한 설치 화면을 사용한다. 설치 EXE를 다시 만들면 해시가 달라지므로 최종 배포 파일로 전체 수명주기를 다시 실행한다.

각 실행의 `results.json`에서 시작·종료·모드·`testClass`·`apiMode`·`systemMode`·실행 파일 SHA-256·각 항목의 결과를 확인한다. 실패·중단·보고서 누락은 통과가 아니다. 캡처 생성과 사람이 검토한 결과도 별도로 기록한다.

디자인 검수용 위젯은 항상 위·배경 불투명 상태로 촬영하며, 뒤에 중립색 창을 배치한다. 다른 앱이나 시스템 대화상자가 촬영 영역을 가리면 설정창은 Windows의 `PrintWindow`로, 위젯은 해당 TestHost 창이 마지막으로 표시한 색상·투명도 표면으로 촬영한다. 외부 창이 가로막은 마우스 적중 검사는 SKIP으로 기록한다. 설정창은 촬영할 때만 맨 앞으로 올리고 49개 내부 지점의 가림 여부를 검사한 뒤 클라이언트 영역만 캡처한다. 설정과 실제 배경 투명도 기본값을 바꾼 것이 아니라 촬영 조건이다. `scripts/collect-design-review.cjs`는 `artifacts/final-runs.json`의 `[{"mode":"ui-review","folder":"test-results/e2e-..."}, ...]` 목록에서 통과한 실행의 지정 PNG만 수집하며 프로파일·인증 정보는 복사하지 않는다.

일반 E2E는 실제 기상청 인증이나 Open-Meteo 서버의 가용성을 증명하지 않는다. 실제 Open-Meteo는 `tempos_tests.exe --live-weather`, 실제 기상청 키·Google OAuth·Windows 로그인은 별도 실환경 검사로 구분한다. 물리 드래그와 Win+D를 Win32 메시지 테스트로 대신했다고 표시하지 않는다.

배포 ZIP으로 검사한 [2026-10-13 이전 실연동 결과](validation/windows-2026-10-13/README.md)는 이 기준을 정하기 전 기록이다. 그날의 실제 API 요청·SKIP을 목업 결과로 바꿔 쓰지 않는다. 당시 `scripts/collect-windows-e2e.cjs`와 결과는 해당 실행을 재현하는 역사 자료로 유지한다.

현재 기준의 [목업 검사 결과](validation/2026-10-13-api-mock.json)는 `--api-mock`, `--weather`, `--windows-real`을 순차 실행한 기록이다. `scripts/collect-api-mock-e2e.cjs`는 최종 실행 파일 해시와 API 모드·요청 매칭을 검증하고 최초 실패를 보존한다.
