#include "update_install.h"
#include "startup.h"
#include "test_isolation.h"
#include <set>

namespace tempos {
namespace fs = std::filesystem;
namespace {
#ifdef TEMPOS_TEST_HOST
constexpr auto runnerName = L"Tempos.UpdateTestRunner.exe";
constexpr auto appName = L"Tempos.TestHost.exe";
#else
constexpr auto runnerName = L"Tempos.UpdateRunner.exe";
constexpr auto appName = L"Tempos.exe";
#endif
constexpr auto uninstallName = L"Uninstall Tempos.exe";
constexpr auto registration = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Tempos";
struct Handle {
  HANDLE value{};
  explicit Handle(HANDLE h = nullptr) : value(h) {}
  ~Handle() { reset(); }
  void reset() {
    if (value && value != INVALID_HANDLE_VALUE)
      CloseHandle(value);
    value = nullptr;
  }
  Handle(const Handle &) = delete;
  Handle &operator=(const Handle &) = delete;
};
void require(bool ok, const char *reason) {
  if (!ok)
    throw std::runtime_error(reason);
}
void save(const fs::path &p, const std::string &s) {
  require(atomicWrite(p, s), "save update transaction");
}
bool same(const fs::path &a, const fs::path &b) {
  return _wcsicmp(fs::weakly_canonical(a).c_str(), fs::weakly_canonical(b).c_str()) == 0;
}
std::wstring quote(const fs::path &p) {
  return startupCommand(p.wstring());
}
uint64_t created(HANDLE h) {
  FILETIME c{}, e{}, k{}, u{};
  if (!GetProcessTimes(h, &c, &e, &k, &u))
    return 0;
  return (uint64_t(c.dwHighDateTime) << 32) | c.dwLowDateTime;
}
bool processMatches(HANDLE h, const fs::path &app, uint64_t time) {
  wchar_t path[32768]{};
  DWORD n = DWORD(std::size(path));
  return h && created(h) == time && QueryFullProcessImageNameW(h, 0, path, &n) && same(path, app);
}
HANDLE launch(const fs::path &exe, std::wstring args, DWORD *pid = nullptr) {
  auto cmd = quote(exe) + L" " + args;
  STARTUPINFOW si{sizeof si};
  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                      exe.parent_path().c_str(), &si, &pi))
    return nullptr;
  CloseHandle(pi.hThread);
  if (pid)
    *pid = pi.dwProcessId;
  return pi.hProcess;
}
std::string fileVersion(const fs::path &exe) {
  DWORD ignored{};
  auto size = GetFileVersionInfoSizeW(exe.c_str(), &ignored);
  require(size && size < 1024 * 1024, "file version");
  std::vector<char> bytes(size);
  require(GetFileVersionInfoW(exe.c_str(), 0, size, bytes.data()), "file version");
  VS_FIXEDFILEINFO *info{};
  UINT n{};
  require(VerQueryValueW(bytes.data(), L"\\", reinterpret_cast<void **>(&info), &n) && n >= sizeof *info &&
              info->dwSignature == 0xfeef04bd && !LOWORD(info->dwFileVersionLS),
          "invalid executable version");
  return std::to_string(HIWORD(info->dwFileVersionMS)) + "." + std::to_string(LOWORD(info->dwFileVersionMS)) +
         "." + std::to_string(HIWORD(info->dwFileVersionLS));
}
std::wstring mutexName(const fs::path &root) {
#ifdef TEMPOS_TEST_HOST
  return L"Local\\Tempos.Test.Update." +
         std::to_wstring(std::hash<std::wstring>{}(testProfileIdentity(root)));
#else
  (void)root;
  return L"Local\\Tempos.Installer";
#endif
}
bool validTarget(const fs::path &app, const fs::path &root) {
#ifdef TEMPOS_TEST_HOST
  return isolatedTestProfile(root) && same(app, root / L"installed" / appName);
#else
  (void)root;
  return installedUpdateTarget(app);
#endif
}
struct Job {
  fs::path dir, root, app;
  std::wstring id, token;
  std::string from, to;
  Json request;
  fs::path target() const { return app.parent_path(); }
  fs::path stage() const { return dir / L"stage"; }
  fs::path backup() const { return dir / L"backup"; }
};
Job load(const fs::path &dir) {
  Job j;
  j.dir = dir;
  j.id = dir.filename().wstring();
  j.root = dir.parent_path().parent_path();
  require(validJobId(j.id) && same(dir.parent_path(), j.root / L"updates"), "invalid transaction");
  checkUpdatePath(j.root, dir / L"job.json");
  auto data = boundedRead(dir / L"job.json", 16384);
  require(bool(data), "missing transaction");
  j.request = Json::parse(*data);
  j.app = wide(j.request.at("app").get<std::string>());
  j.token = wide(j.request.at("token").get<std::string>());
  j.from = j.request.at("previous");
  j.to = j.request.at("version");
  require(j.request.at("schema") == 2 && validJobId(j.token) && validTarget(j.app, j.root) &&
              compareVersions(j.to, j.from) > 0,
          "invalid update target");
  checkUpdatePath(j.target(), j.app);
  return j;
}
void publishActiveRoot(const fs::path &root, bool active) {
#ifndef TEMPOS_TEST_HOST
  constexpr auto key = L"Software\\Tempos\\Installer";
  if (active)
    require(RegSetKeyValueW(HKEY_CURRENT_USER, key, L"ActiveUpdateRoot", REG_SZ, root.c_str(),
                            DWORD((root.native().size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS,
            "persist active update root");
  else {
    auto status = RegDeleteKeyValueW(HKEY_CURRENT_USER, key, L"ActiveUpdateRoot");
    require(status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND, "clear active update root");
  }
#else
  (void)root;
  (void)active;
#endif
}
fs::path pending(const fs::path &root) {
  auto id = boundedRead(root / L"update-pending", 128);
  if (!id)
    return {};
  require(validJobId(wide(*id)), "invalid pending transaction");
  return root / L"updates" / wide(*id);
}
void atomicCopy(const fs::path &source, const fs::path &dest, const Job &j) {
  fs::create_directories(dest.parent_path());
  auto temp = dest;
  temp += L".tempos-" + j.id;
  checkUpdatePath(dest.parent_path(), temp);
  require(CopyFileW(source.c_str(), temp.c_str(), FALSE), "copy update file");
  if (!MoveFileExW(temp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(temp.c_str());
    throw std::runtime_error("replace update file");
  }
}
FileInventory managed(const fs::path &dir) {
  auto files = readInventory(dir);
  require(files.contains(appName), "missing application in inventory");
#ifndef TEMPOS_TEST_HOST
  require(files.contains(uninstallName), "missing current uninstaller");
#endif
  return files;
}
uint64_t sizeOf(const fs::path &dir, const FileInventory &files) {
  uint64_t n = 0;
  for (auto &[name, hash] : files)
    n += fs::file_size(dir / name);
  return n;
}
void setRegistration(const Job &j, std::string version, DWORD kib) {
#ifdef TEMPOS_TEST_HOST
  save(j.root / L"test-uninstall.json", Json{{"version", version}, {"kib", kib}}.dump());
#else
  HKEY key{};
  require(RegOpenKeyExW(HKEY_CURRENT_USER, registration, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &key) ==
              ERROR_SUCCESS,
          "open uninstall registration");
  auto v = wide(version);
  auto a = RegSetValueExW(key, L"DisplayVersion", 0, REG_SZ, reinterpret_cast<const BYTE *>(v.c_str()),
                          DWORD((v.size() + 1) * sizeof(wchar_t)));
  auto b =
      RegSetValueExW(key, L"EstimatedSize", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&kib), sizeof kib);
  RegCloseKey(key);
  require(a == ERROR_SUCCESS && b == ERROR_SUCCESS, "write uninstall registration");
#endif
}
void validateTargetFiles(const Job &j, const FileInventory &old, const FileInventory &next) {
  for (auto &[name, hash] : old) {
    auto p = j.target() / name;
    checkUpdatePath(j.target(), p);
    require(fileSha256(p) == hash, "installed file changed or missing");
  }
  for (auto &[name, hash] : next) {
    auto p = j.target() / name;
    checkUpdatePath(j.target(), p);
    require(old.contains(name) || !fs::exists(p), "unmanaged file collision");
    for (auto d = p.parent_path(); d != j.target(); d = d.parent_path())
      require(!fs::exists(d) || fs::is_directory(d), "unmanaged directory collision");
    auto tmp = p;
    tmp += L".tempos-" + j.id;
    require(!fs::exists(tmp), "temporary filename collision");
  }
}
void backup(const Job &j, const FileInventory &old) {
  require(fs::create_directory(j.backup()), "backup already exists");
  for (auto &[name, hash] : old) {
    auto out = j.backup() / name;
    fs::create_directories(out.parent_path());
    fs::copy_file(j.target() / name, out);
    require(fileSha256(out) == hash, "backup mismatch");
  }
  fs::create_directory(j.dir / L"profile");
  for (auto name : {L"settings.json", L"settings.backup.json"}) {
    checkUpdatePath(j.root, j.root / name);
    if (fs::exists(j.root / name))
      fs::copy_file(j.root / name, j.dir / L"profile" / name);
  }
}
void replace(const Job &j, const fs::path &source, const FileInventory &want, const FileInventory &previous) {
  for (auto &[name, hash] : want) {
    checkUpdatePath(source, source / name);
    checkUpdatePath(j.target(), j.target() / name);
    require(fileSha256(source / name) == hash, "update source hash");
    if (fileSha256(j.target() / name) != hash)
      atomicCopy(source / name, j.target() / name, j);
  }
  for (auto &[name, hash] : previous) {
    if (want.contains(name))
      continue;
    auto file = j.target() / name;
    checkUpdatePath(j.target(), file);
    if (fs::exists(file)) {
      require(fileSha256(file) == hash, "refuse to remove changed file");
      fs::remove(file);
    }
    for (auto d = file.parent_path(); d != j.target() && fs::is_directory(d) && fs::is_empty(d);
         d = d.parent_path())
      fs::remove(d);
  }
}
void rollback(const Job &j) {
  auto previous = managed(j.backup()), next = managed(j.stage());
  replace(j, j.backup(), previous, next);
  for (auto name : {L"settings.json", L"settings.backup.json"}) {
    auto from = j.dir / L"profile" / name, to = j.root / name;
    checkUpdatePath(j.root, to);
    if (fs::exists(from)) {
      if (fileSha256(from) != fileSha256(to))
        atomicCopy(from, to, j);
    } else if (fs::exists(to))
      fs::remove(to);
  }
  setRegistration(j, j.from, DWORD((sizeOf(j.backup(), previous) + 1023) / 1024));
}
void result(const Job &j, bool ok, DWORD pid) {
  auto message = ok ? "새 버전의 시작을 확인하고 업데이트를 완료했습니다."
                    : "업데이트를 완료하지 못했습니다. 이전 버전 " + j.from + "을 복원했습니다.";
  save(j.root / L"update-result.json", Json{{"previous", j.from},
                                            {"version", j.to},
                                            {"success", ok},
                                            {"message", message},
                                            {"restartedPid", pid},
                                            {"finishedAt", nowUnix()},
                                            {"jobFolder", utf8(j.dir.wstring())}}
                                           .dump());
}
void finish(const Job &j, bool ok, DWORD pid) {
  result(j, ok, pid);
  save(j.dir / L"phase", ok ? "complete" : "restored");
  fs::remove(j.root / L"update-ready");
  publishActiveRoot(j.root, false);
  fs::remove(j.root / L"update-pending");
  for (auto dir : {j.stage(), j.backup(), j.dir / L"profile"}) {
    checkUpdatePath(j.dir, dir);
    std::error_code ignored;
    fs::remove_all(dir, ignored);
  }
}
std::wstring restartArgs(const Job &j) {
  return L"--settings --after-update --data-dir " + quote(j.root);
}
void restoreAndRestart(const Job &j, Handle &lock) {
  auto phase = boundedRead(j.dir / L"phase", 64).value_or("");
  if (phase == "applying")
    rollback(j);
  finish(j, false, 0);
  lock.reset();
  DWORD pid{};
  Handle process(launch(j.app, restartArgs(j), &pid));
  result(j, false, pid);
}
std::wstring healthToken;
} // namespace
fs::path runningExecutable() {
  wchar_t path[32768]{};
  auto n = GetModuleFileNameW(nullptr, path, DWORD(std::size(path)));
  require(n && n < std::size(path), "executable path");
  return path;
}
std::string updatePublicKey() {
#ifdef TEMPOS_TEST_HOST
  return "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a";
#else
  return TEMPOS_UPDATE_PUBLIC_KEY;
#endif
}
bool installedUpdateTarget(const fs::path &app) {
#ifdef TEMPOS_TEST_HOST
  return app.filename() == appName;
#else
  wchar_t path[32768]{};
  DWORD n = sizeof path;
  return RegGetValueW(HKEY_CURRENT_USER, L"Software\\Tempos\\Installer", L"InstallLocation",
                      RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, path, &n) == ERROR_SUCCESS &&
         path[0] && same(app, fs::path(path) / appName);
#endif
}
bool beginUpdateInstall(const UpdateState &state, const fs::path &root, std::wstring &error) {
  try {
    auto app = runningExecutable(), dir = state.installer.parent_path();
    require(validTarget(app, root) && state.phase == UpdatePhase::Ready && state.release &&
                same(dir.parent_path(), root / L"updates") && validJobId(dir.filename().wstring()) &&
                pending(root).empty(),
            "update not ready");
    checkUpdatePath(root, dir);
    auto runner = dir / runnerName;
    require(CopyFileW(app.c_str(), runner.c_str(), FALSE), "copy updater");
#ifdef TEMPOS_TEST_HOST
    auto dll = app.parent_path() / L"clang_rt.asan_dynamic-x86_64.dll";
    if (fs::exists(dll))
      fs::copy_file(dll, dir / dll.filename(), fs::copy_options::overwrite_existing);
#endif
    auto token = newId(), event = L"Local\\Tempos.Update." + token;
    Handle ready(CreateEventW(nullptr, TRUE, FALSE, (event + L".Ready").c_str()));
    Handle go(CreateEventW(nullptr, TRUE, FALSE, (event + L".Go").c_str()));
    require(ready.value && go.value, "update handshake");
    save(dir / L"job.json", Json{{"schema", 2},
                                 {"app", utf8(app.wstring())},
                                 {"previous", TEMPOS_VERSION_STRING},
                                 {"version", state.release->version},
                                 {"token", utf8(token)},
                                 {"pid", GetCurrentProcessId()},
                                 {"created", created(GetCurrentProcess())}}
                                .dump());
    Handle helper(launch(runner, L"--apply-update " + quote(dir / L"job.json")));
    HANDLE handles[] = {ready.value, helper.value};
    require(helper.value && WaitForMultipleObjects(2, handles, FALSE, 120000) == WAIT_OBJECT_0 &&
                SetEvent(go.value),
            "updater preflight");
    return true;
  } catch (...) {
    error = L"패치를 준비하지 못했습니다. 설치 폴더의 파일·여유 공간을 확인하거나 변경 내역에서 수동 "
            L"다운로드해 주세요.";
    return false;
  }
}
bool resumeUpdate(const fs::path &root, std::wstring_view token) {
  auto activeRoot = root;
#ifndef TEMPOS_TEST_HOST
  wchar_t registeredRoot[32768]{};
  DWORD n = sizeof registeredRoot;
  if (installedUpdateTarget(runningExecutable()) &&
      RegGetValueW(HKEY_CURRENT_USER, L"Software\\Tempos\\Installer", L"ActiveUpdateRoot",
                   RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, registeredRoot, &n) == ERROR_SUCCESS &&
      registeredRoot[0])
    activeRoot = registeredRoot;
#endif
  auto dir = pending(activeRoot);
  if (dir.empty()) {
    // Windows keeps the helper executable locked until it exits.
    if (auto last = boundedRead(root / L"update-result.json", 16384)) {
      try {
        auto previous = fs::path(wide(Json::parse(*last).at("jobFolder").get<std::string>()));
        if (validJobId(previous.filename().wstring()) && same(previous.parent_path(), root / L"updates")) {
          checkUpdatePath(root, previous);
          auto phase = boundedRead(previous / L"phase", 64);
          if (phase == "complete" || phase == "restored") {
            std::error_code ec;
            if (fs::remove(previous / runnerName, ec) || !fs::exists(previous / runnerName))
              fs::remove_all(previous, ec);
          }
        }
      } catch (...) { /* Retry cleanup at a later startup. */
      }
    }
    return false;
  }
  auto j = load(dir);
  require(same(j.app, runningExecutable()), "wrong recovery target");
  if (!token.empty() && token == j.token && j.to == TEMPOS_VERSION_STRING) {
    healthToken = token;
    return false;
  }
  Handle updating(OpenMutexW(SYNCHRONIZE, FALSE, mutexName(root).c_str()));
  if (updating.value)
    return true;
  // Launch only bytes copied from the currently executing, installed application.
  require(CopyFileW(runningExecutable().c_str(), (dir / runnerName).c_str(), FALSE),
          "refresh recovery helper");
  save(dir / L"recovery-parent.json",
       Json{{"pid", GetCurrentProcessId()}, {"created", created(GetCurrentProcess())}}.dump());
  Handle helper(launch(dir / runnerName, L"--recover-update " + quote(dir / L"job.json")));
  require(helper.value != nullptr, "cannot recover transaction");
  return true;
}
bool acknowledgeUpdate(const fs::path &root, bool healthy) {
  if (healthToken.empty())
    return true;
  try {
    auto j = load(pending(root));
    if (!healthy || j.token != healthToken || j.to != TEMPOS_VERSION_STRING)
      return false;
#ifdef TEMPOS_TEST_HOST
    if (auto scenario = boundedRead(root / L"update-scenario.json", 16384);
        scenario && Json::parse(*scenario).value("installerExit", 0) != 0)
      return false;
#endif
    save(j.dir / L"healthy", utf8(healthToken));
    healthToken.clear();
    return true;
  } catch (...) {
    return false;
  }
}
int runUpdateHelper(const fs::path &file, bool recover) {
  try {
    auto dir = file.parent_path();
    auto j = load(dir);
    require(file.filename() == L"job.json" && same(runningExecutable(), dir / runnerName),
            "updater location");
    Handle lock(CreateMutexW(nullptr, FALSE, mutexName(j.root).c_str()));
    bool existing = GetLastError() == ERROR_ALREADY_EXISTS;
    require(lock.value && !existing, "concurrent update");
    if (recover) {
      require(same(pending(j.root), dir), "wrong recovery");
      // Windows can keep the executable mapped after the desktop mutex is released.
      if (auto bytes = boundedRead(dir / L"recovery-parent.json", 1024)) {
        auto initiator = Json::parse(*bytes);
        Handle process(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, initiator.at("pid").get<DWORD>()));
        if (processMatches(process.value, j.app, initiator.at("created")))
          require(WaitForSingleObject(process.value, 60000) == WAIT_OBJECT_0, "recovery requester still running");
      }
      auto desktop = std::wstring(L"Local\\Tempos.Desktop");
#ifdef TEMPOS_TEST_HOST
      desktop += L".Test." + std::to_wstring(std::hash<std::wstring>{}(testProfileIdentity(j.root)));
#endif
      for (int i = 0; i < 100; ++i) {
        Handle app(OpenMutexW(SYNCHRONIZE, FALSE, desktop.c_str()));
        if (!app.value)
          break;
        Sleep(50);
      }
      Handle app(OpenMutexW(SYNCHRONIZE, FALSE, desktop.c_str()));
      require(!app.value, "app still running");
      if (boundedRead(dir / L"healthy", 128) == utf8(j.token)) {
        finish(j, true, 0);
        lock.reset();
        Handle started(launch(j.app, restartArgs(j)));
        return started.value ? 0 : 1;
      }
      restoreAndRestart(j, lock);
      return 0;
    }
    auto event = L"Local\\Tempos.Update." + j.token;
    Handle ready(OpenEventW(EVENT_MODIFY_STATE, FALSE, (event + L".Ready").c_str()));
    Handle go(OpenEventW(SYNCHRONIZE, FALSE, (event + L".Go").c_str()));
    Handle parent(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                              j.request.at("pid").get<DWORD>()));
    require(ready.value && go.value && processMatches(parent.value, j.app, j.request.at("created")) &&
                fileVersion(j.app) == j.from && j.from == TEMPOS_VERSION_STRING,
            "invalid update parent");
    auto package = readSignedPackage(dir, updatePublicKey());
    auto zip = dir / wide(package.name);
    Handle locked(CreateFileW(zip.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr));
    require(locked.value != INVALID_HANDLE_VALUE && package.version == j.to &&
                fs::file_size(zip) == package.bytes && fileSha256(zip) == package.sha256,
            "invalid update package");
    // A failed preflight can be retried. Only this verified transaction owns stage.
    if (fs::exists(j.stage())) {
      checkUpdatePath(dir, j.stage());
      fs::remove_all(j.stage());
    }
    auto next = unpackUpdate(zip, j.stage()), old = managed(j.target());
    require(fileVersion(j.stage() / appName) == j.to, "wrong staged executable");
    validateTargetFiles(j, old, next);
    auto oldSize = sizeOf(j.target(), old), newSize = sizeOf(j.stage(), next);
    require(fs::space(dir).available >= oldSize + 16 * 1024 * 1024 &&
                fs::space(j.target()).available >= newSize,
            "insufficient space");
    save(dir / L"phase", "prepared");
    require(SetEvent(ready.value) && WaitForSingleObject(go.value, 15000) == WAIT_OBJECT_0,
            "handoff cancelled");
    save(j.root / L"update-pending", utf8(j.id));
    publishActiveRoot(j.root, true);
    require(WaitForSingleObject(parent.value, 60000) == WAIT_OBJECT_0, "parent still running");
    DWORD pid{};
    try {
      validateTargetFiles(j, old, next);
      backup(j, old);
      save(dir / L"phase", "applying");
      replace(j, j.stage(), next, old);
#ifdef TEMPOS_TEST_HOST
      if (auto scenario = boundedRead(j.root / L"update-scenario.json", 16384);
          scenario && Json::parse(*scenario).value("installerExit", 0) == 17) {
        save(j.root / L"test-update-interrupted", "1");
        ExitProcess(17);
      }
#endif
      setRegistration(j, j.to, DWORD((newSize + 1023) / 1024));
      Handle child(launch(j.app, restartArgs(j) + L" --update-token " + quote(j.token), &pid));
      require(child.value != nullptr, "launch updated app");
      auto deadline = Clock::now() + 60s;
      bool reported = false;
      for (;;) {
        if (!reported && Clock::now() >= deadline) {
          save(j.root / L"update-result.json",
               Json{{"previous", j.from},
                    {"version", j.to},
                    {"success", false},
                    {"message",
                     "새 앱의 시작 확인을 기다리고 있습니다. 앱을 종료하면 이전 버전을 복원합니다."}}
                   .dump());
          reported = true;
        }
        if (boundedRead(dir / L"healthy", 128) == utf8(j.token))
          break;
        if (WaitForSingleObject(child.value, 100) == WAIT_OBJECT_0) {
          require(boundedRead(dir / L"healthy", 128) == utf8(j.token), "updated app exited before ready");
          break;
        }
      }
    } catch (...) {
      restoreAndRestart(j, lock);
      return 1;
    }
    finish(j, true, pid);
    PostMessageW(FindWindowW(managementWindowClass(j.root).c_str(), nullptr), WM_APP + 6, 0, 0);
    return 0;
  } catch (const std::exception &e) {
    if (file.filename() == L"job.json" && same(file.parent_path(), runningExecutable().parent_path()) &&
        validJobId(file.parent_path().filename().wstring()))
      atomicWrite(file.parent_path() / L"helper-error.txt", e.what());
    return 65;
  }
}
} // namespace tempos
