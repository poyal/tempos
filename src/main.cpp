#include "app.h"
#include <commctrl.h>
#include <shellapi.h>
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  INITCOMMONCONTROLSEX controls{sizeof controls, ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES};
  InitCommonControlsEx(&controls);
  std::filesystem::path root;
  bool visible = false, fixture = false;
#ifdef TEMPOS_TEST_HOST
  fixture = true;
  visible = true;
#endif
  int argc = 0;
  auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  for (int i = 1; i < argc; ++i) {
    std::wstring arg = argv[i];
    if (arg == L"--settings")
      visible = true;
    if (arg == L"--data-dir" && i + 1 < argc)
      root = argv[++i];
#ifdef TEMPOS_TEST_HOST
    if (arg == L"--live")
      fixture = false;
#endif
  }
  if (argv)
    LocalFree(argv);
  std::wstring mutexName = L"Local\\Tempos.Desktop";
#ifdef TEMPOS_TEST_HOST
  mutexName += L".Test." + std::to_wstring(std::hash<std::wstring>{}(root.wstring()));
#endif
  HANDLE single = CreateMutexW(nullptr, FALSE, mutexName.c_str());
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    auto existing = FindWindowW(L"Tempos.Management", nullptr);
    if (existing) {
      ShowWindow(existing, SW_SHOW);
      SetForegroundWindow(existing);
    }
    if (single)
      CloseHandle(single);
    CoUninitialize();
    return 0;
  }
  int result = 1;
  try {
    tempos::App app(instance, root, fixture, visible);
    result = app.run();
  } catch (const std::exception &) {
    MessageBoxW(nullptr, L"Tempos를 시작할 수 없습니다. 그래픽 드라이버와 설정 파일 접근 권한을 확인하세요.",
                L"Tempos", MB_OK | MB_ICONERROR);
  }
  if (single)
    CloseHandle(single);
  CoUninitialize();
  return result;
}
