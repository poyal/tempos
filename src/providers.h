#pragma once
#include "model.h"
#include "http.h"
#include <pdh.h>
#include <map>
namespace tempos {
inline constexpr auto allDisksSource = L"@all-local-volumes";
std::wstring gpuEngineKey(std::wstring instance);
struct NetworkSample {
  std::wstring id;
  uint64_t input{}, output{};
};
class NetworkTotal {
public:
  Snapshot read(const std::vector<NetworkSample> &, double time);
  void reset() { previous_.clear(); }

private:
  struct Previous {
    uint64_t input{}, output{};
    double time{};
  };
  std::map<std::wstring, Previous> previous_;
};
class LocalMetrics {
public:
  LocalMetrics();
  ~LocalMetrics();
  Snapshot read(Kind kind, const std::wstring &source);
  std::vector<Device> devices(Kind kind);
  void reset();
  void retain(bool cpu, bool gpu, bool network);

private:
  Snapshot cpu(), gpu(const std::wstring &), memory(), system(), network(const std::wstring &),
      disk(const std::wstring &);
  struct GpuDevice {
    Device device;
    LUID luid{};
  };
  std::vector<GpuDevice> gpus_;
  PDH_HQUERY cpuQuery_{}, gpuQuery_{};
  PDH_HCOUNTER cpuCounter_{}, gpuCounter_{};
  bool cpuPrimed_ = false, gpuPrimed_ = false;
  double cpuTime_{}, gpuTime_{};
  std::map<std::wstring, double> gpuUsage_;
  std::vector<unsigned char> gpuBuffer_;
  struct NetPrevious {
    uint64_t input{}, output{};
    double time{};
  };
  std::map<std::wstring, NetPrevious> previous_;
  NetworkTotal networkTotal_;
  std::wstring cpuName_, osName_, pcName_;
};
std::wstring providerKey(const Widget &widget, WeatherProvider provider = WeatherProvider::Kma);
class ProviderService {
public:
  using RemoteRead = std::function<Snapshot(const Widget &, std::stop_token, WeatherProvider)>;
  ProviderService(HWND notify, RemoteRead remote, bool fixture = false);
  ~ProviderService();
  void configure(const std::vector<Widget> &widgets, bool paused, bool energySaving = false,
                 WeatherProvider provider = WeatherProvider::Kma);
  std::shared_ptr<const Snapshot> snapshot(const Widget &widget);
  std::vector<Device> devices(Kind kind);
  void refresh();
  void invalidate(Kind kind);
  void stop();
  size_t subscriptions() const;

private:
  struct Entry {
    Widget widget;
    WeatherProvider provider = WeatherProvider::Kma;
    int interval{};
    Clock::time_point due{};
    bool busy = false;
    uint64_t generation{};
    std::shared_ptr<Snapshot> data = std::make_shared<Snapshot>();
  };
  void run(std::stop_token token);
  void publish(const std::wstring &key, uint64_t generation, Snapshot data);
  mutable std::mutex mutex_;
  std::mutex localMutex_;
  std::condition_variable_any cv_;
  std::map<std::wstring, Entry> entries_;
  LocalMetrics local_;
  WorkQueue remoteQueue_{2};
  RemoteRead remote_;
  HWND notify_{};
  bool paused_ = false, fixture_ = false;
  WeatherProvider weatherProvider_ = WeatherProvider::Kma;
  uint64_t generation_ = 0;
  std::jthread thread_;
};
constexpr UINT WM_TEMPOS_DATA = WM_APP + 1;
} // namespace tempos
