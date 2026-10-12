# E2E 실행과 격리

Tempos E2E는 FlaUI.UIA3로 실제 Win32 앱을 조작하고 저장 JSON·프로세스·창 배치·접근성 출력을 대조한다. [검증 기록](verification.md)에는 실제 실행한 범위만 남긴다.

## 실행 준비

Windows 사용자 세션, .NET 10 SDK, Release 빌드가 필요하다. ASan 앱 검사에는 MSVC ASan 런타임 DLL 경로도 필요하다. 먼저 [개발 안내](development.md)에 따라 빌드한다.

```powershell
dotnet restore tests/e2e/Tempos.E2E.csproj --locked-mode
dotnet build tests/e2e/Tempos.E2E.csproj -c Release --no-restore
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release --no-build -- . --weather
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release --no-build -- . --startup
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release --no-build -- . --persistence
```

로컬 `.tools/dotnet`을 사용하는 경우 해당 `dotnet.exe`와 `DOTNET_ROOT`, `DOTNET_CLI_HOME`, `NUGET_PACKAGES`를 구성한다. 테스트 모드는 한 번에 하나를 지정하고 데스크톱 검사는 순차 실행한다.

현재 Tempos E2E는 창을 표시하고 바탕화면을 캡처한다. **숨김 E2E는 구현되어 있지 않다.** 작업 중인 화면과 겹치지 않는 사용자 세션에서 수행한다. Passport의 Electron 숨김 모드를 Tempos에도 구현된 것으로 설명하지 않는다.

## 검사 모드

| 옵션 | 확인하는 동작 |
| --- | --- |
| 없음 | 기본 9종 추가·설정·저장·재시작 |
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
| `--live-smoke` | 실제 CPU·GPU·메모리 수집 |
| `--saved-layout <파일>` | 지정 JSON을 복사하여 적용·추가·정렬·재시작 검사 |
| `--smoke --exe <경로>` | 지정한 배포 실행 파일의 초기 실행·지역표·종료 |

`--exe`로 `out/asan/Tempos.TestHost.exe`나 패키지 실행 파일을 지정할 수 있다. 공급자 응답 제어와 격리 레지스트리 진단이 필요한 `--weather`, `--startup`은 TestHost 전용이다.

## 데이터와 프로세스 수명

실행마다 `test-results/e2e-<시각>/`을 만들고 `--data-dir`로 전달한다. 실제 사용자 설정은 초기화하지 않는다. `--saved-layout`도 원본을 복사한다. 종료·재시작 사례는 실제 PID 종료를 기다리고 새 앱을 실행한다. 강제 종료 검사는 테스트가 소유한 PID만 종료한다.

TestHost의 자동 시작은 `HKCU\Software\Tempos\Tests\<데이터 경로 식별자>`에 격리한다. E2E 종료 시 자신이 만든 키를 정리한다. 실제 Windows Run 등록이나 사용자 시작 앱 설정을 테스트용으로 바꾸지 않는다. 등록 명령 실행을 실제 Windows 로그인 성공으로 기록하지 않는다.

날씨 fixture는 TestHost 전용 `weather-scenario.json`과 제어 명령을 사용한다. 두 서비스의 제어된 HTTP 응답을 실제 파서에 통과시킨다. 호출 기록에는 호스트·시각·시나리오만 남기며 인증키나 전체 요청 URL은 기록하지 않는다. 배포 실행 파일에는 fixture를 포함하지 않는다.

## 결과 판정

각 실행의 `results.json`에서 시작·종료·모드·실행 파일 SHA-256·각 항목의 결과를 확인한다. 실패·중단·보고서 누락은 통과가 아니다. 캡처 생성과 사람이 검토한 결과도 별도로 기록한다.

디자인 검수용 위젯은 항상 위·배경 불투명 상태로 촬영하며, 뒤에 중립색 창을 배치한다. 설정창은 촬영할 때만 맨 앞으로 올리고 49개 내부 지점의 가림 여부를 검사한 뒤 클라이언트 영역만 캡처한다. 설정과 실제 배경 투명도 기본값을 바꾼 것이 아니라 촬영 조건이다. `scripts/collect-design-review.cjs`는 `artifacts/final-runs.json`의 `[{"mode":"ui-review","folder":"test-results/e2e-..."}, ...]` 목록에서 통과한 실행의 지정 PNG만 수집하며 프로파일·인증 정보는 복사하지 않는다.

일반 E2E는 실제 기상청 인증이나 Open-Meteo 서버의 가용성을 증명하지 않는다. 실제 Open-Meteo는 `tempos_tests.exe --live-weather`, 실제 기상청 키·Google OAuth·Windows 로그인은 별도 실환경 검사로 구분한다. 물리 드래그와 Win+D를 Win32 메시지 테스트로 대신했다고 표시하지 않는다.
