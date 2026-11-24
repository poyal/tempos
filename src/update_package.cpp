#include "updates.h"
#include <monocypher-ed25519.h>
#include <miniz.h>
#include <charconv>
#include <cwctype>
#include <set>

namespace tempos {
namespace fs = std::filesystem;
namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
std::wstring lower(std::wstring s) {
  if (!s.empty())
    CharLowerBuffW(s.data(), DWORD(s.size()));
  return s;
}
std::wstring relativeName(std::string_view name) {
  auto path = wide(name);
  require(!path.empty() && path.size() < 1024 && utf8(path) == name &&
              path.find_first_of(L"\\:\"<>|?*") == path.npos,
          "invalid update filename");
  for (auto c : path)
    require(c >= 32, "control character in filename");
  size_t start = 0;
  do {
    auto end = path.find('/', start);
    auto part = path.substr(start, end - start);
    require(!part.empty() && part.back() != '.' && part.back() != ' ', "invalid path component");
    auto base = lower(part.substr(0, part.find('.')));
    require(base != L"con" && base != L"prn" && base != L"aux" && base != L"nul" && base != L"conin$" &&
                base != L"conout$" &&
                !(base.size() == 4 && (base.starts_with(L"com") || base.starts_with(L"lpt")) &&
                  wcschr(L"0123456789¹²³", base[3])),
            "device path");
    if (end == path.npos)
      break;
    start = end + 1;
  } while (true);
  return path;
}
bool digest(std::string_view s) {
  return s.size() == 64 && s.find_first_not_of("0123456789abcdef") == s.npos;
}
} // namespace
std::string packageName(std::string_view version) {
  return "Tempos-" + std::string(version) + "-win-x64-update.zip";
}
bool validJobId(std::wstring_view id) {
  GUID guid{};
  if (id.size() != 38 || FAILED(CLSIDFromString(std::wstring(id).c_str(), &guid)))
    return false;
  wchar_t canonical[40]{};
  StringFromGUID2(guid, canonical, 40);
  return _wcsicmp(canonical, std::wstring(id).c_str()) == 0;
}
SignedPackage readSignedPackage(const fs::path &folder, std::string_view key) {
  auto body = boundedRead(folder / updateManifestName, 65536),
       signature = boundedRead(folder / updateSignatureName, 64);
  require(key.size() == 64 && body && signature && signature->size() == 64, "missing update signature");
  uint8_t publicKey[32]{};
  for (size_t i = 0; i < sizeof publicKey; ++i) {
    auto part = key.substr(i * 2, 2);
    auto parsed = std::from_chars(part.data(), part.data() + 2, publicKey[i], 16);
    require(parsed.ec == std::errc{} && parsed.ptr == part.data() + 2, "invalid public key");
  }
  require(crypto_ed25519_check(reinterpret_cast<const uint8_t *>(signature->data()), publicKey,
                               reinterpret_cast<const uint8_t *>(body->data()), body->size()) == 0,
          "signature mismatch");
  auto j = Json::parse(*body);
  require(j.at("format") == "tempos-update-v1" && j.at("platform") == "win-x64" &&
              j.at("bytes").is_number_unsigned(),
          "update manifest format");
  SignedPackage p{j.at("version"), j.at("name"), j.at("sha256"), j.at("bytes")};
  compareVersions(p.version, p.version);
  require(p.name == packageName(p.version) && digest(p.sha256) && p.bytes && p.bytes <= maxUpdateBytes,
          "update manifest values");
  return p;
}
void checkUpdatePath(const fs::path &root, const fs::path &path) {
  auto relative = path.lexically_relative(root);
  require(!relative.empty() && !relative.is_absolute(), "outside update root");
  auto current = fs::absolute(root).lexically_normal().root_path();
  auto inspect = [](const fs::path &p) {
    DWORD attrs = GetFileAttributesW(p.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
      auto err = GetLastError();
      require(err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND, "inaccessible update path");
    } else
      require(!(attrs & FILE_ATTRIBUTE_REPARSE_POINT), "linked update path");
  };
  for (const auto &part : fs::absolute(root).lexically_normal().relative_path()) {
    current /= part;
    inspect(current);
  }
  for (const auto &part : relative) {
    require(part != L"..", "outside update root");
    current /= part;
    inspect(current);
  }
}
FileInventory readInventory(const fs::path &folder) {
  auto file = folder / inventoryName;
  checkUpdatePath(folder, file);
  auto bytes = boundedRead(file);
  require(bytes.has_value(), "missing file inventory");
  auto list = Json::parse(*bytes);
  require(list.is_array() && !list.empty() && list.size() < 8192, "invalid file inventory");
  FileInventory files;
  std::set<std::wstring> names;
  for (auto &entry : list) {
    auto path = relativeName(entry.at("path").get<std::string>());
    auto hash = entry.at("sha256").get<std::string>();
    require(lower(path) != wide(inventoryName) && names.insert(lower(path)).second && digest(hash),
            "invalid inventory entry");
    files.emplace(path, hash);
  }
  files.emplace(wide(inventoryName), fileSha256(file));
  return files;
}
FileInventory unpackUpdate(const fs::path &zip, const fs::path &stage) {
  require(fs::create_directory(stage), "stage already exists");
  struct Archive {
    mz_zip_archive zip{};
    FILE *input{};
    ~Archive() {
      if (zip.m_pState)
        mz_zip_reader_end(&zip);
      if (input)
        fclose(input);
    }
  } a;
  require(_wfopen_s(&a.input, zip.c_str(), L"rb") == 0 && mz_zip_reader_init_cfile(&a.zip, a.input, 0, 0),
          "invalid zip");
  unsigned count = mz_zip_reader_get_num_files(&a.zip);
  require(count && count <= 8192, "zip entry count");
  uint64_t remaining = 1024ull * 1024 * 1024;
  std::set<std::wstring> names;
  for (unsigned i = 0; i < count; ++i) {
    mz_zip_archive_file_stat stat{};
    require(mz_zip_reader_file_stat(&a.zip, i, &stat) && !stat.m_is_directory && !stat.m_is_encrypted &&
                stat.m_is_supported && !(stat.m_external_attr & FILE_ATTRIBUTE_REPARSE_POINT) &&
                ((stat.m_external_attr >> 28) == 0 || (stat.m_external_attr >> 28) == 8) &&
                stat.m_uncomp_size <= remaining && stat.m_uncomp_size <= maxUpdateBytes,
            "unsupported zip entry");
    unsigned length = mz_zip_reader_get_filename(&a.zip, i, nullptr, 0);
    require(length > 1 && length <= 4096, "zip filename length");
    std::string raw(length, '\0');
    mz_zip_reader_get_filename(&a.zip, i, raw.data(), length);
    raw.pop_back();
    auto name = relativeName(raw);
    require(names.insert(lower(name)).second, "duplicate zip path");
    auto file = stage / name;
    checkUpdatePath(stage, file);
    fs::create_directories(file.parent_path());
    FILE *out{};
    require(_wfopen_s(&out, file.c_str(), L"wbx") == 0, "create staging file");
    bool ok = mz_zip_reader_extract_to_cfile(&a.zip, i, out, 0) != 0;
    require(fclose(out) == 0 && ok, "extract zip file");
    remaining -= stat.m_uncomp_size;
  }
  auto files = readInventory(stage);
  require(files.size() == names.size(), "zip contents differ from inventory");
  for (auto &[name, hash] : files)
    require(names.contains(lower(name)) && fileSha256(stage / name) == hash, "staged file mismatch");
  return files;
}
} // namespace tempos
