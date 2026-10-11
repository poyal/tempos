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
