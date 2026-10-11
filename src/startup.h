#pragma once
#include "model.h"
namespace tempos {
std::wstring startupCommand(const std::wstring &exe, const std::filesystem::path &dataRoot = {});
class StartupRegistration {
public:
  explicit StartupRegistration(std::wstring key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                               HKEY root = HKEY_CURRENT_USER)
      : key_(std::move(key)), root_(root) {}
  bool enabled() const;
  LONG set(bool enabled, const std::wstring &command) const;
  const std::wstring &key() const { return key_; }

private:
  std::wstring key_;
  HKEY root_;
};
} // namespace tempos
