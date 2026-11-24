#pragma once
#include "storage.h"
namespace tempos {
inline constexpr char updateManifestName[] = "update-win-x64.json";
inline constexpr char updateSignatureName[] = "update-win-x64.json.sig";
inline constexpr char inventoryName[] = "tempos-files.json";
struct SignedPackage {
  std::string version, name, sha256;
  uint64_t bytes{};
};
SignedPackage readSignedPackage(const std::filesystem::path &folder, std::string_view key);
std::string packageName(std::string_view version);
bool validJobId(std::wstring_view id);
void checkUpdatePath(const std::filesystem::path &root, const std::filesystem::path &path);
using FileInventory = std::map<std::wstring, std::string>;
FileInventory readInventory(const std::filesystem::path &folder);
FileInventory unpackUpdate(const std::filesystem::path &zip, const std::filesystem::path &stage);
} // namespace tempos
