#pragma once
#include "model.h"
#include <nlohmann/json.hpp>
namespace tempos {
using Json = nlohmann::json;
Json widgetJson(const Widget &widget);
Widget readWidget(const Json &json);
Json settingsJson(const Settings &settings, bool exporting = false);
Settings readSettings(const Json &json);
bool atomicWrite(const std::filesystem::path &path, const std::string &data);
std::optional<std::string> boundedRead(const std::filesystem::path &path, size_t maxBytes = 4 * 1024 * 1024);
class Store {
public:
  explicit Store(std::filesystem::path root = {});
  Settings load();
  bool save(const Settings &settings);
  bool saveSecret(const std::wstring &name, std::string_view value);
  std::string secret(const std::wstring &name) const;
  void eraseSecret(const std::wstring &name);
  bool savePrivate(const std::wstring &name, const Json &value);
  Json loadPrivate(const std::wstring &name) const;
  const std::filesystem::path &root() const { return root_; }
  std::wstring warning;

private:
  std::filesystem::path root_;
  mutable std::mutex mutex_;
};
} // namespace tempos
