#include "storage.h"
#include <shlobj.h>
#include <dpapi.h>
#include <fstream>
#include <set>
namespace tempos {
static std::string str(const std::wstring &s) {
  return utf8(s);
}
Json widgetJson(const Widget &w) {
  return {{"id", str(w.id)},
          {"kind", int(w.kind)},
          {"size", int(w.size)},
          {"scale", w.scale},
          {"x", w.x},
          {"y", w.y},
          {"monitor", str(w.monitor)},
          {"theme", w.theme},
          {"interval", w.interval},
          {"backgroundTransparency", w.backgroundTransparency},
          {"transparency", w.transparency},
          {"locked", w.locked},
          {"clickThrough", w.clickThrough},
          {"topmost", w.topmost},
          {"seconds", w.seconds},
          {"twelveHour", w.twelveHour},
          {"graph", w.graph},
          {"weatherAuto", w.weatherAuto},
          {"fahrenheit", w.fahrenheit},
          {"binaryDisk", w.binaryDisk},
          {"networkBytes", w.networkBytes},
          {"source", str(w.source)},
          {"timezone", str(w.timezone)},
          {"region", str(w.region)},
          {"nx", w.nx},
          {"ny", w.ny},
          {"latitude", w.latitude},
          {"longitude", w.longitude},
          {"weekStart", w.weekStart},
          {"systemFields", w.systemFields}};
}
Widget readWidget(const Json &j) {
  Widget w;
  w.id = wide(j.value("id", str(w.id)));
  w.kind = Kind(j.at("kind").get<int>());
  w.size = Size(j.value("size", 2));
  w.scale = j.value("scale", 100);
  w.x = j.value("x", 16.f);
  w.y = j.value("y", 16.f);
  w.monitor = wide(j.value("monitor", ""));
  w.theme = j.value("theme", -1);
  w.interval = j.value("interval", defaultInterval(w.kind));
  w.backgroundTransparency = j.value("backgroundTransparency", 10);
  w.transparency = j.value("transparency", 0);
  w.locked = j.value("locked", false);
  w.clickThrough = j.value("clickThrough", false);
  w.topmost = j.value("topmost", false);
  w.seconds = j.value("seconds", false);
  w.twelveHour = j.value("twelveHour", false);
  w.graph = j.value("graph", true);
  w.weatherAuto = j.value("weatherAuto", true);
  w.fahrenheit = j.value("fahrenheit", false);
  w.binaryDisk = j.value("binaryDisk", false);
  w.networkBytes = j.value("networkBytes", false);
  w.source = wide(j.value("source", ""));
  w.timezone = wide(j.value("timezone", ""));
  w.region = wide(j.value("region", str(w.region)));
  w.nx = j.value("nx", 60);
  w.ny = j.value("ny", 127);
  w.latitude = j.value("latitude", 37.573);
  w.longitude = j.value("longitude", 126.979);
  w.weekStart = j.value("weekStart", 0);
  if (j.contains("systemFields"))
    w.systemFields = j.at("systemFields").get<std::array<bool, 5>>();
  if (!validateWidget(w))
    throw std::runtime_error("Invalid widget settings");
  return w;
}
Json settingsJson(const Settings &s, bool exporting) {
  Json j = {{"version", 1}, {"theme", s.theme}, {"widgets", Json::array()}};
  if (!exporting)
    j["hidden"] = s.hidden;
  for (auto &w : s.widgets)
    j["widgets"].push_back(widgetJson(w));
  return j;
}
Settings readSettings(const Json &j) {
  if (j.value("version", 0) != 1)
    throw std::runtime_error("Unsupported settings version");
  Settings s;
  s.theme = j.value("theme", 1);
  s.hidden = j.value("hidden", false);
  if (s.theme < 0 || s.theme >= 9)
    throw std::runtime_error("Invalid theme");
  if (!j.contains("widgets") || !j["widgets"].is_array() || j["widgets"].size() > 64)
    throw std::runtime_error("Invalid widget count");
  std::set<std::wstring> ids;
  for (auto &item : j["widgets"]) {
    auto w = readWidget(item);
    if (!ids.insert(w.id).second)
      throw std::runtime_error("Duplicate widget id");
    s.widgets.push_back(std::move(w));
  }
  return s;
}
std::optional<std::string> boundedRead(const std::filesystem::path &p, size_t limit) {
  std::error_code ec;
  auto n = std::filesystem::file_size(p, ec);
  if (ec || n > limit)
    return {};
  std::ifstream f(p, std::ios::binary);
  if (!f)
    return {};
  std::string bytes(static_cast<size_t>(n), '\0');
  f.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!f && n)
    return {};
  return bytes;
}
bool atomicWrite(const std::filesystem::path &p, const std::string &data) {
  std::error_code ec;
  std::filesystem::create_directories(p.parent_path(), ec);
  if (ec)
    return false;
  auto temp = p;
  temp += L".tmp";
  HANDLE h =
      CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE)
    return false;
  DWORD written = 0;
  bool ok = WriteFile(h, data.data(), DWORD(data.size()), &written, nullptr) && written == data.size() &&
            FlushFileBuffers(h);
  CloseHandle(h);
  if (!ok) {
    DeleteFileW(temp.c_str());
    return false;
  }
  if (MoveFileExW(temp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    return true;
  DeleteFileW(temp.c_str());
  return false;
}
Store::Store(std::filesystem::path root) : root_(std::move(root)) {
  if (root_.empty()) {
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p))) {
      root_ = std::filesystem::path(p) / L"Tempos";
      CoTaskMemFree(p);
    } else
      throw std::runtime_error("LocalAppData unavailable");
  }
  std::filesystem::create_directories(root_);
}
Settings Store::load() {
  auto path = root_ / L"settings.json";
  auto read = [&](const std::filesystem::path &p) -> std::optional<Settings> {
    auto data = boundedRead(p);
    if (!data)
      return {};
    try {
      return readSettings(Json::parse(*data));
    } catch (...) {
      return {};
    }
  };
  if (auto s = read(path))
    return *s;
  if (std::filesystem::exists(path)) {
    std::error_code ec;
    std::filesystem::copy_file(path, root_ / (L"settings.corrupt." + std::to_wstring(nowUnix()) + L".json"),
                               std::filesystem::copy_options::overwrite_existing, ec);
    warning = L"설정 파일을 읽을 수 없어 복구했습니다.";
  }
  if (auto s = read(root_ / L"settings.backup.json"))
    return *s;
  Settings s;
  for (Kind k : {Kind::Clock, Kind::Weather, Kind::Cpu, Kind::Gpu, Kind::Memory}) {
    Widget w;
    w.kind = k;
    w.interval = defaultInterval(k);
    w.x = 16 + float(s.widgets.size() / 3) * 352;
    w.y = 16 + float(s.widgets.size() % 3) * 176;
    s.widgets.push_back(w);
  }
  return s;
}
bool Store::save(const Settings &s) {
  std::lock_guard guard(mutex_);
  try {
    auto file = root_ / L"settings.json";
    if (auto old = boundedRead(file)) {
      try {
        readSettings(Json::parse(*old));
        atomicWrite(root_ / L"settings.backup.json", *old);
      } catch (...) {
      }
    }
    return atomicWrite(file, settingsJson(s).dump(2));
  } catch (...) {
    return false;
  }
}
bool Store::saveSecret(const std::wstring &name, std::string_view value) {
  if (name.find_first_not_of(L"abcdefghijklmnopqrstuvwxyz0123456789-_") != std::wstring::npos ||
      value.size() > 4 * 1024 * 1024)
    return false;
  std::lock_guard guard(mutex_);
  DATA_BLOB in{DWORD(value.size()), reinterpret_cast<BYTE *>(const_cast<char *>(value.data()))}, out{};
  if (!CryptProtectData(&in, L"Tempos", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
    return false;
  std::string data(reinterpret_cast<char *>(out.pbData), out.cbData);
  SecureZeroMemory(out.pbData, out.cbData);
  LocalFree(out.pbData);
  return atomicWrite(root_ / (name + L".bin"), data);
}
std::string Store::secret(const std::wstring &name) const {
  if (name.find_first_not_of(L"abcdefghijklmnopqrstuvwxyz0123456789-_") != std::wstring::npos)
    return {};
  std::lock_guard guard(mutex_);
  auto bytes = boundedRead(root_ / (name + L".bin"), 5 * 1024 * 1024);
  if (!bytes)
    return {};
  DATA_BLOB in{DWORD(bytes->size()), reinterpret_cast<BYTE *>(bytes->data())}, out{};
  if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
    return {};
  std::string result(reinterpret_cast<char *>(out.pbData), out.cbData);
  SecureZeroMemory(out.pbData, out.cbData);
  LocalFree(out.pbData);
  return result;
}
void Store::eraseSecret(const std::wstring &name) {
  if (name.find_first_not_of(L"abcdefghijklmnopqrstuvwxyz0123456789-_") != std::wstring::npos)
    return;
  std::lock_guard guard(mutex_);
  DeleteFileW((root_ / (name + L".bin")).c_str());
  DeleteFileW((root_ / (name + L".bin.tmp")).c_str());
}
bool Store::savePrivate(const std::wstring &name, const Json &j) {
  try {
    return saveSecret(name, j.dump());
  } catch (...) {
    return false;
  }
}
Json Store::loadPrivate(const std::wstring &name) const {
  try {
    auto s = secret(name);
    return s.empty() ? Json::object() : Json::parse(s);
  } catch (...) {
    return Json::object();
  }
}
} // namespace tempos
