#pragma once
#include "http.h"
#include "storage.h"
#include "version.h"
#include "update_package.h"
namespace tempos {
inline constexpr wchar_t updateHost[] = L"api.github.com";
inline constexpr wchar_t updateFeed[] = L"/repos/poyal/tempos/releases/latest";
inline constexpr uint64_t maxUpdateBytes = 256ull * 1024 * 1024;
struct UpdateRelease {
  std::string version, tag, sha256;
  std::wstring page, download;
  uint64_t bytes{};
  bool metadata{};
  bool installable() const { return metadata && bytes > 0 && !download.empty() && sha256.size() == 64; }
};
int compareVersions(std::string_view left, std::string_view right);
UpdateRelease parseUpdateRelease(std::string_view json);
std::optional<std::pair<std::wstring, std::wstring>> updateDownloadAddress(std::wstring_view url);
std::string fileSha256(const std::filesystem::path &file);
bool verifyUpdateFile(const std::filesystem::path &, const UpdateRelease &);
enum class UpdatePhase { Idle, Checking, Current, Available, Downloading, Ready, Error };
struct UpdateState {
  UpdatePhase phase = UpdatePhase::Idle;
  std::wstring message = L"업데이트를 확인해 주세요.";
  std::optional<UpdateRelease> release;
  int64_t checkedAt{};
  uint64_t received{};
  std::filesystem::path installer;
  bool busy() const { return phase == UpdatePhase::Checking || phase == UpdatePhase::Downloading; }
};
class UpdateService {
public:
  UpdateService(HttpClient &, std::filesystem::path root, std::function<void()> notify,
                std::string currentVersion = TEMPOS_VERSION_STRING, std::string publicKey = TEMPOS_UPDATE_PUBLIC_KEY);
  ~UpdateService();
  UpdateState state() const;
  bool check();
  bool download();
  void cancel();
  void stop();
private:
  void publish(UpdateState);
  HttpClient &http_;
  std::filesystem::path root_;
  std::function<void()> notify_;
  std::string current_, key_;
  bool restoreReady();
  mutable std::mutex mutex_;
  UpdateState state_;
  std::jthread worker_;
};
} // namespace tempos
