#include "update_install.h"
#include "startup.h"
#include "test_isolation.h"
#include <fstream>
namespace tempos {
namespace {
struct Handle {
  HANDLE value{};
  explicit Handle(HANDLE h = nullptr) : value(h) {}
  ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;
};
bool samePath(const std::filesystem::path &a, const std::filesystem::path &b) {
  std::error_code ea, eb;
  auto aa = std::filesystem::weakly_canonical(a, ea), bb = std::filesystem::weakly_canonical(b, eb);
  return !ea && !eb && _wcsicmp(aa.c_str(), bb.c_str()) == 0;
}
std::wstring quote(const std::filesystem::path &value) { return startupCommand(value.wstring()); }
uint64_t createdAt(HANDLE process) {
  FILETIME created{}, exited{}, kernel{}, user{};
  if (!GetProcessTimes(process, &created, &exited, &kernel, &user)) return 0;
  return (uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}
bool start(const std::filesystem::path &exe, const std::wstring &arguments, PROCESS_INFORMATION &process) {
  auto command = quote(exe) + L" " + arguments;
  STARTUPINFOW startup{sizeof startup};
  startup.dwFlags = STARTF_USESHOWWINDOW; startup.wShowWindow = SW_HIDE;
  return CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, exe.parent_path().c_str(), &startup, &process) != FALSE;
}
#ifndef TEMPOS_TEST_HOST
bool installedVersionMatches(const std::filesystem::path &app, const std::string &expected) {
  DWORD ignored{};
  DWORD size = GetFileVersionInfoSizeW(app.c_str(), &ignored);
  if (!size || size > 1024 * 1024) return false;
  std::vector<unsigned char> bytes(size);
  if (!GetFileVersionInfoW(app.c_str(), 0, size, bytes.data())) return false;
  VS_FIXEDFILEINFO *info{}; UINT length{};
  if (!VerQueryValueW(bytes.data(), L"\\", reinterpret_cast<void **>(&info), &length) ||
      length < sizeof *info || info->dwSignature != 0xfeef04bd || LOWORD(info->dwFileVersionLS) != 0) return false;
  return std::to_string(HIWORD(info->dwFileVersionMS)) + "." + std::to_string(LOWORD(info->dwFileVersionMS)) + "." +
      std::to_string(HIWORD(info->dwFileVersionLS)) == expected;
}
#endif
Json readJob(const std::filesystem::path &file) {
  auto text = boundedRead(file, 16384);
  if (!text) throw std::runtime_error("update job");
  auto j = Json::parse(*text);
  if (j.at("schema") != 1 || file.filename() != L"job.json" ||
      !samePath(file.parent_path(), runningExecutable().parent_path())) throw std::runtime_error("job location");
  auto root = std::filesystem::path(wide(j.at("dataRoot").get<std::string>()));
  if (!samePath(file.parent_path().parent_path(), root / L"updates")) throw std::runtime_error("job root");
#ifdef TEMPOS_TEST_HOST
  if (!isolatedTestProfile(root) || runningExecutable().filename() != L"Tempos.UpdateTestRunner.exe") throw std::runtime_error("test update isolation");
#else
  if (runningExecutable().filename() != L"Tempos.UpdateRunner.exe") throw std::runtime_error("update runner");
#endif
  return j;
}
} // namespace
std::filesystem::path runningExecutable() {
  wchar_t path[32768]{};
  DWORD count = GetModuleFileNameW(nullptr, path, DWORD(std::size(path)));
  if (!count || count >= std::size(path)) throw std::runtime_error("executable path");
  return std::filesystem::path(path);
}
bool installedUpdateTarget(const std::filesystem::path &app) {
#ifdef TEMPOS_TEST_HOST
  return app.filename() == L"Tempos.TestHost.exe";
#else
  wchar_t path[32768]{}; DWORD bytes = sizeof path;
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Tempos\\Installer", L"InstallLocation",
                  RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, path, &bytes) != ERROR_SUCCESS || !path[0]) return false;
  return samePath(app, std::filesystem::path(path) / L"Tempos.exe");
#endif
}
bool beginUpdateInstall(const UpdateState &state, const std::filesystem::path &root, std::wstring &error) {
  error.clear();
  try {
    auto app = runningExecutable();
    if (!installedUpdateTarget(app) || state.phase != UpdatePhase::Ready || !state.release ||
        !samePath(state.installer.parent_path().parent_path(), root / L"updates") ||
        !verifyUpdateFile(state.installer, *state.release)) throw std::runtime_error("update not ready");
#ifdef TEMPOS_TEST_HOST
    if (!isolatedTestProfile(root)) throw std::runtime_error("test root");
    auto runner = state.installer.parent_path() / L"Tempos.UpdateTestRunner.exe";
#else
    auto runner = state.installer.parent_path() / L"Tempos.UpdateRunner.exe";
#endif
    if (!CopyFileW(app.c_str(), runner.c_str(), TRUE)) throw std::runtime_error("copy runner");
#ifdef TEMPOS_TEST_HOST
    auto asan = app.parent_path() / L"clang_rt.asan_dynamic-x86_64.dll";
    if (std::filesystem::exists(asan) && !CopyFileW(asan.c_str(), (runner.parent_path() / asan.filename()).c_str(), TRUE))
      throw std::runtime_error("copy test runtime");
#endif
    auto eventName = L"Local\\Tempos.Update." + newId();
    Handle ready(CreateEventW(nullptr, TRUE, FALSE, (eventName + L".Ready").c_str()));
    Handle proceed(CreateEventW(nullptr, TRUE, FALSE, (eventName + L".Proceed").c_str()));
    if (!ready.value || !proceed.value) throw std::runtime_error("update event");
    Json job = {{"schema", 1}, {"app", utf8(app.wstring())}, {"dataRoot", utf8(root.wstring())},
        {"parentPid", GetCurrentProcessId()}, {"parentCreated", createdAt(GetCurrentProcess())},
        {"event", utf8(eventName)}, {"version", state.release->version}, {"sha256", state.release->sha256},
        {"bytes", state.release->bytes}, {"installer", utf8(state.installer.filename().wstring())}};
    auto jobFile = runner.parent_path() / L"job.json";
    if (!atomicWrite(jobFile, job.dump(2))) throw std::runtime_error("write job");
    PROCESS_INFORMATION process{};
    if (!start(runner, L"--apply-update " + quote(jobFile), process)) throw std::runtime_error("start runner");
    Handle processHandle(process.hProcess), thread(process.hThread);
    HANDLE waits[] = {ready.value, process.hProcess};
    if (WaitForMultipleObjects(2, waits, FALSE, 10000) != WAIT_OBJECT_0) throw std::runtime_error("runner readiness");
    if (!SetEvent(proceed.value)) throw std::runtime_error("runner handoff");
    error.clear(); return true;
  } catch (...) {
    error = L"설치를 시작하지 못했습니다. 현재 앱을 유지합니다. 다시 시도하거나 릴리즈 페이지에서 설치 파일을 받아 주세요.";
    return false;
  }
}
#ifdef TEMPOS_TEST_HOST
int runMockUpdateInstaller(const std::filesystem::path &file) {
  try {
    auto job = readJob(file);
    auto root = std::filesystem::path(wide(job.at("dataRoot").get<std::string>()));
    auto options = boundedRead(root / L"update-scenario.json");
    auto mode = options ? Json::parse(*options).value("installerExit", 0) : 0;
    atomicWrite(file.parent_path() / L"mock-installer.json", Json{{"mock", true}, {"exit", mode}, {"pid", GetCurrentProcessId()}}.dump());
    return mode;
  } catch (...) { return 65; }
}
#endif
int runUpdateHelper(const std::filesystem::path &file) {
  try {
    auto job = readJob(file);
    auto root = std::filesystem::path(wide(job.at("dataRoot").get<std::string>()));
    auto app = std::filesystem::path(wide(job.at("app").get<std::string>()));
    if (!installedUpdateTarget(app)) return 65;
    auto eventName = wide(job.at("event").get<std::string>());
    if (!eventName.starts_with(L"Local\\Tempos.Update.") || eventName.size() > 100) return 65;
    Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, (eventName + L".Ready").c_str()));
    Handle proceed(OpenEventW(SYNCHRONIZE, FALSE, (eventName + L".Proceed").c_str()));
    Handle parent(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, job.at("parentPid").get<DWORD>()));
    if (!ready.value || !proceed.value || !parent.value || createdAt(parent.value) != job.at("parentCreated").get<uint64_t>()) return 65;
    wchar_t parentPath[32768]{}; DWORD chars = DWORD(std::size(parentPath));
    if (!QueryFullProcessImageNameW(parent.value, 0, parentPath, &chars) || !samePath(parentPath, app)) return 65;
    UpdateRelease release; release.version = job.at("version").get<std::string>();
    release.sha256 = job.at("sha256").get<std::string>(); release.bytes = job.at("bytes").get<uint64_t>();
    release.download = L"verified";
    if (compareVersions(release.version, TEMPOS_VERSION_STRING) <= 0 || release.bytes > maxUpdateBytes) return 65;
    auto name = wide("Tempos-" + release.version + "-win-x64.exe");
    if (job.at("installer").get<std::string>() != utf8(name)) return 65;
    auto installer = file.parent_path() / name;
    // Deny replacement/writes between the second integrity check and installer completion.
    Handle locked(CreateFileW(installer.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (locked.value == INVALID_HANDLE_VALUE || !verifyUpdateFile(installer, release)) return 65;
    if (!SetEvent(ready.value) || WaitForSingleObject(proceed.value, 15000) != WAIT_OBJECT_0) return 66;
    if (WaitForSingleObject(parent.value, 60000) != WAIT_OBJECT_0) return 66;
    DWORD exitCode = 1;
    PROCESS_INFORMATION installation{};
#ifdef TEMPOS_TEST_HOST
    bool launched = start(runningExecutable(), L"--mock-update-installer " + quote(file), installation);
#else
    bool launched = start(installer, L"/S /D=" + app.parent_path().wstring(), installation);
#endif
    bool timedOut = false;
    if (launched) {
      Handle process(installation.hProcess), thread(installation.hThread);
      timedOut = WaitForSingleObject(process.value, 15 * 60 * 1000) != WAIT_OBJECT_0;
      if (!timedOut) GetExitCodeProcess(process.value, &exitCode);
    }
#ifndef TEMPOS_TEST_HOST
    if (launched && !timedOut && exitCode == 0 && !installedVersionMatches(app, release.version)) exitCode = ERROR_REVISION_MISMATCH;
#endif
    const bool success = launched && !timedOut && exitCode == 0;
    CloseHandle(locked.value); locked.value = nullptr;
    if (!timedOut) DeleteFileW(installer.c_str());
    Json result = {{"version", release.version}, {"success", success}, {"exit", exitCode},
        {"timedOut", timedOut}, {"finishedAt", nowUnix()}, {"helperPid", GetCurrentProcessId()},
        {"message", success ? "업데이트 설치를 완료했습니다." : "업데이트 설치를 완료하지 못했습니다. 현재 버전을 확인하고 다시 시도해 주세요."},
        {"restartedPid", 0}, {"jobFolder", utf8(file.parent_path().wstring())}};
#ifdef TEMPOS_TEST_HOST
    result["installerMode"] = "mock";
#else
    result["installerMode"] = "real";
#endif
    atomicWrite(root / L"update-result.json", result.dump(2));
    if (timedOut)
      MessageBoxW(nullptr, L"설치가 아직 끝나지 않아 Tempos를 자동 실행하지 않았습니다. 설치가 끝난 뒤 시작 메뉴에서 실행해 주세요.", L"Tempos 업데이트", MB_OK | MB_ICONINFORMATION);
    // Do not run the app while a timed-out installer might still replace its files.
    if (!timedOut) {
      PROCESS_INFORMATION restarted{};
      if (start(app, L"--settings --after-update --data-dir " + quote(root), restarted)) {
        Handle process(restarted.hProcess), thread(restarted.hThread);
        result["restartedPid"] = restarted.dwProcessId;
        atomicWrite(root / L"update-result.json", result.dump(2));
      } else {
        MessageBoxW(nullptr, L"Tempos를 다시 시작하지 못했습니다. 시작 메뉴에서 실행하거나 릴리즈 페이지의 설치 파일을 다시 실행해 주세요.", L"Tempos 업데이트", MB_OK | MB_ICONERROR);
      }
    }
    return success ? 0 : 1;
  } catch (...) { return 65; }
}
} // namespace tempos
