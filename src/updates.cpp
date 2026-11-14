#include "updates.h"
#include <bcrypt.h>
#include <charconv>
#include <fstream>
namespace tempos {
static std::array<unsigned, 3> versionParts(std::string_view value) {
  std::array<unsigned, 3> parts{};
  for (size_t i = 0; i < 3; ++i) {
    size_t end = value.find('.');
    auto part = value.substr(0, end);
    if (part.empty() || part.size() > 9 || (part.size() > 1 && part.front() == '0'))
      throw std::runtime_error("version");
    auto parsed = std::from_chars(part.data(), part.data() + part.size(), parts[i]);
    if (parsed.ec != std::errc{} || parsed.ptr != part.data() + part.size() ||
        (i < 2 ? end == value.npos : end != value.npos)) throw std::runtime_error("version");
    if (i < 2) value.remove_prefix(end + 1);
  }
  return parts;
}
int compareVersions(std::string_view left, std::string_view right) {
  auto a = versionParts(left), b = versionParts(right);
  return a < b ? -1 : a > b ? 1 : 0;
}
static bool digestValid(std::string_view value) {
  return value.size() == 64 && value.find_first_not_of("0123456789abcdefABCDEF") == value.npos;
}
UpdateRelease parseUpdateRelease(std::string_view text) {
  if (text.size() > 1024 * 1024) throw std::runtime_error("release too large");
  auto j = Json::parse(text);
  if (j.at("draft").get<bool>() || j.at("prerelease").get<bool>()) throw std::runtime_error("not stable");
  UpdateRelease r;
  r.tag = j.at("tag_name").get<std::string>();
  r.version = r.tag.starts_with('v') ? r.tag.substr(1) : r.tag;
  versionParts(r.version);
  r.page = L"https://github.com/poyal/tempos/releases/tag/" + wide(r.tag);
  const auto name = "Tempos-" + r.version + "-win-x64.exe";
  const auto expected = L"https://github.com/poyal/tempos/releases/download/" + wide(r.tag + "/" + name);
  const auto &assets = j.at("assets");
  if (!assets.is_array() || assets.size() > 100) throw std::runtime_error("assets");
  int matches = 0;
  for (const auto &a : assets) {
    if (a.value("name", std::string()) != name) continue;
    if (++matches > 1) throw std::runtime_error("ambiguous installer");
    if (a.value("state", std::string()) != "uploaded" || !a.contains("size") || !a["size"].is_number_unsigned()) continue;
    auto size = a["size"].get<uint64_t>();
    auto digest = a.contains("digest") && a["digest"].is_string() ? a["digest"].get<std::string>() : "";
    if (size == 0 || size > maxUpdateBytes || !digest.starts_with("sha256:") || !digestValid(digest.substr(7)) ||
        wide(a.value("browser_download_url", std::string())) != expected) continue;
    r.download = expected; r.bytes = size; r.sha256 = digest.substr(7);
    std::transform(r.sha256.begin(), r.sha256.end(), r.sha256.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  }
  return r;
}
std::optional<std::pair<std::wstring, std::wstring>> updateDownloadAddress(std::wstring_view url) {
  if (url.size() > 4095 || url.find_first_of(L"\r\n\t\\# ") != url.npos || !url.starts_with(L"https://")) return {};
  url.remove_prefix(8);
  auto slash = url.find('/');
  if (slash == url.npos) return {};
  auto host = url.substr(0, slash), path = url.substr(slash);
  if ((host == L"github.com" && path.starts_with(L"/poyal/tempos/releases/download/")) ||
      (host == L"release-assets.githubusercontent.com" && path.starts_with(L"/github-production-release-asset/")))
    return std::pair{std::wstring(host), std::wstring(path)};
  return {};
}
std::string fileSha256(const std::filesystem::path &file) {
  std::ifstream input(file, std::ios::binary);
  if (!input) return {};
  BCRYPT_HASH_HANDLE hash{};
  if (BCryptCreateHash(BCRYPT_SHA256_ALG_HANDLE, &hash, nullptr, 0, nullptr, 0, 0) < 0) return {};
  std::array<char, 65536> buffer{};
  bool ok = true;
  while (input) {
    input.read(buffer.data(), buffer.size());
    if (input.gcount() && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), ULONG(input.gcount()), 0) < 0) { ok = false; break; }
  }
  unsigned char digest[32]{};
  ok = ok && input.eof() && BCryptFinishHash(hash, digest, sizeof digest, 0) >= 0;
  BCryptDestroyHash(hash);
  if (!ok) return {};
  std::string result;
  for (auto c : digest) { result += "0123456789abcdef"[c >> 4]; result += "0123456789abcdef"[c & 15]; }
  return result;
}
bool verifyUpdateFile(const std::filesystem::path &file, const UpdateRelease &release) {
  std::error_code ec;
  auto bytes = std::filesystem::file_size(file, ec);
  return !ec && release.installable() && bytes == release.bytes && fileSha256(file) == release.sha256;
}
UpdateService::UpdateService(HttpClient &http, std::filesystem::path root, std::function<void()> notify,
                             std::string current) : http_(http), root_(std::move(root)), notify_(std::move(notify)), current_(std::move(current)) {}
UpdateService::~UpdateService() { stop(); }
void UpdateService::stop() { worker_.request_stop(); if (worker_.joinable()) worker_.join(); }
void UpdateService::cancel() { worker_.request_stop(); }
UpdateState UpdateService::state() const { std::lock_guard lock(mutex_); return state_; }
void UpdateService::publish(UpdateState value) {
  { std::lock_guard lock(mutex_); state_ = std::move(value); }
  if (notify_) notify_();
}
bool UpdateService::check() {
  auto next = state();
  if (next.busy()) return false;
  stop();
  next = {}; next.phase = UpdatePhase::Checking; next.message = L"새 버전을 확인하고 있습니다…";
  publish(next);
  worker_ = std::jthread([this, next](std::stop_token stop) mutable {
    try {
      auto response = http_.request(updateHost, updateFeed, L"GET", {},
          L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2026-03-10\r\n", stop);
      if (stop.stop_requested()) { next.phase = UpdatePhase::Idle; next.message = L"업데이트 확인을 취소했습니다."; }
      else if (!response.ok()) {
        next.phase = UpdatePhase::Error;
        next.message = response.status == 403 || response.status == 429
            ? L"GitHub 요청 한도에 도달했습니다. 잠시 후 다시 확인해 주세요."
            : L"업데이트를 확인하지 못했습니다. 인터넷 연결을 확인하고 다시 시도해 주세요.";
      } else {
        next.release = parseUpdateRelease(response.body);
        bool newer = compareVersions(next.release->version, current_) > 0;
        next.phase = newer ? UpdatePhase::Available : UpdatePhase::Current;
        next.message = newer ? L"새 버전 " + wide(next.release->version) + L"을 사용할 수 있습니다."
                             : L"현재 최신 버전을 사용하고 있습니다.";
        if (newer && !next.release->installable()) next.message += L" 자동 설치 파일은 아직 준비되지 않았습니다.";
      }
    } catch (...) { next.phase = UpdatePhase::Error; next.message = L"공개 버전 정보를 확인할 수 없습니다. 잠시 후 다시 시도해 주세요."; }
    next.checkedAt = nowUnix(); publish(next);
  });
  return true;
}
bool UpdateService::download() {
  auto next = state();
  if (next.busy() || !next.release || !next.release->installable() || compareVersions(next.release->version, current_) <= 0) return false;
  stop();
  next.phase = UpdatePhase::Downloading; next.received = 0; next.installer.clear(); next.message = L"업데이트를 다운로드하고 있습니다…";
  publish(next);
  worker_ = std::jthread([this, next](std::stop_token stop) mutable {
    std::filesystem::path file;
    try {
      auto folder = root_ / L"updates" / newId();
      std::filesystem::create_directories(folder);
      file = folder / wide("Tempos-" + next.release->version + "-win-x64.exe");
      std::ofstream output(file, std::ios::binary | std::ios::trunc);
      if (!output) throw std::runtime_error("create update");
      auto address = next.release->download;
      Response response;
      for (int redirect = 0; redirect < 4; ++redirect) {
        auto parsed = updateDownloadAddress(address);
        if (!parsed) throw std::runtime_error("update address");
        uint64_t lastNotify = 0;
        response = http_.request(parsed->first, parsed->second, L"GET", {}, L"Accept: application/octet-stream\r\n", stop,
            [&](std::string_view bytes) {
              output.write(bytes.data(), bytes.size()); next.received += bytes.size();
              if (next.received - lastNotify >= 512 * 1024) { lastNotify = next.received; publish(next); }
              return bool(output) && !stop.stop_requested();
            }, size_t(next.release->bytes), 10min);
        if (response.error || response.status < 300 || response.status >= 400) break;
        if (response.location.empty()) break;
        address = response.location;
      }
      output.close();
      if (stop.stop_requested()) throw std::runtime_error("cancelled");
      if (!response.ok() || !output || !verifyUpdateFile(file, *next.release)) throw std::runtime_error("download verification");
      next.phase = UpdatePhase::Ready; next.installer = file; next.message = L"다운로드와 파일 검증이 완료됐습니다. 설치를 준비합니다…";
    } catch (...) {
      if (!file.empty()) { std::error_code ec; std::filesystem::remove(file, ec); std::filesystem::remove(file.parent_path(), ec); }
      next.phase = stop.stop_requested() ? UpdatePhase::Available : UpdatePhase::Error;
      next.message = stop.stop_requested() ? L"다운로드를 취소했습니다. 현재 앱은 그대로 사용할 수 있습니다."
          : L"다운로드 또는 파일 검증에 실패했습니다. 현재 앱을 유지합니다. 다시 시도해 주세요.";
      next.installer.clear();
    }
    publish(next);
  });
  return true;
}
} // namespace tempos
