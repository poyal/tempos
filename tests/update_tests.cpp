#include "updates.h"
#include "api_fixture.h"
#include <fstream>
using namespace tempos;
extern void check(bool, const char *);
void updateTests() {
  check(compareVersions("0.1.10", "0.1.9") > 0 && compareVersions("2.0.0", "10.0.0") < 0 && compareVersions("1.2.3", "1.2.3") == 0,
        "Update versions compare numeric components without lexical ordering");
  for (auto invalid : {"", "1.2", "1.2.3.4", "01.2.3", "1.-2.3", "1.2.3-beta", "1.2.3+build", "1.2.9999999999", "+1.2.3", "1.2.3/evil", "1.2.3\n"}) {
    bool rejected = false; try { compareVersions(invalid, "0.1.3"); } catch (...) { rejected = true; }
    check(rejected, "Malformed or nonstable update version rejected");
  }
  for (auto url : {L"http://github.com/poyal/tempos/releases/download/v1/a.exe", L"https://github.com.evil/poyal/tempos/releases/download/v1/a.exe",
      L"https://github.com@evil/poyal/tempos/releases/download/v1/a.exe", L"https://github.com/other/repo/a.exe",
      L"https://release-assets.githubusercontent.com:443/github-production-release-asset/a", L"file:///C:/bad.exe",
      L"https://github.com/poyal/tempos/releases/download/a\r\nx", L"https://github.com/poyal/tempos/releases/download/a#b"})
    check(!updateDownloadAddress(url), "Update redirects reject unsafe schemes, hosts, ports, credentials and fragments");
  check(bool(updateDownloadAddress(L"https://release-assets.githubusercontent.com/github-production-release-asset/a?token=sample")),
        "Pinned GitHub asset CDN HTTPS redirect accepted");
  auto root = std::filesystem::temp_directory_path() / (L"tempos-updates-" + newId());
  std::filesystem::create_directories(root);
  auto sample = root / L"hash-test.txt";
  atomicWrite(sample, "abc");
  check(fileSha256(sample) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "Installer SHA-256 matches independent standard vector");
  HttpClient http(mockApi(root));
  UpdateService service(http, root, [] {}, "0.1.3", mockUpdateKey);
  auto scenario = [&](Json j) { check(atomicWrite(root / L"update-scenario.json", j.dump()), "Update HTTP scenario written"); };
  auto wait = [&] {
    auto until = Clock::now() + 5s;
    while (service.state().busy() && Clock::now() < until) std::this_thread::sleep_for(10ms);
    check(!service.state().busy(), "Asynchronous update work completes");
    return service.state();
  };
  scenario({{"version", "0.1.3"}}); service.check(); check(wait().phase == UpdatePhase::Current, "Same release never installs");
  scenario({{"version", "0.1.2"}}); service.check(); check(wait().phase == UpdatePhase::Current && !service.download(), "Older release never downgrades");
  scenario({{"version", "0.1.10"}, {"check", "slow"}}); service.check();
  check(!service.check() && !service.download(), "Duplicate checks and downloads coalesce while busy");
  service.cancel(); check(wait().phase == UpdatePhase::Idle, "Pending check cancellation preserves the current app");
  for (auto mode : {"draft", "prerelease", "malformed", "quota", "timeout", "server-error"}) {
    scenario({{"version", "0.1.10"}, {"check", mode}}); service.check();
    auto state = wait(); check(state.phase == UpdatePhase::Error && !state.release && !service.download(), "Invalid or failed release check cannot install stale metadata");
  }
  for (auto mode : {"missing", "bad-url", "bad-digest"}) {
    scenario({{"version", "0.1.10"}, {"check", mode}}); service.check();
    auto state = wait(); check(state.phase == UpdatePhase::Available && !state.release->installable() && !service.download(), "Missing or untrusted installer keeps release notes but disables installation");
  }
  scenario({{"version", "0.1.10"}});
  auto raw = http.request(updateHost, updateFeed).body;
  auto valid = Json::parse(raw);
  for (int kind = 0; kind < 6; ++kind) {
    auto altered = valid;
    if (kind == 0) altered["assets"].push_back(altered["assets"][0]);
    if (kind == 1) altered["assets"][0]["size"] = maxUpdateBytes + 1;
    if (kind == 2) altered["assets"][0]["size"] = -1;
    if (kind == 3) altered["assets"][0]["size"] = 0;
    if (kind == 4) altered["assets"][0]["state"] = "new";
    if (kind == 5) altered["assets"][0]["digest"] = nullptr;
    bool safe = false; try { safe = !parseUpdateRelease(altered.dump()).installable(); } catch (...) { safe = true; }
    check(safe, "Duplicate, oversized, signed-negative, empty, unpublished or unhashed installer is not executable");
  }
  for (auto mode : {"bad-signature", "corrupt", "short", "oversize", "unsafe-redirect", "redirect-loop", "timeout", "server-error"}) {
    scenario({{"version", "0.1.10"}, {"download", mode}}); service.check(); wait();
    check(service.download(), "Trusted release starts a download");
    auto state = wait(); check(state.phase == UpdatePhase::Error && state.installer.empty(), "Failed, truncated, oversized, corrupt or redirected download is never ready to run");
    size_t executables = 0;
    for (auto &entry : std::filesystem::recursive_directory_iterator(root / L"updates")) if (entry.path().extension() == L".exe") ++executables;
    check(executables == 0, "Rejected update partial files are removed");
  }
  scenario({{"version", "0.1.10"}, {"download", "slow"}}); service.check(); wait(); service.download(); service.cancel();
  check(wait().phase == UpdatePhase::Available, "Download cancellation permits retry without closing the app");
  scenario({{"version", "0.1.10"}, {"download", "redirect"}}); service.download();
  auto ready = wait();
  check(ready.phase == UpdatePhase::Ready && verifyUpdateFile(ready.installer, *ready.release), "Trusted redirect, disk write, exact size and SHA-256 produce a verified installer");
  atomicWrite(ready.installer, "tampered");
  check(!verifyUpdateFile(ready.installer, *ready.release), "Second integrity check rejects tampering after download");
  bool called = false;
  HttpClient limited([&](auto &, auto &, auto &, auto &, auto &, auto) { called = true; return Response{200, 0, "abcd"}; });
  auto response = limited.request(updateHost, L"/unregistered");
  check(response.error == ERROR_ACCESS_DENIED && !called, "HTTP whitelist restricts GitHub metadata to this repository's release endpoint");
  response = limited.request(updateHost, updateFeed, L"GET", {}, {}, {}, [](auto) { return false; });
  check(response.error == ERROR_WRITE_FAULT, "Stream write failures propagate through HTTP boundary");
  service.stop();
  auto settings = readSettings(Json{{"version",1},{"widgets",Json::array()}});
  check(settings.checkUpdatesOnStart, "Existing settings enable one startup update check by default");
  settings.checkUpdatesOnStart = false;
  check(!readSettings(settingsJson(settings)).checkUpdatesOnStart && !settingsJson(settings, true).contains("checkUpdatesOnStart"), "Startup update preference persists locally and is excluded from layout export");
  // Delete exact files only; the temporary test root is never an installed profile.
  for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
    if (entry.is_regular_file()) { std::error_code ec; std::filesystem::remove(entry.path(), ec); }
}
