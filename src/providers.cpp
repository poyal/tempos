#include <winsock2.h>
#include <ws2ipdef.h>
#include "providers.h"
#include <pdhmsg.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <winternl.h>
#include <set>
using Microsoft::WRL::ComPtr;
namespace tempos {
std::wstring gpuEngineKey(std::wstring instance) {
  // PDH emits uppercase LUID hex digits while DXGI keys use lowercase.
  std::transform(instance.begin(), instance.end(), instance.begin(),
                 [](wchar_t c) { return c >= L'A' && c <= L'Z' ? wchar_t(c + (L'a' - L'A')) : c; });
  auto pos = instance.find(L"luid_");
  if (pos == std::wstring::npos || instance.find(L"_total") != std::wstring::npos)
    return {};
  return instance.substr(pos);
}
static std::wstring registryString(HKEY root, const wchar_t *path, const wchar_t *value) {
  wchar_t b[512]{};
  DWORD n = sizeof b;
  if (RegGetValueW(root, path, value, RRF_RT_REG_SZ, nullptr, b, &n) == ERROR_SUCCESS)
    return b;
  return {};
}
LocalMetrics::LocalMetrics() {
  cpuName_ = registryString(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                            L"ProcessorNameString");
  DWORD n = 256;
  wchar_t name[256]{};
  if (GetComputerNameW(name, &n))
    pcName_ = name;
  auto build = registryString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                              L"CurrentBuildNumber");
  osName_ = L"Windows · 빌드 " + build;
}
LocalMetrics::~LocalMetrics() {
  if (cpuQuery_)
    PdhCloseQuery(cpuQuery_);
  if (gpuQuery_)
    PdhCloseQuery(gpuQuery_);
}
void LocalMetrics::reset() {
  cpuPrimed_ = gpuPrimed_ = false;
  previous_.clear();
  gpuUsage_.clear();
  gpuTime_ = 0;
}
void LocalMetrics::retain(bool cpu, bool gpu, bool network) {
  if (!cpu && cpuQuery_) {
    PdhCloseQuery(cpuQuery_);
    cpuQuery_ = nullptr;
    cpuCounter_ = nullptr;
    cpuPrimed_ = false;
  }
  if (!gpu && gpuQuery_) {
    PdhCloseQuery(gpuQuery_);
    gpuQuery_ = nullptr;
    gpuCounter_ = nullptr;
    gpuPrimed_ = false;
    gpuUsage_.clear();
    gpuBuffer_.clear();
    gpuBuffer_.shrink_to_fit();
  }
  if (!network)
    previous_.clear();
}
std::vector<Device> LocalMetrics::devices(Kind k) {
  std::vector<Device> out;
  if (k == Kind::Gpu) {
    gpus_.clear();
    ComPtr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
      for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> a;
        if (factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND)
          break;
        DXGI_ADAPTER_DESC1 d{};
        if (!a || FAILED(a->GetDesc1(&d)) || (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
          continue;
        DISPLAYCONFIG_ADAPTER_NAME name{};
        name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME;
        name.header.size = sizeof name;
        name.header.adapterId = d.AdapterLuid;
        std::wstring id;
        if (DisplayConfigGetDeviceInfo(&name.header) == ERROR_SUCCESS)
          id = name.adapterDevicePath;
        else
          id = L"device:" + std::to_wstring(d.VendorId) + L":" + std::to_wstring(d.DeviceId) + L":" +
               d.Description;
        gpus_.push_back({{id, d.Description}, d.AdapterLuid});
        out.push_back({id, d.Description});
      }
  } else if (k == Kind::Network) {
    PMIB_IF_TABLE2 table = nullptr;
    if (GetIfTable2(&table) == NO_ERROR) {
      for (ULONG i = 0; i < table->NumEntries; ++i) {
        auto &r = table->Table[i];
        if (r.Type == IF_TYPE_SOFTWARE_LOOPBACK)
          continue;
        wchar_t id[40]{};
        StringFromGUID2(r.InterfaceGuid, id, 40);
        out.push_back({id, std::wstring(r.Alias) + L" · " + r.Description});
      }
      FreeMibTable(table);
    }
  } else if (k == Kind::Disk) {
    wchar_t volume[MAX_PATH]{};
    HANDLE h = FindFirstVolumeW(volume, MAX_PATH);
    if (h != INVALID_HANDLE_VALUE) {
      do {
        wchar_t paths[1024]{};
        DWORD needed = 0;
        if (!GetVolumePathNamesForVolumeNameW(volume, paths, 1024, &needed) || !*paths)
          continue;
        UINT type = GetDriveTypeW(paths);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE)
          continue;
        wchar_t label[128]{};
        GetVolumeInformationW(volume, label, 128, nullptr, nullptr, nullptr, nullptr, 0);
        out.push_back({volume, std::wstring(paths) + L" " + label});
      } while (FindNextVolumeW(h, volume, MAX_PATH));
      FindVolumeClose(h);
    }
  }
  return out;
}
Snapshot LocalMetrics::read(Kind k, const std::wstring &source) {
  switch (k) {
  case Kind::Cpu:
    return cpu();
  case Kind::Gpu:
    return gpu(source);
  case Kind::Memory:
    return memory();
  case Kind::System:
    return system();
  case Kind::Network:
    return network(source);
  case Kind::Disk:
    return disk(source);
  default: {
    Snapshot s;
    s.status = Status::Ready;
    return s;
  }
  }
}
Snapshot LocalMetrics::cpu() {
  if (!cpuQuery_) {
    PdhOpenQueryW(nullptr, 0, &cpuQuery_);
    if (cpuQuery_)
      PdhAddEnglishCounterW(cpuQuery_, L"\\Processor Information(_Total)\\% Processor Time", 0, &cpuCounter_);
    if (!cpuCounter_ && cpuQuery_)
      PdhAddEnglishCounterW(cpuQuery_, L"\\Processor(_Total)\\% Processor Time", 0, &cpuCounter_);
  }
  Snapshot s;
  s.title = cpuName_;
  double t = monotonic();
  if (!cpuCounter_ || PdhCollectQueryData(cpuQuery_) != ERROR_SUCCESS) {
    s.status = Status::Unavailable;
    s.message = L"CPU 사용량을 읽을 수 없음";
    return s;
  }
  PDH_FMT_COUNTERVALUE v{};
  if (cpuPrimed_ && PdhGetFormattedCounterValue(cpuCounter_, PDH_FMT_DOUBLE, nullptr, &v) == ERROR_SUCCESS &&
      (v.CStatus == PDH_CSTATUS_VALID_DATA || v.CStatus == PDH_CSTATUS_NEW_DATA) && v.doubleValue >= 0 &&
      v.doubleValue <= 100.01) {
    s.status = Status::Ready;
    s.value = std::clamp(v.doubleValue, 0., 100.);
    s.history.add({cpuTime_, t, s.value, true});
  } else {
    s.message = L"측정 중";
  }
  cpuPrimed_ = true;
  cpuTime_ = t;
  s.sampledAt = t;
  return s;
}
Snapshot LocalMetrics::gpu(const std::wstring &source) {
  if (!gpuQuery_) {
    PdhOpenQueryW(nullptr, 0, &gpuQuery_);
    if (gpuQuery_)
      PdhAddEnglishCounterW(gpuQuery_, L"\\GPU Engine(*)\\Utilization Percentage", 0, &gpuCounter_);
    devices(Kind::Gpu);
  }
  Snapshot s;
  auto found = std::find_if(gpus_.begin(), gpus_.end(), [&](auto &g) { return g.device.id == source; });
  if (source.empty() && !gpus_.empty())
    found = gpus_.begin();
  if (found == gpus_.end()) {
    s.status = Status::Unavailable;
    s.message = L"GPU를 선택하세요";
    return s;
  }
  s.title = found->device.name;
  double t = monotonic();
  if (t - gpuTime_ >= .8) {
    gpuUsage_.clear();
    if (!gpuCounter_ || PdhCollectQueryData(gpuQuery_) != ERROR_SUCCESS) {
      s.status = Status::Unavailable;
      s.message = L"GPU 사용량을 읽을 수 없음";
      return s;
    }
    if (gpuPrimed_) {
      DWORD bytes = 0, count = 0;
      auto rc = PdhGetFormattedCounterArrayW(gpuCounter_, PDH_FMT_DOUBLE, &bytes, &count, nullptr);
      if (rc == PDH_MORE_DATA && bytes <= 2 * 1024 * 1024) {
        gpuBuffer_.resize(bytes);
        auto *items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(gpuBuffer_.data());
        rc = PdhGetFormattedCounterArrayW(gpuCounter_, PDH_FMT_DOUBLE, &bytes, &count, items);
        if (rc == ERROR_SUCCESS) {
          std::map<std::wstring, double> engines;
          for (DWORD i = 0; i < count; ++i) {
            auto &item = items[i];
            if (item.FmtValue.CStatus != PDH_CSTATUS_VALID_DATA &&
                item.FmtValue.CStatus != PDH_CSTATUS_NEW_DATA)
              continue;
            auto key = gpuEngineKey(item.szName);
            if (key.empty())
              continue;
            double value = item.FmtValue.doubleValue;
            if (!std::isfinite(value) || value < 0)
              continue;
            engines[key] += value;
          }
          for (auto &[key, value] : engines) {
            auto end = key.find(L"_phys_");
            if (end != std::wstring::npos && value <= 100.5)
              gpuUsage_[key.substr(0, end)] = std::max(gpuUsage_[key.substr(0, end)], std::min(100., value));
          }
        }
      }
    }
    gpuPrimed_ = true;
    gpuTime_ = t;
  }
  wchar_t key[80]{};
  swprintf_s(key, L"luid_0x%08x_0x%08x", static_cast<unsigned>(found->luid.HighPart), found->luid.LowPart);
  auto value = gpuUsage_.find(key);
  if (value == gpuUsage_.end()) {
    s.status = Status::Unavailable;
    s.message = L"카운터 준비 중 또는 지원 안 됨";
  } else {
    s.status = Status::Ready;
    s.value = value->second;
  }
  s.sampledAt = t;
  return s;
}
Snapshot LocalMetrics::memory() {
  Snapshot s;
  MEMORYSTATUSEX m{};
  m.dwLength = sizeof m;
  if (GlobalMemoryStatusEx(&m) && m.ullTotalPhys > 0 && m.ullAvailPhys <= m.ullTotalPhys) {
    s.status = Status::Ready;
    s.total = double(m.ullTotalPhys);
    s.available = double(m.ullAvailPhys);
    s.secondary = s.total - s.available;
    s.value = s.secondary / s.total * 100.;
    s.title = L"물리 메모리";
    s.detail = formatBytes(s.secondary) + L" / " + formatBytes(s.total);
  } else {
    s.status = Status::Error;
    s.message = L"메모리 조회 실패";
  }
  return s;
}
Snapshot LocalMetrics::system() {
  if (gpus_.empty())
    devices(Kind::Gpu);
  Snapshot s;
  s.status = Status::Ready;
  s.title = pcName_;
  s.detail = osName_;
  s.extra = cpuName_;
  for (auto &g : gpus_)
    s.extra += L"\n" + g.device.name;
  s.value = double(GetTickCount64() / 1000);
  return s;
}
Snapshot LocalMetrics::network(const std::wstring &source) {
  Snapshot s;
  GUID guid{};
  if (source.empty() || FAILED(CLSIDFromString(source.c_str(), &guid))) {
    s.status = Status::Unavailable;
    s.message = L"네트워크 어댑터를 선택하세요";
    return s;
  }
  MIB_IF_ROW2 row{};
  if (ConvertInterfaceGuidToLuid(&guid, &row.InterfaceLuid) != NO_ERROR || GetIfEntry2(&row) != NO_ERROR) {
    previous_.erase(source);
    s.status = Status::Unavailable;
    s.message = L"장치 없음";
    return s;
  }
  s.title = row.Alias;
  if (row.OperStatus != IfOperStatusUp) {
    previous_.erase(source);
    s.status = Status::Unavailable;
    s.message = L"연결 안 됨";
    return s;
  }
  double t = monotonic();
  auto old = previous_.find(source);
  if (old != previous_.end() && t > old->second.time && row.InOctets >= old->second.input &&
      row.OutOctets >= old->second.output) {
    s.value = double(row.InOctets - old->second.input) / (t - old->second.time);
    s.secondary = double(row.OutOctets - old->second.output) / (t - old->second.time);
    s.status = Status::Ready;
  } else
    s.message = L"측정 중";
  previous_[source] = {row.InOctets, row.OutOctets, t};
  s.sampledAt = t;
  return s;
}
Snapshot LocalMetrics::disk(const std::wstring &source) {
  Snapshot s;
  std::wstring volume = source;
  if (volume.empty()) {
    wchar_t win[MAX_PATH]{}, mount[MAX_PATH]{}, id[MAX_PATH]{};
    GetWindowsDirectoryW(win, MAX_PATH);
    if (GetVolumePathNameW(win, mount, MAX_PATH) && GetVolumeNameForVolumeMountPointW(mount, id, MAX_PATH))
      volume = id;
  }
  if (volume.empty() || volume.rfind(L"\\\\?\\Volume{", 0) != 0) {
    s.status = Status::Unavailable;
    s.message = L"로컬 볼륨을 선택하세요";
    return s;
  }
  wchar_t paths[1024]{};
  DWORD n = 0;
  GetVolumePathNamesForVolumeNameW(volume.c_str(), paths, 1024, &n);
  s.title = *paths ? paths : L"볼륨";
  DISK_SPACE_INFORMATION info{};
  DWORD old = 0;
  SetThreadErrorMode(SEM_FAILCRITICALERRORS, &old);
  HRESULT hr = GetDiskSpaceInformationW(volume.c_str(), &info);
  SetThreadErrorMode(old, nullptr);
  if (SUCCEEDED(hr) && info.ActualTotalAllocationUnits > 0 &&
      info.ActualAvailableAllocationUnits <= info.ActualTotalAllocationUnits) {
    double unit = double(info.SectorsPerAllocationUnit) * info.BytesPerSector;
    s.total = info.ActualTotalAllocationUnits * unit;
    s.available = info.ActualAvailableAllocationUnits * unit;
    s.secondary = info.CallerAvailableAllocationUnits * unit;
    s.value = (s.total - s.available) / s.total * 100.;
    s.status = Status::Ready;
  } else {
    s.status = Status::Unavailable;
    s.message = L"볼륨 조회 불가 · 제거 또는 잠김";
  }
  return s;
}
std::wstring providerKey(const Widget &w) {
  std::wstring key = std::to_wstring(int(w.kind)) + L":" + w.source;
  if (w.kind == Kind::Weather)
    key += L":" + std::to_wstring(w.nx) + L":" + std::to_wstring(w.ny);
  if (w.kind == Kind::Calendar) {
    key += L":" + w.timezone + L":" + std::to_wstring(int(w.size)) + L":" + std::to_wstring(w.weekStart);
    for (auto &id : w.calendars)
      key += L":" + id;
  }
  return key;
}
ProviderService::ProviderService(HWND notify, RemoteRead remote, bool fixture)
    : remote_(std::move(remote)), notify_(notify), fixture_(fixture),
      thread_([this](std::stop_token t) { run(t); }) {}
ProviderService::~ProviderService() {
  stop();
}
void ProviderService::stop() {
  thread_.request_stop();
  cv_.notify_all();
  if (thread_.joinable())
    thread_.join();
  remoteQueue_.stop();
}
void ProviderService::configure(const std::vector<Widget> &widgets, bool paused, bool energy) {
  std::lock_guard g(mutex_);
  {
    std::lock_guard localGuard(localMutex_);
    auto has = [&](Kind k) {
      return !paused && std::any_of(widgets.begin(), widgets.end(), [&](auto &w) { return w.kind == k; });
    };
    local_.retain(has(Kind::Cpu), has(Kind::Gpu), has(Kind::Network));
    if (paused_ != paused)
      local_.reset();
  }
  std::set<std::wstring> keys;
  std::map<std::wstring, int> periods;
  for (auto &w : widgets) {
    if (w.kind == Kind::Clock)
      continue;
    auto key = providerKey(w);
    keys.insert(key);
    int period = w.interval;
    if (energy && (w.kind == Kind::Cpu || w.kind == Kind::Gpu || w.kind == Kind::Network))
      period = std::max(5, period);
    if (w.kind == Kind::Weather)
      period = std::max(600, period);
    if (!periods.contains(key))
      periods[key] = period;
    else
      periods[key] = std::min(periods[key], period);
    if (!entries_.contains(key)) {
      Entry e;
      e.widget = w;
      e.generation = ++generation_;
      entries_.emplace(key, std::move(e));
    } else
      entries_.at(key).widget = w;
  }
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (!keys.contains(it->first))
      it = entries_.erase(it);
    else {
      if (it->second.interval != periods[it->first])
        it->second.due = Clock::now();
      it->second.interval = periods[it->first];
      ++it;
    }
  }
  if (paused_ != paused) {
    for (auto &[key, e] : entries_)
      e.due = Clock::now();
  }
  paused_ = paused;
  cv_.notify_all();
}
size_t ProviderService::subscriptions() const {
  std::lock_guard g(mutex_);
  return entries_.size();
}
std::shared_ptr<const Snapshot> ProviderService::snapshot(const Widget &w) {
  std::lock_guard g(mutex_);
  auto i = entries_.find(providerKey(w));
  return i == entries_.end() ? std::make_shared<Snapshot>() : i->second.data;
}
std::vector<Device> ProviderService::devices(Kind k) {
  std::lock_guard g(localMutex_);
  return local_.devices(k);
}
void ProviderService::refresh() {
  std::lock_guard g(mutex_);
  for (auto &[key, e] : entries_)
    e.due = Clock::now();
  cv_.notify_all();
}
void ProviderService::invalidate(Kind kind) {
  std::lock_guard g(mutex_);
  for (auto &[key, e] : entries_)
    if (e.widget.kind == kind) {
      e.generation = ++generation_;
      e.busy = false;
      e.data = std::make_shared<Snapshot>();
      e.due = Clock::now();
    }
  cv_.notify_all();
}
void ProviderService::publish(const std::wstring &key, uint64_t generation, Snapshot data) {
  std::lock_guard g(mutex_);
  auto i = entries_.find(key);
  if (i == entries_.end() || i->second.generation != generation)
    return;
  auto &e = i->second;
  auto old = e.data;
  double t = monotonic();
  if (e.widget.kind == Kind::Cpu || e.widget.kind == Kind::Gpu || e.widget.kind == Kind::Network) {
    data.history = old->history;
    data.history2 = old->history2;
    if (old->sampledAt > 0 && t - old->sampledAt <= std::max(5, e.interval * 2)) {
      data.history.add({old->sampledAt, t, data.value, data.status == Status::Ready});
      if (e.widget.kind == Kind::Network)
        data.history2.add({old->sampledAt, t, data.secondary, data.status == Status::Ready});
    }
  }
  data.sampledAt = t;
  e.data = std::make_shared<Snapshot>(std::move(data));
  e.busy = false;
  e.due = Clock::now() + std::chrono::seconds(e.interval);
  PostMessageW(notify_, WM_TEMPOS_DATA, 0, 0);
  cv_.notify_all();
}
void ProviderService::run(std::stop_token token) {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  while (!token.stop_requested()) {
    std::wstring key;
    Widget w;
    uint64_t generation = 0;
    {
      std::unique_lock l(mutex_);
      auto next = Clock::now() + 24h;
      if (!paused_)
        for (auto &[k, e] : entries_) {
          if (e.busy)
            continue;
          if (e.due <= Clock::now()) {
            key = k;
            w = e.widget;
            generation = e.generation;
            e.busy = true;
            break;
          }
          next = std::min(next, e.due);
        }
      if (key.empty()) {
        cv_.wait_until(l, token, next, [&] {
          return token.stop_requested() ||
                 (!paused_ && std::any_of(entries_.begin(), entries_.end(), [](auto &p) {
                   return !p.second.busy && p.second.due <= Clock::now();
                 }));
        });
        continue;
      }
    }
    if (w.kind == Kind::Weather || w.kind == Kind::Calendar || w.kind == Kind::Disk) {
      if (!remoteQueue_.post([this, key, w, generation](std::stop_token st) {
            Snapshot s;
            try {
              if (fixture_) {
                s.status = Status::Ready;
                s.title = L"테스트 자료";
                s.value = 42;
                s.secondary = 512e9;
                s.total = 1e12;
                s.available = 512e9;
                if (w.kind != Kind::Disk)
                  s = remote_(w, st);
              } else if (w.kind == Kind::Disk) {
                LocalMetrics diskReader;
                s = diskReader.read(w.kind, w.source);
              } else
                s = remote_(w, st);
            } catch (...) {
              s.status = Status::Error;
              s.message = L"자료를 읽을 수 없음";
            }
            publish(key, generation, std::move(s));
          })) {
        Snapshot s;
        s.status = Status::Error;
        s.message = L"작업 대기 한도";
        publish(key, generation, std::move(s));
      }
    } else {
      Snapshot s;
      if (fixture_) {
        s.status = Status::Ready;
        s.value = w.kind == Kind::Network ? 1e6 : 42.;
        s.secondary = w.kind == Kind::Network ? 125000. : 8. * 1024 * 1024 * 1024;
        s.total = 16. * 1024 * 1024 * 1024;
        s.available = s.total - s.secondary;
        s.title = std::wstring(kindName(w.kind));
        s.detail = L"테스트 자료";
      } else {
        std::lock_guard g(localMutex_);
        s = local_.read(w.kind, w.source);
      }
      publish(key, generation, std::move(s));
    }
  }
  CoUninitialize();
}
} // namespace tempos
