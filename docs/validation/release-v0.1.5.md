# Tempos v0.1.5 릴리즈 후보 검증

Windows x64에서 버전 0.1.5로 빌드한 앱을 검사했습니다. Release와 AddressSanitizer 빌드의 핵심 검사는 각각 9,699개가 통과했습니다. 최초 샌드박스 실행은 두 빌드 모두 Windows DPAPI 저장에서 중단됐으며, 동일한 바이너리를 사용자 세션에서 다시 검사했습니다.

| 빌드 | Windows E2E | 통과 | 실패 | 건너뜀 |
| --- | --- | ---: | ---: | ---: |
| release | workspace-editor | 62 | 0 | 0 |
| release | ui-review | 532 | 0 | 0 |
| release | updates | 83 | 0 | 0 |
| release | legacy-grid | 28 | 0 | 0 |
| release | persistence | 223 | 0 | 106 |
| release | windows-real | 52 | 0 | 1 |
| asan | workspace-editor | 62 | 0 | 0 |
| asan | updates | 83 | 0 | 0 |

Windows UI와 저장·재시작은 격리된 TestHost에서 검사했습니다. 외부 서비스와 업데이트 서버에는 HTTP 목업을 사용했고, windows-real 모드에서는 로컬 하드웨어 카운터를 실제로 읽었습니다. SKIP 사유는 실행별 기록에 있습니다. 기존 Tempos가 설치되어 실행 중이어서 설치·제거 수명주기 검사는 건너뛰었습니다.

배포 앱 SHA-256은 `7DC2E041EA986608263CAA04592BC128E92789B65987618CA84054CA3F6E1333`입니다. 이 앱을 다시 빌드하지 않고 설치 EXE와 무설치 ZIP에 넣습니다.

[최종 후보의 명령·해시와 실행별 결과](release-v0.1.5.json) · [개발 단계 화면 및 성능 검증](editor-2026-11-24/results.md) · [릴리즈 노트](../releases/v0.1.5.md)

## 공개 다운로드 검증

GitHub 최신 정식 릴리즈로 [v0.1.5](https://github.com/poyal/tempos/releases/tag/v0.1.5)를 게시했습니다. 릴리즈 소스는 `98ea027d8d3e40284ad2f30191ebd32e4bb009ba`입니다. 설치 EXE, ZIP, 전후 비교 HTML, 체크섬 파일 세 개를 공개 URL에서 인증 없이 다시 받았습니다. 여섯 파일 모두 크기와 SHA-256이 로컬 원본 및 GitHub 메타데이터와 일치했습니다. 설치 페이로드의 321개 파일 해시와 ZIP에 포함된 앱 해시도 확인했습니다. [공개 파일별 해시와 다운로드 주소](release-v0.1.5-published.json)

패키징 중 센서 검사 22개가 통과했고, 성능 측정 정책 및 로그 동시 읽기 검사 25개도 통과했습니다.
