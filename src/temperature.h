#pragma once
#include "model.h"
#include <condition_variable>
#include <thread>
namespace tempos {
// JSON is bounded and versioned at the pipe boundary, not at the UI.
TemperatureReading parseCpuTemperature(std::string_view response, double requestedAt);
TemperatureReading gpuTemperature(LUID luid);
class CpuTemperatureClient {
public:
  explicit CpuTemperatureClient(std::wstring pipeName = L"\\\\.\\pipe\\Tempos.Sensors.v1");
  ~CpuTemperatureClient();
  void configure(bool enabled, int interval);
  TemperatureReading reading() const;
private:
  void run(std::stop_token);
  mutable std::mutex mutex_;
  std::condition_variable_any cv_;
  bool enabled_ = false;
  int interval_ = 2;
  uint64_t generation_{};
  TemperatureReading reading_{NAN, TemperatureStatus::Disabled, 0};
  const std::wstring pipeName_;
  std::jthread thread_;
};
} // namespace tempos
