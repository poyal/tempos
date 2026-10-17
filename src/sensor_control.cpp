// Elevated, fixed-purpose service management invoked only by the sensor installer.
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <string>
#include <vector>
namespace {
struct ServiceHandle {
  SC_HANDLE handle{};
  ~ServiceHandle() { if (handle) CloseServiceHandle(handle); }
};
DWORD waitFor(SC_HANDLE service, DWORD desired) {
  const auto until = GetTickCount64() + 30000;
  SERVICE_STATUS_PROCESS state{};
  DWORD bytes = 0;
  while (GetTickCount64() < until) {
    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
                              reinterpret_cast<BYTE *>(&state), sizeof state, &bytes))
      return GetLastError();
    if (state.dwCurrentState == desired) return ERROR_SUCCESS;
    if (desired == SERVICE_RUNNING && state.dwCurrentState == SERVICE_STOPPED)
      return state.dwWin32ExitCode ? state.dwWin32ExitCode : ERROR_SERVICE_NOT_ACTIVE;
    Sleep(100);
  }
  return ERROR_TIMEOUT;
}
DWORD stopService(SC_HANDLE service) {
  SERVICE_STATUS state{};
  if (!ControlService(service, SERVICE_CONTROL_STOP, &state)) {
    auto error = GetLastError();
    if (error == ERROR_SERVICE_NOT_ACTIVE) return ERROR_SUCCESS;
    if (error != ERROR_SERVICE_CANNOT_ACCEPT_CTRL) return error;
  }
  return waitFor(service, SERVICE_STOPPED);
}
DWORD control(std::wstring action) {
  if (action != L"/stop" && action != L"/register" && action != L"/unregister")
    return ERROR_INVALID_PARAMETER;
  PWSTR programFiles = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFilesX64, 0, nullptr, &programFiles)))
    return ERROR_PATH_NOT_FOUND;
  std::wstring base = std::wstring(programFiles) + L"\\Tempos";
  CoTaskMemFree(programFiles);
  for (const auto &path : {base, base + L"\\Sensors"}) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
      return ERROR_ACCESS_DENIED;
  }
  std::wstring executable = base + L"\\Sensors\\Tempos.Sensors.exe";
  std::wstring command = L"\"" + executable + L"\"";
  ServiceHandle manager{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE)};
  if (!manager.handle) return GetLastError();
  ServiceHandle service{OpenServiceW(manager.handle, L"TemposSensors", SERVICE_ALL_ACCESS)};
  if (service.handle) {
    DWORD bytes = 0;
    QueryServiceConfigW(service.handle, nullptr, 0, &bytes);
    if (!bytes || bytes > 65536) return ERROR_INVALID_DATA;
    std::vector<BYTE> buffer(bytes);
    auto config = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(buffer.data());
    if (!QueryServiceConfigW(service.handle, config, bytes, &bytes)) return GetLastError();
    if (_wcsicmp(config->lpBinaryPathName, command.c_str()) != 0)
      return ERROR_ACCESS_DENIED;
  } else if (GetLastError() != ERROR_SERVICE_DOES_NOT_EXIST) return GetLastError();
  if (action == L"/stop" || action == L"/unregister") {
    if (!service.handle) return ERROR_SUCCESS;
    DWORD result = stopService(service.handle);
    if (result) return result;
    if (action == L"/unregister" && !DeleteService(service.handle)) return GetLastError();
    return ERROR_SUCCESS;
  }
  if (GetFileAttributesW(executable.c_str()) == INVALID_FILE_ATTRIBUTES) return ERROR_FILE_NOT_FOUND;
  if (!service.handle)
    service.handle = CreateServiceW(manager.handle, L"TemposSensors", L"Tempos CPU temperature sensors",
        SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
        command.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr);
  if (!service.handle) return GetLastError();
  SERVICE_DESCRIPTIONW description{const_cast<wchar_t *>(L"Read-only CPU temperature for Tempos. Idle without widget requests.")};
  ChangeServiceConfig2W(service.handle, SERVICE_CONFIG_DESCRIPTION, &description);
  SERVICE_DELAYED_AUTO_START_INFO delayed{TRUE};
  ChangeServiceConfig2W(service.handle, SERVICE_CONFIG_DELAYED_AUTO_START_INFO, &delayed);
  if (!StartServiceW(service.handle, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING)
    return GetLastError();
  return waitFor(service.handle, SERVICE_RUNNING);
}
}
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  int count = 0;
  auto args = CommandLineToArgvW(GetCommandLineW(), &count);
  DWORD result = count == 2 ? control(args[1]) : ERROR_INVALID_PARAMETER;
  if (args) LocalFree(args);
  return int(result);
}
