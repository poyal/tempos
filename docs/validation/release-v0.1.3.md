# v0.1.3 릴리즈 후보 검증 · 2026-11-14

최종 버전 리소스 0.1.3으로 Release·ASan 단위 검사와 Windows E2E를 다시 실행했다. [실행별 결과·해시·명령](release-v0.1.3.json), [릴리즈 노트](../releases/v0.1.3.md).

## 후보와 검사 결과

- 빌드 기준은 커밋 `fcc993cb028676c8ca834d0cbb4aad32af30c76c`와 당시 미커밋 변경이다. 최종 소스는 `v0.1.3` 태그로 기록한다. 테스트 후 앱을 다시 빌드하지 않고 `package.ps1 -SkipBuild`로 포장한다.
- 배포 앱 SHA-256: `CB885C4928EA30583C77D6DE363EC14806687EE6C70E147FB6BAD5CB04849391`.
- Release TestHost SHA-256: `132F64770D8F76BDFB2C02A6C437B23EB979D275EDB80CB335149444AD671CB5`.
- ASan TestHost SHA-256: `DA6EEB74EC4951078B0EBC06C4C3AC04D79B900D7C8B8CA99D7FC88ACA3C7E70`.
- Release·ASan 단위 각각 **9,575 PASS**, 센서 **22 PASS**, 성능 정책·로그 공유 **25 PASS**(동시 읽기/쓰기 1,000표본).
- Release E2E **3977 PASS, 0 FAIL, 53 SKIP** · 24개 모드.
- ASan E2E **420 PASS, 0 FAIL, 0 SKIP** · legacy-grid, edit-mode, interaction, render-cache.

| Release 모드 | PASS | FAIL | SKIP |
| --- | ---: | ---: | ---: |
| edit-mode | 279 | 0 | 0 |
| interaction | 22 | 0 | 0 |
| drag-performance | 22 | 0 | 0 |
| geometry-matrix | 711 | 0 | 0 |
| render-cache | 91 | 0 | 0 |
| legacy-grid | 28 | 0 | 0 |
| shell-actions | 16 | 0 | 0 |
| persistence | 281 | 0 | 48 |
| options | 86 | 0 | 0 |
| failures | 140 | 0 | 0 |
| capacity | 84 | 0 | 0 |
| placement | 60 | 0 | 0 |
| api-mock | 68 | 0 | 0 |
| weather | 74 | 0 | 0 |
| startup | 25 | 0 | 1 |
| isolation | 11 | 0 | 0 |
| clock-layout | 118 | 0 | 0 |
| matrix | 174 | 0 | 0 |
| behavior | 33 | 0 | 0 |
| temperature | 509 | 0 | 0 |
| ui-review | 502 | 0 | 0 |
| design | 577 | 0 | 0 |
| visual | 13 | 0 | 3 |
| windows-real | 53 | 0 | 1 |

원본은 `test-results/full-validation-20261114-150251/manifest.json`, `test-results/full-validation-20261114-151745/manifest.json` 및 해당 실행 폴더에 보관했다. 위 개수는 이번 후보 실행만 집계하며 이전 반복 검사와 합산하지 않는다.

## 검사 경계와 이전 결과

외부 날씨·Google Calendar·OAuth·현재 위치 API는 HTTP 응답 경계 목업이며 미등록 요청은 실패시킨다. 실제 파서·저장·복원·오류 처리를 통과한다. `windows-real`의 Windows 입력 및 로컬 CPU/GPU/메모리/디스크/네트워크 수집만 실제다. 실연동 검사는 요청되거나 수행되지 않았다.

기존 설치본이 실행 중이므로 앱 설치·제거, 센서 서비스/PawnIO 설치·제거는 **SKIP**이다. E2E는 별도 TestHost와 실행별 데이터 및 시작 등록을 사용했고 매 실행에서 설치본 레이아웃·백업·시작 등록 불변을 확인했다. E2E SKIP의 개별 항목과 이유는 결과 JSON에 보존한다. 화면 가림·실제 로그인·Win+D·지원되지 않는 온도 등을 통과로 바꾸지 않는다.

최초 제한 실행 환경의 핵심 검사는 9,450개 이후 DPAPI 저장에서 실패했다. 동일 파일을 실제 사용자 실행 환경에서 다시 실행해 9,575개를 통과했다. 최초 로그 `test-results/release-v0.1.3-build-unit.log`를 보존한다.

[개발 단계 기능·스트레스·2시간 지속 검사](2026-11-14-grid-regression.md) 및 [두 PC의 30분 성능 검사](2026-11-14-personal-two-windows.json)는 **버전 리소스 변경 전 해시**의 기록이다. 이전/현재 Tempos.exe 및 TestHost의 PE `.text` SHA-256은 동일하지만 전체 파일 해시는 다르다. 이번 후보에서 2시간 지속 검사나 두 PC의 30분 성능을 다시 실행했다고 표시하지 않는다. 기본 5개 구성의 이전 실측은 각각 4.980/1.866 ms/초이며 새 필수 예산 10 ms/초를 통과했다. 현재 PC의 별도 절약 목표 4 ms/초는 미달이다.

다른 PC의 전체 기능·모니터 간 이동·배율 전환, 두 PC의 9개 구성 장시간 검사, PC별 3회 짧은 반복 기준선, 전력·배터리 검증은 완료 범위에 포함하지 않는다. README 화면은 표시된 v0.1.2 촬영 자료를 유지한다.

## 패키징·게시

설치 EXE와 ZIP에는 검증한 동일 배포 실행 파일을 넣고 TestHost·개인 설정·키를 제외한다. 문서·이미지·해시는 패키징 후 별도로 확인한다. 공개 게시와 다운로드 재검증 결과는 게시 후 문서 전용 영수증으로 추가한다. 이 후보 기록 자체는 게시 완료를 의미하지 않는다.
