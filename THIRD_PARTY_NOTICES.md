# 외부 구성요소와 자료

## nlohmann/json 3.12.0

Copyright (c) 2013-2025 Niels Lohmann

MIT License. 원문 라이선스는 `third_party/nlohmann/LICENSE.MIT`입니다. 배포 ZIP에도 해당 라이선스를 포함합니다.

## 기상청 지역 격자

자료: 기상청. `동네예보지점좌표(위경도)_260701.xlsx` 공개 자료에서 지역명, 격자와 위경도를 추출했습니다. 원본 링크와 변환 도구는 `data/README.md`에 있습니다. 출처 표시 조건은 공공누리 제1유형을 따릅니다.

## Open-Meteo 날씨 자료

자료: [Open-Meteo](https://open-meteo.com/). 날씨 데이터는 [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/)에 따라 출처를 표시합니다. Tempos는 날씨 코드를 한국어 상태·아이콘으로 변환하고 선택한 단위로 표시합니다. Open-Meteo의 무료 API 이용은 [비상업적 이용 조건](https://open-meteo.com/en/terms)을 따르며 데이터 라이선스와 API 서비스 이용 조건을 구분합니다. 서버 구현을 앱에 포함하지 않으며 사용자 PC에서 선택한 지역 좌표로 API에 직접 접속합니다.

## 개발·테스트 도구

Windows 설치 EXE는 [NSIS](https://nsis.sourceforge.io/) Unicode 도구로 만듭니다. 설치·제거 엔진은 zlib/libpng 라이선스, LZMA 압축 모듈은 Common Public License 1.0을 따릅니다. 배포한 도구의 전체 고지는 설치 폴더의 `licenses/NSIS.txt`에 포함합니다. Tempos의 설치 그림은 기존 앱 아이콘과 자체 생성 배경을 사용합니다.

FlaUI.UIA3 5.0.0 (MIT), Microsoft .NET SDK 및 Windows/Visual Studio SDK는 개발과 테스트에 사용합니다. FlaUI는 배포하지 않습니다. 기본 Tempos.exe는 네이티브 앱이며, 선택 설치하는 센서 도우미에는 .NET 런타임을 포함합니다.

## 선택 설치하는 CPU 센서 도우미

Tempos.Sensors.Setup.exe에는 LibreHardwareMonitorLib 0.9.6(MPL-2.0), .NET 10.0.12 런타임 및 고정한 전이 의존성을 포함합니다. LibreHardwareMonitor 원본 소스는 [고정 커밋](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/tree/3d331e3370efb858411f19511373eff65a218701)에서 제공하며 라이브러리 소스를 수정하지 않았습니다.

서명된 PawnIO 2.2.0 공식 설치 파일도 포함합니다. 라이선스는 GPL-2.0-or-later이며 공식 [설치 배포본](https://github.com/namazso/PawnIO.Setup/releases/tag/2.2.0), [드라이버 소스](https://github.com/namazso/PawnIO/tree/5cdf470831fdfff3f7f1d06363ca6b230f3bf35a), [모듈 소스](https://github.com/namazso/PawnIO.Modules)를 참조하세요. 빌드 시 설치 파일의 SHA-256과 Authenticode 서명을 검증합니다.

고정 버전·출처·해시는 `third_party/sensors/dependencies.json`과 `sensors/Tempos.Sensors/packages.lock.json`에 있습니다. 센서 설치 폴더의 `licenses`에 원문 라이선스·외부 고지·패키지 메타데이터와 의존성 목록을 함께 배포합니다. 센서 제거는 공유 PawnIO 드라이버를 제거하지 않습니다.
