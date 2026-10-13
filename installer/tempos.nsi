Unicode true
!include MUI2.nsh
!include LogicLib.nsh
!include FileFunc.nsh
!include x64.nsh
!include StrFunc.nsh
${StrStr}

!define APP_KEY "Software\Tempos\Installer"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Tempos"
!define RUN_KEY "Software\Microsoft\Windows\CurrentVersion\Run"
Name "Tempos"
OutFile "${INSTALLER_OUT}"
InstallDir "$LOCALAPPDATA\Programs\Tempos"
InstallDirRegKey HKCU "${APP_KEY}" "InstallLocation"
RequestExecutionLevel user
ManifestDPIAware true
SetCompressor /SOLID lzma
BrandingText "Tempos · 바탕화면 위젯"
VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey /LANG=1042 "ProductName" "Tempos"
VIAddVersionKey /LANG=1042 "CompanyName" "Poyal"
VIAddVersionKey /LANG=1042 "FileDescription" "Tempos Windows x64 설치"
VIAddVersionKey /LANG=1042 "FileVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1042 "ProductVersion" "${APP_VERSION}"
VIAddVersionKey /LANG=1042 "LegalCopyright" "Tempos contributors"
!define MUI_ICON "${APP_ICON}"
!define MUI_UNICON "${APP_ICON}"
!define MUI_WELCOMEFINISHPAGE_BITMAP "${APP_SIDEBAR}"
!define MUI_ABORTWARNING
!define MUI_DIRECTORYPAGE_TEXT_TOP "현재 로그인한 사용자에게 Tempos를 설치합니다. 관리자 권한은 필요하지 않습니다.$\r$\n$\r$\n설치 폴더를 선택하세요. 기존 설치가 있으면 같은 폴더에 업데이트합니다."
!define MUI_FINISHPAGE_RUN "$INSTDIR\Tempos.exe"
!define MUI_FINISHPAGE_RUN_PARAMETERS "--settings"
!define MUI_FINISHPAGE_RUN_TEXT "Tempos 실행"
!define MUI_FINISHPAGE_TEXT "Tempos 설치를 완료했습니다.$\r$\n$\r$\n시작 메뉴 또는 바탕화면의 Tempos를 실행하세요. 위젯 설정은 작업표시줄의 Tempos 아이콘에서 엽니다."
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!define MUI_UNCONFIRMPAGE_TEXT_TOP "Tempos와 바로가기를 제거합니다. 저장한 위젯 구성과 연동 정보는 유지합니다."
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "Korean"

Var InstallerMutex
Var ExistingDir
Var RequestedDir

!macro CheckRunning
  System::Call 'kernel32::OpenMutexW(i 0x100000, i 0, w "Local\Tempos.Desktop") p .r0'
  ${If} $0 != 0
    System::Call 'kernel32::CloseHandle(p r0)'
    MessageBox MB_OK|MB_ICONEXCLAMATION "Tempos가 실행 중입니다. 트레이 메뉴에서 종료한 뒤 다시 실행하세요." /SD IDOK
    SetErrorLevel 10
    Quit
  ${EndIf}
!macroend

!macro Init
  SetShellVarContext current
  SetRegView 64
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/allusers" $1
  ${IfNot} ${Errors}
    SetErrorLevel 2
    Quit
  ${EndIf}
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "Tempos는 Windows x64에서 실행할 수 있습니다." /SD IDOK
    SetErrorLevel 3
    Quit
  ${EndIf}
  System::Call 'kernel32::CreateMutexW(p 0, i 0, w "Local\Tempos.Installer") p .r0 ?e'
  Pop $1
  StrCpy $InstallerMutex $0
  ${If} $1 == 183
    SetErrorLevel 4
    Quit
  ${EndIf}
  !insertmacro CheckRunning
!macroend

Function .onInit
  !insertmacro Init
  ReadRegStr $ExistingDir HKCU "${APP_KEY}" "InstallLocation"
  # NSIS can discard an invalid /D= and silently fall back to its default.
  # Read the original command line so an explicit path is always honored or rejected.
  System::Call 'kernel32::GetCommandLineW() w .r0'
  ${StrStr} $RequestedDir $0 "/D="
  ${If} $RequestedDir != ""
    StrCpy $RequestedDir $RequestedDir "" 3
    # NSIS GetFullPathName can return empty for a not-yet-created directory.
    System::Call 'kernel32::GetFullPathNameW(w "$RequestedDir", i ${NSIS_MAX_STRLEN}, w .r2, p 0) i .r1'
    ${If} $1 == 0
    ${OrIf} $1 >= ${NSIS_MAX_STRLEN}
      SetErrorLevel 6
      Quit
    ${EndIf}
    StrCpy $INSTDIR $2
    Call ValidateInstallDir
    ${If} $3 != 0
      SetErrorLevel 6
      Quit
    ${EndIf}
  ${EndIf}
FunctionEnd

Function ValidateInstallDir
  StrCpy $3 0
  # Keep one registered installation; relocation is uninstall then install.
  ${If} $ExistingDir != ""
  ${AndIf} $INSTDIR != $ExistingDir
    StrCpy $3 1
  ${EndIf}
  ${If} $INSTDIR == "$LOCALAPPDATA\Tempos"
  ${OrIf} $INSTDIR == "$LOCALAPPDATA"
  ${OrIf} $INSTDIR == "$PROFILE"
  ${OrIf} $INSTDIR == "$DESKTOP"
  ${OrIf} $INSTDIR == "$WINDIR"
    StrCpy $3 1
  ${EndIf}
  ${GetRoot} "$INSTDIR" $0
  ${If} $INSTDIR == "$0\"
  ${OrIf} $INSTDIR == "$0"
    StrCpy $3 1
  ${EndIf}
FunctionEnd

Function .onVerifyInstDir
  Call ValidateInstallDir
  ${If} $3 != 0
    Abort
  ${EndIf}
FunctionEnd

Section "Tempos" Main
  Call ValidateInstallDir
  ${If} $3 != 0
    MessageBox MB_OK|MB_ICONSTOP "기존 설치 폴더 또는 새로운 앱 전용 폴더를 선택하세요." /SD IDOK
    SetErrorLevel 6
    Quit
  ${EndIf}
  !insertmacro CheckRunning
  SetOverwrite on
  ClearErrors
  !include "${PAYLOAD_INSTALL}"
  IfErrors 0 +3
    SetErrorLevel 7
    Quit
  WriteUninstaller "$INSTDIR\Uninstall Tempos.exe"
  WriteRegStr HKCU "${APP_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "Tempos"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "Poyal"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\Tempos.exe,0"
  WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '$\"$INSTDIR\Uninstall Tempos.exe$\"'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '$\"$INSTDIR\Uninstall Tempos.exe$\" /S'
  WriteRegStr HKCU "${UNINSTALL_KEY}" "URLInfoAbout" "https://github.com/poyal/tempos"
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "EstimatedSize" ${PAYLOAD_KIB}
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
  CreateDirectory "$SMPROGRAMS\Tempos"
  CreateShortcut "$SMPROGRAMS\Tempos\Tempos.lnk" "$INSTDIR\Tempos.exe" "--settings" "$INSTDIR\Tempos.exe"
  CreateShortcut "$DESKTOP\Tempos.lnk" "$INSTDIR\Tempos.exe" "--settings" "$INSTDIR\Tempos.exe"
SectionEnd

Function un.onInit
  !insertmacro Init
  ReadRegStr $ExistingDir HKCU "${APP_KEY}" "InstallLocation"
  ${If} $ExistingDir == ""
  ${OrIf} $INSTDIR != $ExistingDir
    SetErrorLevel 5
    Quit
  ${EndIf}
FunctionEnd

Section "Uninstall"
  !insertmacro CheckRunning
  # Remove only startup commands pointing at this install. Preserve another copy.
  ReadRegStr $0 HKCU "${RUN_KEY}" "Tempos"
  StrLen $1 '$\"$INSTDIR\Tempos.exe$\"'
  StrCpy $2 $0 $1
  ${If} $2 == '$\"$INSTDIR\Tempos.exe$\"'
    DeleteRegValue HKCU "${RUN_KEY}" "Tempos"
  ${EndIf}
  # The generated manifest deletes exact payload files, then empty directories.
  # Never recursively remove INSTDIR or the separate LocalAppData\Tempos profile.
  !include "${PAYLOAD_UNINSTALL}"
  Delete "$INSTDIR\Uninstall Tempos.exe"
  Delete "$SMPROGRAMS\Tempos\Tempos.lnk"
  RMDir "$SMPROGRAMS\Tempos"
  Delete "$DESKTOP\Tempos.lnk"
  DeleteRegKey HKCU "${UNINSTALL_KEY}"
  DeleteRegKey HKCU "${APP_KEY}"
  SetOutPath "$TEMP"
  RMDir "$INSTDIR"
SectionEnd
