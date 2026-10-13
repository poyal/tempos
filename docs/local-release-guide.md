# 로컬 검증과 패키징 안내

적용 대상은 Tempos Windows 11 x64 설치 EXE와 무설치 ZIP 배포다. Passport·Perilog의 로컬 검증 후 동일 산출물 배포 원칙을 참조했다. 설치 EXE는 Passport처럼 현재 사용자용 NSIS 설치·폴더 선택·시작 메뉴·Windows 제거 등록을 제공한다. 다른 프로젝트의 Electron·Android 명령이나 검증 개수를 사용하지 않는다.

## 변경과 검증

1. 앱·테스트·문서를 최종 동작에 맞춘다. 개발 중에는 관련 검사를 먼저 실행한다.
2. 배포 후보에서 Release·ASan 핵심 검사와 변경 관련 E2E를 완료한다. 사용자 데이터는 쓰지 않는다.
3. 빌드 소스 커밋, 미커밋 변경 여부, 실행 시각·명령·종료 코드·바이너리 해시를 기록한다. 수정 후 검사한 파일인지 확인한다.
4. 실패·생략·실제 서비스 미검증은 별도로 남긴다. 기존 버전의 통과를 새 빌드 결과로 대신하지 않는다.

## 동일 파일 패키징

```powershell
# 이미 검증한 out/release의 파일을 재빌드하지 않고 포장
./scripts/package.ps1 -SkipBuild

# 일반 E2E는 TestHost와 목업 API로 실행
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release --no-build -- . --smoke
```

`-SkipBuild` 없이 실행하면 새 빌드를 만들므로 이전 바이너리 검사 결과를 그대로 적용할 수 없다. 패키지 버전은 `CMakeLists.txt`·리소스·문서와 맞춘다. ZIP 파일명만으로 공개 릴리즈가 되지는 않는다.

ZIP에 실행 파일, `data/regions.json`, 사용자 README, docs, 외부 고지·라이선스가 포함되는지 확인한다. TestHost·개인 설정·캐시·키는 포함하지 않는다. 압축을 푼 실행 파일 SHA-256이 빌드한 Release 실행 파일과 같은지 확인한다.

스크립트는 실행 파일의 버전과 ZIP 버전의 일치도 검사한다. README와 사용법 이미지는 ZIP 안에서 열 수 있게 포함하고, docs에서 참조하는 과거 디자인·개발 지침은 같은 릴리즈 태그의 저장소 링크로 변환한다. 패키징 후 `node scripts/verify-docs.cjs <압축 푼 앱 폴더>`로 문서·이미지 참조와 촬영 해시를 확인한다.

위 E2E는 TestHost에서 외부 API 목업으로 실행한다. 압축을 푼 배포 앱의 실제 서비스 연결 검사는 사용자가 별도로 요청한 경우에만 `--integration-live-api --exe <압축푼폴더/Tempos.exe>`를 붙여 수행하고 실연동 결과로 기록한다. 목업 검사와 패키지 내용·해시 확인을 실제 API 연결 성공으로 표시하지 않는다.

## Windows 설치 EXE

Unicode NSIS를 준비하고 `TEMPOS_MAKENSIS` 환경 변수 또는 `-NsisCompiler`로 `makensis.exe`를 지정한다. `.tools/nsis/Bin/makensis.exe`도 자동 인식한다. v0.1.0은 Passport의 로컬 도구와 같은 NSIS v3.04로 검증했다. NSIS 도구·런타임 라이선스 `COPYING`이 도구 루트에 있어야 한다.

```powershell
# 검증된 ZIP과 .zip.sha256을 입력으로 사용한다. 앱은 재빌드하지 않는다.
./scripts/package-installer.ps1 -Version 0.1.0
dotnet run --project tests/e2e/Tempos.E2E.csproj -c Release -- . --installer --exe artifacts/Tempos-0.1.0-win-x64.exe
node scripts/verify-docs.cjs
```

`artifacts/installer-package.json`에는 입력 ZIP·설치 EXE·앱 실행 파일·NSIS 컴파일러 해시와 전체 설치 파일 목록을 기록한다. ZIP의 앱은 유지하며 현재 README·docs·고지를 설치본에 갱신한다. 입력 ZIP과 기존 공개 ZIP을 덮어쓰지 않는다. 생성한 `payload` 경로에서도 `verify-docs.cjs`를 실행한다. 설치 그림은 `scripts/installer-artwork.ps1`로 재생성할 수 있다.

설치 기본 경로는 `%LocalAppData%\Programs\Tempos`, 사용자 데이터는 `%LocalAppData%\Tempos`다. 설치는 현재 사용자 HKCU에만 등록하고 `/allusers`는 종료 코드 2로 거부한다. 실행 중인 Tempos가 있으면 종료 코드 10으로 중단한다. `/S /D=<전용 설치 폴더>`로 무인 설치할 수 있으며 NSIS 규칙에 따라 `/D=`는 마지막 인수로 두고 경로를 따옴표로 감싸지 않는다. 드라이브 루트·데이터 폴더·기존 설치와 다른 경로는 허용하지 않는다.

`--installer`는 실제 설치 파일·HKCU 등록·바로가기·한글 경로·실행·재설치·제거·설정 보존을 검사한다. 기존 설치·바로가기·사용자 앱이 있으면 시작하지 않는다. 설치 폴더와 시계만 있는 테스트 데이터는 `test-results` 아래에 두며 테스트가 만든 항목만 정리한다. API 동작 검사는 하지 않으며 일반 목업 E2E와 별도 결과로 남긴다. 자세한 격리 조건은 [E2E 안내](e2e.md#installer)에 있다.

## 보관과 게시

빌드는 `out/`, 배포 파일과 로그는 `artifacts/`, 실행별 원본은 `test-results/`, 장기 요약은 `docs/validation/`에 둔다. 기존 검증 근거와 사용자 복구 자료를 임의로 지우지 않는다. 생성물과 사용자 데이터는 Git에 넣지 않는다.

패키징 스크립트는 ZIP 또는 설치 EXE와 각각의 SHA-256 파일을 만든다. 공개 릴리즈 게시 자동화는 구현되어 있지 않다. Git 커밋·푸시와 공개 릴리즈 게시를 구분하며, 게시 요청이 있을 때 검증한 동일 파일을 게시하고 공개 URL에서 재다운로드 해시를 확인한다. 검사 후 다시 패키징했다면 새 설치 EXE를 다시 검사해야 한다.

이미 공개한 태그·ZIP은 바꾸지 않는다. 같은 앱 버전에 설치형을 추가하면 패키징 소스를 별도 태그(예: `v0.1.0-win-x64-installer`)로 남기고 기존 릴리즈에 EXE와 `.exe.sha256`만 추가한다. 게시 후 영수증은 문서 전용 후속 커밋으로 남기며 검증한 설치 파일은 재빌드하지 않는다.

검증 기록에는 다음을 남긴다: 검사 환경, 소스 기준, 실행 파일 해시, 실행별 결과 링크, 실패와 재검사 이유, 실제 서비스 확인 범위, ZIP 해시, 게시 여부. 문서 전용 후속 수정은 바이너리 변경과 구분한다.
