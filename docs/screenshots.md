# 실제 화면과 촬영 조건

README의 이미지는 네이티브 TestHost를 실행해 **앱 창 또는 위젯 영역만** 캡처한 화면입니다. 개인 계정·키·일정은 사용하지 않았습니다. Open-Meteo·기상청 값은 재현 가능한 예제 응답이며 실제 서버 연결 증거는 [검증 기록](verification.md)에서 따로 확인합니다.

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

README의 [전체 위젯 예시](assets/widgets-overview.png)는 실제 앱의 M 크기 캡처 9장을 동일 배율로 배치한 이미지입니다. 디자인 시안이나 한 번에 배치한 바탕화면 전체를 촬영한 이미지가 아닙니다. TestHost는 배포 앱과 같은 Win32/Direct2D 렌더러를 사용하고 예제 공급자만 다릅니다.

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
