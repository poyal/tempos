#include "startup.h"
namespace tempos {
static std::wstring quote(const std::wstring &arg) {
  std::wstring result = L"\"";
  size_t slashes = 0;
  for (wchar_t ch : arg) {
    if (ch == L'\\') {
      ++slashes;
      continue;
    }
    result.append(ch == L'"' ? slashes * 2 + 1 : slashes, L'\\');
    slashes = 0;
    result += ch;
  }
  result.append(slashes * 2, L'\\');
  return result + L"\"";
}
std::wstring startupCommand(const std::wstring &exe, const std::filesystem::path &root) {
  return quote(exe) +
         (root.empty() ? L"" : L" --data-dir " + quote(std::filesystem::absolute(root).wstring()));
}
bool StartupRegistration::enabled() const {
  DWORD type = 0, bytes = 0;
  return RegGetValueW(root_, key_.c_str(), L"Tempos", RRF_RT_REG_SZ, &type, nullptr, &bytes) ==
             ERROR_SUCCESS &&
         bytes > sizeof(wchar_t);
}
LONG StartupRegistration::set(bool enabled, const std::wstring &command) const {
  HKEY key{};
  LONG result = RegCreateKeyExW(root_, key_.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
  if (result != ERROR_SUCCESS)
    return result;
  result = enabled
               ? RegSetValueExW(key, L"Tempos", 0, REG_SZ, reinterpret_cast<const BYTE *>(command.c_str()),
                                DWORD((command.size() + 1) * sizeof(wchar_t)))
               : RegDeleteValueW(key, L"Tempos");
  RegCloseKey(key);
  return !enabled && result == ERROR_FILE_NOT_FOUND ? ERROR_SUCCESS : result;
}
} // namespace tempos
