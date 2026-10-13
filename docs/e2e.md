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

README 화면 재촬영에는 `--ui-review`(위젯 37개 크기)와 `--readme`(설정·연동·백업의 실제 조작)를 사용한다. `--readme`는 예제 수집기와 목업 API만 허용하며 시작 등록은 테스트 레지스트리에서 수행한다. 원본은 실행별 `test-results`에 저장한다. 검토한 앱 영역만 `scripts/release-screenshots.cjs`로 수집하고 `scripts/verify-docs.cjs`로 문서 링크·크기별 개수·해시를 확인한다. [촬영 조건](screenshots.md#release-010).

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
| `--api-mock` | OAuth 승인 거부·교환·갱신, 캘린더 목록·페이지 합치기·빈 응답·401/403/429/503·시간 초과·비정상 응답·캐시·연결 해제 |
| `--weather` | 공급자 전환, 키 보존, 신규/구버전 기본값, 요청 공유, 저장 실패, 정상·강제 종료 복원, 레이아웃 가져오기, 오류 응답·WMO 상태·늦은 응답, 위젯 출처 문구 제거 |
| `--ui-review` | 9종 선택별 설정, 클릭 무동작·읽기 전용 접근성, 현재 위치 한 번 선택·저장, 전체 네트워크, 37개 크기, 전체 디스크·자동 페이지 순환 |
| `--startup` | 격리된 레지스트리 등록·해제, 재시작 체크 복원, 공백·한글 데이터 경로, 등록 명령 실행 |
| `--persistence` | 위젯 ID·설정·실제 창·숨김·중간 삭제·종료 후 복원 |
| `--options` / `--failures` | 위젯별 옵션·테마·확대 / 파일 잠금 저장 실패·입력·백업 복구 |
| `--capacity` / `--placement` | 64개 한도·빈 레이아웃 / 모니터 임시 배치·공간 부족 |
| `--matrix` / `--clock-layout` | 지원 크기·배율 / 시계 16개 표시 조합 |
| `--behavior` / `--interaction` | 표시·공유 수집·트레이 / Win32 드래그 메시지·잠금 |
| `--design` / `--visual` | 실제 창 캡처·상태별 배치 / 시각·바탕화면 검사 |
| `--stress` | 준비 100회·5분, 1,000회 변경, 5분 안정화 후 자원 변화 |
| `--live-smoke` | 실제 CPU·GPU·메모리 수집. 외부 API는 목업 유지 |
| `--saved-layout <파일>` | 지정 JSON을 복사하여 적용·추가·정렬·재시작 검사 |
| `--smoke` | TestHost 초기 실행·지역표·종료 |

`--exe`로 다른 빌드의 `Tempos.TestHost.exe`를 지정할 수 있다. 기본 E2E에 `Tempos.exe`를 주면 프로세스를 실행하기 전에 거부한다. 시작·재시작마다 진단의 API 모드도 확인한다. `--weather`, `--api-mock`, `--ui-review`, `--design`, `--startup`은 목업 전용이다.

`--windows-real`은 TestHost의 로컬 수집만 실제로 전환한다. 날씨는 기본 지역에 대한 목업 21°C, 현재 위치는 부산 목업 좌표를 사용한다. OS `SendInput`은 실제 포인터 이동과 저장 위치 변경까지 확인하며, 입력 데스크톱이 이를 허용하지 않으면 SKIP으로 남긴다. 메시지 기반 `--interaction`의 통과로 대체하지 않는다.

실제 API 요청은 사용자가 별도로 요청할 때 **실연동 검사**로 실행한다. `--integration-live-api`와 명시적인 배포 `Tempos.exe` 경로가 모두 필요하다. 결과의 `testClass`는 `live-api-integration`이며 일반 E2E와 합산하지 않는다. 이 옵션 자체를 사용자 승인으로 취급하지 않는다.

```powershell
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release --no-build -- . --windows-real --exe out/release/Tempos.TestHost.exe
```

실연동에서 현재 위치까지 검사하려면 Windows 위치 조회·선택 지역 좌표 전송에 대한 사용자 승인 후에만 `--allow-location-to-open-meteo`를 추가한다. 일반 E2E에서는 이 옵션을 거부한다. 전역 위치 권한을 켜거나 실제 계정을 자동 연결하지 않는다.

## 데이터와 프로세스 수명

실행마다 `test-results/e2e-<시각>/`을 만들고 `--data-dir`로 전달한다. 실제 사용자 설정은 초기화하지 않는다. `--saved-layout`도 원본을 복사한다. 종료·재시작 사례는 실제 PID 종료를 기다리고 새 앱을 실행한다. 강제 종료 검사는 테스트가 소유한 PID만 종료한다.

TestHost의 자동 시작은 `HKCU\Software\Tempos\Tests\<데이터 경로 식별자>`에 격리한다. E2E 종료 시 자신이 만든 키를 정리한다. 실제 Windows Run 등록이나 사용자 시작 앱 설정을 테스트용으로 바꾸지 않는다. 등록 명령 실행을 실제 Windows 로그인 성공으로 기록하지 않는다.

날씨 목업은 `weather-scenario.json`, OAuth·캘린더 목업은 `api-scenario.json`, 위치 목업은 `location-scenario.json`을 사용한다. HTTP 계약과 브라우저 콜백 목업은 `src/api_fixture.h`에 있으며 TestHost·핵심 검사에만 포함한다. 요청 기록은 `api-mock-requests.jsonl`에 경로·시나리오·매칭 여부만 남기고 쿼리 좌표·키·토큰은 기록하지 않는다. 배포 실행 파일은 목업을 포함하지 않고 원래 외부 API 동작을 유지한다.

## 결과 판정

각 실행의 `results.json`에서 시작·종료·모드·`testClass`·`apiMode`·`systemMode`·실행 파일 SHA-256·각 항목의 결과를 확인한다. 실패·중단·보고서 누락은 통과가 아니다. 캡처 생성과 사람이 검토한 결과도 별도로 기록한다.

디자인 검수용 위젯은 항상 위·배경 불투명 상태로 촬영하며, 뒤에 중립색 창을 배치한다. 설정창은 촬영할 때만 맨 앞으로 올리고 49개 내부 지점의 가림 여부를 검사한 뒤 클라이언트 영역만 캡처한다. 설정과 실제 배경 투명도 기본값을 바꾼 것이 아니라 촬영 조건이다. `scripts/collect-design-review.cjs`는 `artifacts/final-runs.json`의 `[{"mode":"ui-review","folder":"test-results/e2e-..."}, ...]` 목록에서 통과한 실행의 지정 PNG만 수집하며 프로파일·인증 정보는 복사하지 않는다.

일반 E2E는 실제 기상청 인증이나 Open-Meteo 서버의 가용성을 증명하지 않는다. 실제 Open-Meteo는 `tempos_tests.exe --live-weather`, 실제 기상청 키·Google OAuth·Windows 로그인은 별도 실환경 검사로 구분한다. 물리 드래그와 Win+D를 Win32 메시지 테스트로 대신했다고 표시하지 않는다.

배포 ZIP으로 검사한 [2026-10-13 이전 실연동 결과](validation/windows-2026-10-13/README.md)는 이 기준을 정하기 전 기록이다. 그날의 실제 API 요청·SKIP을 목업 결과로 바꿔 쓰지 않는다. 당시 `scripts/collect-windows-e2e.cjs`와 결과는 해당 실행을 재현하는 역사 자료로 유지한다.

현재 기준의 [목업 검사 결과](validation/2026-10-13-api-mock.json)는 `--api-mock`, `--weather`, `--windows-real`을 순차 실행한 기록이다. `scripts/collect-api-mock-e2e.cjs`는 최종 실행 파일 해시와 API 모드·요청 매칭을 검증하고 최초 실패를 보존한다.
