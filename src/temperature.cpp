#include "temperature.h"
#include "storage.h"
#include <winternl.h>
#include <d3dkmthk.h>
namespace tempos {
TemperatureReading parseCpuTemperature(std::string_view response, double requestedAt) {
  TemperatureReading out{NAN, TemperatureStatus::Unavailable, 0};
  if (response.size() > 4096)
    return out;
  try {
    auto j = Json::parse(response);
    if (j.at("version").get<int>() != 1)
      return out;
    auto status = j.at("status").get<std::string>();
    if (status == "unsupported") {
      out.status = TemperatureStatus::Unsupported;
      return out;
    }
    if (status != "ready")
      return out;
    double c = j.at("celsius").get<double>(), age = j.at("ageMs").get<double>();
    // Zero is also what unavailable firmware/driver paths commonly return.
    if (!std::isfinite(c) || c <= 0 || c > 150 || !std::isfinite(age) || age < 0 || age > 60000)
      return out;
    out = {c, TemperatureStatus::Ready, requestedAt - age / 1000.};
  } catch (...) {}
  return out;
}
TemperatureReading gpuTemperature(LUID luid) {
  TemperatureReading result;
  D3DKMT_OPENADAPTERFROMLUID open{};
  open.AdapterLuid = luid;
  if (D3DKMTOpenAdapterFromLuid(&open) < 0)
    return result;
  D3DKMT_ADAPTER_PERFDATA data{};
  D3DKMT_QUERYADAPTERINFO query{};
  query.hAdapter = open.hAdapter;
  query.Type = KMTQAITYPE_ADAPTERPERFDATA;
  query.pPrivateDriverData = &data;
  query.PrivateDriverDataSize = sizeof data;
  auto status = D3DKMTQueryAdapterInfo(&query);
  D3DKMT_CLOSEADAPTER close{open.hAdapter};
  D3DKMTCloseAdapter(&close);
  if (status >= 0 && data.Temperature > 0 && data.Temperature <= 1500)
    result = {data.Temperature / 10., TemperatureStatus::Ready, monotonic()};
  return result;
}
namespace {
struct Handle {
  HANDLE value = INVALID_HANDLE_VALUE;
  ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
TemperatureReading readPipe(const std::wstring &pipeName, int interval) {
  TemperatureReading unavailable{NAN, TemperatureStatus::Unavailable, 0};
  // GENERIC_WRITE includes FILE_CREATE_PIPE_INSTANCE, deliberately absent from
  // the service ACL. Request only the data access the client actually needs.
  Handle pipe{CreateFileW(pipeName.c_str(), FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE, 0, nullptr,
                          OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr)};
  if (pipe.value == INVALID_HANDLE_VALUE)
    return unavailable;
  Handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  if (!event.value)
    return unavailable;
  const double started = monotonic();
  auto transfer = [&](void *data, DWORD bytes, bool writing, DWORD &done) {
    OVERLAPPED pending{};
    pending.hEvent = event.value;
    ResetEvent(event.value);
    BOOL ok = writing ? WriteFile(pipe.value, data, bytes, &done, &pending)
                      : ReadFile(pipe.value, data, bytes, &done, &pending);
    if (ok)
      return true;
    if (GetLastError() != ERROR_IO_PENDING)
      return false;
    DWORD budget = DWORD(std::max(0., 750. - (monotonic() - started) * 1000));
    if (WaitForSingleObject(event.value, budget) != WAIT_OBJECT_0) {
      CancelIoEx(pipe.value, &pending);
      GetOverlappedResult(pipe.value, &pending, &done, TRUE);
      return false;
    }
    return GetOverlappedResult(pipe.value, &pending, &done, FALSE) != FALSE;
  };
  std::string request = Json{{"version", 1}, {"intervalSeconds", interval}}.dump() + "\n";
  DWORD done = 0;
  if (!transfer(request.data(), DWORD(request.size()), true, done) || done != request.size())
    return unavailable;
  std::string response;
  while (response.size() < 4096) {
    char buffer[512];
    if (!transfer(buffer, sizeof buffer, false, done) || !done)
      return unavailable;
    response.append(buffer, done);
    if (response.find('\n') != std::string::npos)
      return parseCpuTemperature(response, started);
  }
  return unavailable;
}
}
CpuTemperatureClient::CpuTemperatureClient(std::wstring pipeName)
    : pipeName_(std::move(pipeName)), thread_([this](std::stop_token stop) { run(stop); }) {}
CpuTemperatureClient::~CpuTemperatureClient() {
  thread_.request_stop();
  cv_.notify_all();
  thread_.join();
}
void CpuTemperatureClient::configure(bool enabled, int interval) {
  std::lock_guard lock(mutex_);
  interval = std::clamp(interval, 1, 10);
  if (enabled == enabled_ && interval == interval_)
    return;
  enabled_ = enabled;
  interval_ = interval;
  ++generation_;
  reading_ = {NAN, enabled ? TemperatureStatus::Loading : TemperatureStatus::Disabled, 0};
  cv_.notify_all();
}
TemperatureReading CpuTemperatureClient::reading() const {
  std::lock_guard lock(mutex_);
  auto result = reading_;
  if (result.status == TemperatureStatus::Ready && !validTemperature(result, monotonic(), interval_)) {
    result.celsius = NAN;
    result.status = TemperatureStatus::Stale;
  }
  return result;
}
void CpuTemperatureClient::run(std::stop_token stop) {
  std::unique_lock lock(mutex_);
  while (!stop.stop_requested()) {
    cv_.wait(lock, stop, [&] { return enabled_; });
    if (stop.stop_requested()) break;
    auto generation = generation_;
    int interval = interval_;
    lock.unlock();
    auto value = readPipe(pipeName_, interval);
    lock.lock();
    if (generation == generation_) reading_ = value;
    cv_.wait_for(lock, stop, std::chrono::seconds(interval),
                 [&] { return generation != generation_; });
  }
}
} // namespace tempos
