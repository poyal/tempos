Unicode true
!include MUI2.nsh
!include LogicLib.nsh
!include FileFunc.nsh
!include WordFunc.nsh
!define SENSOR_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\TemposSensors"
Name "Tempos 센서 도우미"
OutFile "${SENSOR_OUT}"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
ShowInstDetails show
!define MUI_CUSTOMFUNCTION_ABORT SensorAbort
!define MUI_WELCOMEPAGE_TEXT "CPU 온도를 읽는 Tempos 서비스와 필요한 공식 서명 PawnIO 드라이버를 설치합니다.$\r$\n$\r$\nTempos 위젯 앱은 일반 권한으로 계속 실행됩니다. 센서 도우미를 제거해도 다른 앱이 사용하는 PawnIO는 유지합니다."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "Korean"
Var SetupMutex

Function .onInit
  SetRegView 64
  StrCpy $INSTDIR "$PROGRAMFILES64\Tempos\Sensors"
  System::Call 'kernel32::CreateMutexW(p 0, i 0, w "Global\Tempos.SensorSetup") p .r0 ?e'
  Pop $1
  StrCpy $SetupMutex $0
  ${If} $1 == 183
    SetErrorLevel 1618
    Quit
  ${EndIf}
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/REMOVE" $1
  ${IfNot} ${Errors}
    IfFileExists "$INSTDIR\Uninstall.exe" 0 missing
    ExecWait '"$INSTDIR\Uninstall.exe" /S _?=$INSTDIR' $0
    ${If} $0 == 0
      Delete "$INSTDIR\Uninstall.exe"
      RMDir "$INSTDIR"
    ${EndIf}
    SetErrorLevel $0
    Quit
    missing:
    SetErrorLevel 0
    Quit
  ${EndIf}
FunctionEnd
Function SensorAbort
  SetErrorLevel 1223
FunctionEnd

Section
  InitPluginsDir
  SetOutPath "$PLUGINSDIR"
  File /oname=SensorControl.exe "${CONTROLLER}"
  ExecWait '"$PLUGINSDIR\SensorControl.exe" /stop' $0
  ${If} $0 != 0
    SetErrorLevel $0
    Abort "기존 센서 서비스 확인 또는 중지 실패. 다른 서비스는 변경하지 않았습니다."
  ${EndIf}
  # Fixed Program Files destination; the controller rejects reparse-point ancestors.
  CreateDirectory "$INSTDIR"
  nsExec::ExecToStack '"$SYSDIR\icacls.exe" "$INSTDIR" /inheritance:r /grant:r "*S-1-5-18:(OI)(CI)F" "*S-1-5-32-544:(OI)(CI)F" "*S-1-5-32-545:(OI)(CI)RX"'
  Pop $0
  Pop $1
  ${If} $0 != 0
    SetErrorLevel 5
    Abort "센서 폴더 권한 설정 실패"
  ${EndIf}
  ClearErrors
  !include "${SENSOR_INSTALL}"
  ${If} ${Errors}
    SetErrorLevel 5
    Abort "센서 파일 복사 실패"
  ${EndIf}
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKLM "${SENSOR_KEY}" "DisplayName" "Tempos 센서 도우미"
  WriteRegStr HKLM "${SENSOR_KEY}" "DisplayVersion" "1.0.0"
  WriteRegStr HKLM "${SENSOR_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${SENSOR_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
  WriteRegDWORD HKLM "${SENSOR_KEY}" "NoModify" 1
  ReadRegStr $0 HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PawnIO" "DisplayVersion"
  ${VersionCompare} $0 "2.2.0" $1
  ${If} $0 == ""
  ${OrIf} $1 == 2
    SetOutPath "$PLUGINSDIR"
    File /oname=PawnIO_setup.exe "${PAWN_INSTALLER}"
    ExecWait '"$PLUGINSDIR\PawnIO_setup.exe" -install -silent' $0
    ${If} $0 == 3010
      SetErrorLevel 3010
      MessageBox MB_OK "Windows 재시작 후 Tempos 설정에서 센서 도우미 설치 / 복구를 실행하세요." /SD IDOK
      Quit
    ${ElseIf} $0 != 0
      SetErrorLevel $0
      Abort "PawnIO 설치 실패. 설치 / 복구로 다시 시도할 수 있습니다."
    ${EndIf}
  ${EndIf}
  ExecWait '"$INSTDIR\Tempos.SensorControl.exe" /register' $0
  ${If} $0 != 0
    SetErrorLevel $0
    Abort "센서 서비스 시작 실패. 설치 / 복구로 다시 시도할 수 있습니다."
  ${EndIf}
  SetErrorLevel 0
SectionEnd

Function un.onInit
  SetRegView 64
  StrCpy $INSTDIR "$PROGRAMFILES64\Tempos\Sensors"
FunctionEnd
Section "Uninstall"
  ExecWait '"$INSTDIR\Tempos.SensorControl.exe" /unregister' $0
  ${If} $0 != 0
    SetErrorLevel $0
    Abort "센서 서비스 중지 / 제거 실패"
  ${EndIf}
  !include "${SENSOR_UNINSTALL}"
  Delete "$INSTDIR\Uninstall.exe"
  DeleteRegKey HKLM "${SENSOR_KEY}"
  SetOutPath "$TEMP"
  RMDir "$INSTDIR"
  # PawnIO is shared with other applications and is intentionally retained.
  SetErrorLevel 0
SectionEnd
