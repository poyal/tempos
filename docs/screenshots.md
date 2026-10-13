# 실제 화면과 촬영 조건

README의 예제 이미지는 네이티브 TestHost를 실행해 **앱 창 또는 위젯 영역만** 캡처한 화면입니다. 개인 계정·키·일정은 사용하지 않았습니다. 예제의 Open-Meteo·기상청 값은 재현 가능한 응답입니다. 별도로 추가한 실제 Windows 측정 화면은 아래 촬영 조건과 [실행 결과](validation/windows-2026-10-13/README.md)로 구분합니다.

<a id="release-010"></a>
## v0.1.0 · 위젯별 크기와 사용법

현재 README에는 **2026-10-13 Release TestHost**로 새로 촬영한 화면을 사용합니다. 아래 과거 촬영 기록은 날짜별 근거로 보존하며 현재 배포 후보의 검사로 합산하지 않습니다.

| 위젯 | S · Slim · M · L 소개 이미지 |
| --- | --- |
| 시계 | [네 가지 크기](assets/v0.1.0/clock-sizes.png) |
| 날씨 | [네 가지 크기](assets/v0.1.0/weather-sizes.png) |
| CPU | [네 가지 크기](assets/v0.1.0/cpu-sizes.png) |
| GPU | [네 가지 크기](assets/v0.1.0/gpu-sizes.png) |
| 메모리 | [네 가지 크기](assets/v0.1.0/memory-sizes.png) |
| 시스템 정보 | [네 가지 크기](assets/v0.1.0/system-sizes.png) |
| 네트워크 | [네 가지 크기](assets/v0.1.0/network-sizes.png) |
| 디스크 | [네 가지 크기](assets/v0.1.0/disk-sizes.png) |
| 캘린더 | [네 가지 크기](assets/v0.1.0/calendar-sizes.png) · [XL](assets/v0.1.0/calendar-xl.png) |

소개 보드는 원본 위젯 캡처의 비율을 유지하고 바깥에 제목·크기만 추가했습니다. 위젯 내부 글자·아이콘·배경을 다시 그리지 않았습니다. [37개 개별 원본과 오프라인 갤러리](assets/v0.1.0/gallery.html)는 ZIP을 풀고 브라우저에서 열 수 있습니다. [전체 예시](assets/v0.1.0/overview.png)도 M 크기 캡처 9장을 합성한 화면입니다.

사용법은 `usage-*.png` 17장으로 처음 실행, 추가·적용, 정렬 모드, 시계·시스템 표시 옵션, 네트워크, 날씨 지역과 위치 한 번 찾기, 두 날씨 공급자, 캘린더 선택·월 이동, 자동 시작, 백업·복원, 진단 저장을 촬영했습니다. Google 연결은 목업 계정으로 앱 내부의 승인 콜백·목록 선택을 실행한 화면이며 Google 웹 승인 화면을 촬영한 것이 아닙니다. 자동 시작도 테스트 전용 레지스트리에서만 변경했습니다.

- Windows 11 x64, 144 DPI(150%), 위젯 확대 100%, 공통 블루 테마. 확정된 날씨 자동 배경은 그대로 사용합니다.
- 위젯 소개 캡처는 배경 투명도 0%, 중립 배경 `#edf4fa`입니다. 설정 화면의 신규 위젯 기본값은 배경 투명도 10%입니다.
- 시계·달력 날짜는 촬영 시점입니다. 날씨·장치명·용량·그래프·일정은 예제이며 크기별 독립 캡처의 값은 다를 수 있습니다.
- 설정창을 늘려 상태 메시지까지 포함한 내부 영역만 촬영했습니다. 개인 바탕화면·계정·인증키·좌표·PC 경로를 공개 이미지에 넣지 않았습니다.
- 원본 경로·시각·TestHost 해시·PNG 해시·합성 입력은 [captures.json](assets/v0.1.0/captures.json)에 있습니다. Release/ASan/E2E는 [릴리즈 검증 기록](validation/release-v0.1.0.md)을 참조하세요.

재현 순서: 현재 Release TestHost로 `--ui-review`와 `--readme` 실행 → 두 결과를 포함한 실행 목록 JSON 작성 → `node scripts/release-screenshots.cjs <실행 목록.json>` → 모든 보드·설정 화면 육안 확인. 합성 스크립트는 Chrome에서 이미지 로딩과 1200/390px 가로 넘침도 검사합니다. [E2E 실행 방법](e2e.md).

## 실제 Windows 배포 앱 · 2026-10-13

[시계·날씨·CPU·GPU·메모리·전체 네트워크·캘린더 7종](validation/windows-2026-10-13/README.md)은 수정한 배포 ZIP을 새 폴더에 풀고 **Tempos.exe**를 실행한 화면입니다. TestHost나 예제 공급자를 사용하지 않았습니다. 당시 메모리와 네트워크 이미지를 README에 추가했으며 지금은 이 과거 검증 페이지에 보존합니다.

- Windows 11 Pro 빌드 26300, 촬영 모니터 144 DPI(150%), 위젯 확대 100%, 배경 투명도 0%, 기본 파란 테마입니다.
- 시계·시스템 사용량·네트워크·로컬 달력은 실제 값입니다. 메모리 그래프는 값이 거의 일정해 수평선에 가깝습니다.
- 날씨는 초기 기본 지역인 서울 종로구의 실제 Open-Meteo 응답입니다. 현재 위치 조회를 실행한 화면이 아닙니다. 확정 날씨 디자인은 변경하지 않았습니다.
- 캘린더는 Google 계정을 연결하지 않은 로컬 달력입니다. 시스템 정보와 디스크는 검사했지만 PC 이름·볼륨명이 보이는 화면은 공개하지 않았습니다.
- 캡처할 때 위젯을 앞으로 올리고 단색 배경을 두었습니다. 바탕화면 전체, 개인 프로파일, 실제 위치 좌표는 공개 자료에 넣지 않았습니다.

원본은 `test-results/e2e-20261013-082905-622/`이며 08:29:05–08:30:13 KST에 실행했습니다. 실행 파일·PNG 해시는 [실측 결과 JSON](validation/windows-2026-10-13/results.json)에 있습니다. 수정 전 메모리 화면도 결함 증거로 보존했습니다. 같은 조건으로 촬영하려면 [E2E 안내](e2e.md)의 `--windows-real --integration-live-api --exe <Tempos.exe>` (실제 API 사용을 별도 요청한 경우에만)를 실행합니다.

## 날씨 공급자 선택 화면 · 2026-10-12

| 화면 | 조건 |
| --- | --- |
| [Open-Meteo 선택](assets/weather-connections-open-meteo.png) | 신규 기본값, 키 입력 비활성, 무료 이용·요청 간격 안내 |
| [기상청 선택](assets/weather-connections-kma.png) | 키 입력 활성, 테스트 키 저장 후 입력란을 비운 상태 |
| [S](assets/weather-size-0.png) / [Slim](assets/weather-size-1.png) / [M](assets/weather-size-2.png) / [L](assets/weather-size-3.png) | Open-Meteo 예제, 위젯 확대 100%, 배경 투명도 0%의 검수용 화면 |

촬영은 `--weather` E2E가 수행합니다. 이번 원본은 `test-results/e2e-20261012-005307-108/`이며 2026-10-12 00:53–00:55 KST에 실행했습니다. Windows 배율은 150% (144 DPI)이며 실행 파일과 각 이미지 SHA-256은 [검증 요약](validation/2026-10-12-weather.json)에 기록합니다. 기본 배경 투명도는 10%이며 촬영 조건과 구분합니다.

## 재촬영

1. 관련 UI 변경을 빌드하고 격리된 TestHost에서 `--weather`를 실행합니다.
2. 실행 폴더의 `weather-connections-*.png`, `weather-size-*.png`를 직접 열어 출처·글자·배치를 확인합니다.
3. 검토한 앱 영역 이미지만 `docs/assets/`에 복사합니다. 바탕화면 전체 캡처를 공개 문서에 넣지 않습니다.
4. README의 해당 절, 이 문서의 촬영 조건과 검증 기록을 갱신합니다.

다른 위젯과 테마의 과거 검수 화면은 [2026-10-08 디자인 검수](validation/design-2026-10-08/README.md)에 보존합니다. 당시의 연동 화면은 현재 날씨 공급자 선택 UI를 포함하지 않습니다. `design/`의 초기 시안은 실행 화면으로 사용하지 않습니다.

## 공통 디자인 반영 화면 · 2026-10-13

당시 README의 [전체 위젯 예시](assets/widgets-overview.png)는 실제 앱의 M 크기 캡처 9장을 동일 배율로 배치한 이미지입니다. 디자인 시안이나 한 번에 배치한 바탕화면 전체를 촬영한 이미지가 아닙니다. TestHost는 배포 앱과 같은 Win32/Direct2D 렌더러를 사용하고 예제 공급자만 다릅니다.

| 화면 | 설명 |
| --- | --- |
| [시계](assets/final-clock.png) | 아날로그 없는 디지털 구성. 별도 검수에서 12/24시간·초 표시 16개 조합 확인 |
| [메모리 L](assets/final-memory.png) | CPU/GPU와 같은 사용률·60초 그래프·평균/최대 |
| [디스크 M](assets/final-disk-single.png) / [디스크 L](assets/final-disk-all.png) | 선택 볼륨과 전체 드라이브 목록의 차이 |
| [시스템 정보](assets/final-system.png) | 긴 PC·장치명과 사양 목록 |
| [시계 설정](assets/final-settings-clock.png) / [날씨 설정](assets/final-settings-weather.png) / [네트워크 설정](assets/final-settings-network.png) | 선택한 위젯에 해당하는 입력 항목만 표시 |
| [맑은 낮](assets/final-weather-clear.png) / [비](assets/final-weather-rain.png) / [맑은 밤](assets/final-weather-night.png) | 확정된 현행 날씨 배경. 위치 선택은 부산 모의 좌표를 사용 |
| [캘린더 XL](assets/final-calendar-month.png) | 종일·시간 일정과 여러 날 일정. 실제 계정 정보 없음 |

Windows 배율 150%(144 DPI), 위젯 확대 100%, 배경 투명도 0%로 촬영했습니다. 기본 배경 투명도는 10%입니다. 둥근 모서리와 투명한 부분 뒤에는 촬영용 단색 창을 두어 개인 바탕화면이 포함되지 않게 했습니다. 설정창은 촬영할 때만 맨 앞으로 올리고, 투명한 창 테두리가 들어가지 않도록 내부 영역만 캡처했습니다. 값·날짜·지역은 예제 데이터이며 실측 증거가 아닙니다.

[전체 37개 크기와 추가 상태](../design/final-2026-10-12/app-review.html)는 내려받아 브라우저에서 열 수 있습니다. 원본 PNG와 실행 파일 해시는 [캡처 기록](../design/final-2026-10-12/captures.json), README에 사용한 파일 목록은 [화면 출처](assets/final-captures.json)에 기록했습니다.

재촬영은 `--ui-review`, `--clock-layout`, `--design` 검사 후 `scripts/collect-design-review.cjs`로 필요한 캡처만 수집하고 `scripts/readme-screenshots.ps1 -Results <ui-review 실행 폴더>`로 전체 예시를 만듭니다. 초기 가림 실패와 재촬영 내역은 검증 기록에 보존합니다.
