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

최종 E2E **1125개 통과·실패 0·SKIP 2개**다. Release·ASan 핵심 검사는 각각 별도로 표시했으며 이전 개발 결과나 중단 실행의 통과 수를 더하지 않았다. 센서 도우미의 격리된 파이프 검사는 패키징에서 별도로 실행하고 게시 기록에 남긴다.

외부 날씨·위치·Google Calendar·OAuth는 HTTP 경계 목업이다. `--windows-real`에서만 로컬 Windows 시스템 자료를 실제로 읽었고 센서/설치 결과 목업·디자인 예제는 실측으로 표시하지 않는다. 모든 최종 E2E는 설치 레이아웃·백업·자동 시작 값의 전후 비교를 통과했다.

SKIP 항목:

- --windows-real — Actual local temperature: kind 2: No valid sensor reading; status 4. This suite never installs a driver or sensor service.
- --windows-real — OS SendInput drag: Windows input desktop did not accept an observable cursor move; native-message tests are separate.

기존 Tempos 설치가 있으므로 설치·제거 수명주기와 CPU 서비스/PawnIO 설치·제거는 실행하지 않았다. 실제 외부 API·실제 Windows 재로그인도 검사 범위가 아니다.

## 최초 중단과 보완

첫 UI 실행 `e2e-20261017-152320-698`은 재시작 직후 UIA 조회에서 대기해 중단했다. 네이티브 메시지에는 응답했지만 설정창은 아직 표시되지 않은 상태였다. TestHost HWND 생성 직후 자동화 객체를 만들던 러너를 수정해, 설정창 표시와 탭 컨트롤 생성을 확인한 뒤 UIA로 접근한다. 앱은 검증한 해당 TestHost에만 정상 종료를 요청했고 대기 중인 러너만 별도로 중단했다. 원시 로그를 보존하며 결과 JSON이 없는 이 실행을 통과로 처리하지 않는다. 중단 실행의 설치 프로파일 비교는 완료되지 않았고 최종 실행들에서 다시 확인했다.

## 디자인과 배포 무결성

[HTML 전후 비교](2026-10-17-layout-report.html)는 버전 번호를 올리기 전 구현 검증 기록이다. 이후 앱 변경은 0.1.2 버전 정보뿐이며, 최종 바이너리로 37개 구성과 100/150/200% 배율·색상 픽셀을 다시 검사했다. 보고서의 예제 이미지와 실측을 구분한다. [최종 실행 전체 결과와 SHA-256](release-v0.1.2.json).

설치 EXE·ZIP은 검사한 Release 실행 파일을 재빌드 없이 포함한다. TestHost는 제외한다. 게시 후 공개 URL에서 인증 없이 다시 내려받아 로컬 검증 파일 및 공개 체크섬과 일치하는지 확인하며 그 결과는 문서 전용 후속 커밋에 기록한다.
