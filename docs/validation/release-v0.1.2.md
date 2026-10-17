# v0.1.2 릴리즈 검증

2026-10-17 Windows 11 x64에서 실행했다. 앱 소스는 `7e2348e6cb7a0aead29b672a4954057e00f2cb9e`, E2E 러너 준비 대기 보완은 `19cb95d3d5a6c51d37e4f69eca32a435050c8dd3`다. 둘 사이에 네이티브 앱 코드는 바뀌지 않았다.

## 최종 결과

| 검사 | 통과 | SKIP | 실행 |
| --- | ---: | ---: | --- |
| release 핵심 | 243 | 0 | `artifacts/release-v0.1.2-release-core.log` |
| asan 핵심 | 243 | 0 | `artifacts/release-v0.1.2-asan-core.log` |
| `--ui-review` | 500 | 0 | `test-results/e2e-20261017-152735-350` |
| `--temperature` | 505 | 0 | `test-results/e2e-20261017-153008-126` |
| `--weather` | 70 | 0 | `test-results/e2e-20261017-153229-126` |
| `--windows-real` | 50 | 2 | `test-results/e2e-20261017-153440-429` |

최종 E2E **1125개 통과·실패 0·SKIP 2개**다. Release·ASan 핵심 검사는 각각 별도로 표시했으며 이전 개발 결과나 중단 실행의 통과 수를 더하지 않았다. 센서 도우미의 격리된 파이프 검사 22개도 패키징에서 별도로 통과했다.

외부 날씨·위치·Google Calendar·OAuth는 HTTP 경계 목업이다. `--windows-real`에서만 로컬 Windows 시스템 자료를 실제로 읽었고 센서/설치 결과 목업·디자인 예제는 실측으로 표시하지 않는다. 모든 최종 E2E는 설치 레이아웃·백업·자동 시작 값의 전후 비교를 통과했다.

SKIP 항목:

- --windows-real — Actual local temperature: kind 2: No valid sensor reading; status 4. This suite never installs a driver or sensor service.
- --windows-real — OS SendInput drag: Windows input desktop did not accept an observable cursor move; native-message tests are separate.

기존 Tempos 설치가 있으므로 설치·제거 수명주기와 CPU 서비스/PawnIO 설치·제거는 실행하지 않았다. 실제 외부 API·실제 Windows 재로그인도 검사 범위가 아니다.

## 최초 중단과 보완

첫 UI 실행 `e2e-20261017-152320-698`은 재시작 직후 UIA 조회에서 대기해 중단했다. 네이티브 메시지에는 응답했지만 설정창은 아직 표시되지 않은 상태였다. TestHost HWND 생성 직후 자동화 객체를 만들던 러너를 수정해, 설정창 표시와 탭 컨트롤 생성을 확인한 뒤 UIA로 접근한다. 앱은 검증한 해당 TestHost에만 정상 종료를 요청했고 대기 중인 러너만 별도로 중단했다. 원시 로그를 보존하며 결과 JSON이 없는 이 실행을 통과로 처리하지 않는다. 중단 실행의 설치 프로파일 비교는 완료되지 않았고 최종 실행들에서 다시 확인했다.

## 디자인과 배포 무결성

[HTML 전후 비교](2026-10-17-layout-report.html)는 버전 번호를 올리기 전 구현 검증 기록이다. 이후 앱 변경은 0.1.2 버전 정보뿐이며, 최종 바이너리로 37개 구성과 100/150/200% 배율·색상 픽셀을 다시 검사했다. 보고서의 예제 이미지와 실측을 구분한다. [최종 실행 전체 결과와 SHA-256](release-v0.1.2.json).

설치 EXE·ZIP은 검사한 Release 실행 파일을 재빌드 없이 포함한다. TestHost는 제외한다. 패키징 소스와 릴리즈 태그는 `fbcb036564e7bd2579c933816c03a99598e858d9`이며 패키징 시 작업 트리는 깨끗했다.

2026-10-17 15:39 KST에 [v0.1.2 릴리즈](https://github.com/poyal/tempos/releases/tag/v0.1.2)를 게시했다. EXE·ZIP·체크섬·HTML 보고서 3종 등 첨부 8개를 공개 URL에서 인증 없이 다시 내려받아 SHA-256 일치를 확인했다. 공개 ZIP의 실행 파일 버전 0.1.2·검증 바이너리 해시·TestHost 제외·문서 참조도 통과했다. [게시 후 다운로드·패키징 기록](release-v0.1.2-published.json).

| 공개 파일 | SHA-256 |
| --- | --- |
| 설치 EXE | `673B05485B35DEB094D02C7592B4261438F2F8F24A63F99B779AB0243D148A26` |
| 무설치 ZIP | `E27CADC2F7A15163B01DAA567FFA71414E6F64EEB0E3A0038546D1A58D8684B4` |
| 포함된 Tempos.exe | `96EEE394EB5B528AFEC373FF5107F19D3CACBB4EB45CC2A8215C3B4A9E257330` |

게시 후에는 이 검증 영수증과 문서만 후속 커밋했다. 공개 태그·설치 파일·ZIP은 바꾸거나 다시 만들지 않았다.
