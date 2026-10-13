#pragma once
#include "model.h"
#include <cwctype>
#include <shlobj.h>

namespace tempos {
#ifdef TEMPOS_TEST_HOST
inline std::wstring testProfileIdentity(const std::filesystem::path &root) {
  auto value = std::filesystem::weakly_canonical(std::filesystem::absolute(root)).wstring();
  std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) { return wchar_t(std::towlower(ch)); });
  while (value.size() > 3 && (value.back() == L'\\' || value.back() == L'/'))
    value.pop_back();
  return value;
}
inline bool isolatedTestProfile(const std::filesystem::path &root) {
  if (root.empty())
    return false;
  PWSTR local = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)))
    return false;
  std::filesystem::path installed = std::filesystem::path(local) / L"Tempos";
  CoTaskMemFree(local);
  auto candidate = testProfileIdentity(root), profile = testProfileIdentity(installed);
  return candidate != profile && !candidate.starts_with(profile + L"\\");
}
#endif
inline std::wstring managementWindowClass(const std::filesystem::path &root) {
#ifdef TEMPOS_TEST_HOST
  return L"Tempos.TestHost.Management." + std::to_wstring(std::hash<std::wstring>{}(testProfileIdentity(root)));
#else
  (void)root;
  return L"Tempos.Management";
#endif
}
} // namespace tempos
