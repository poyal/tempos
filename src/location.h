#pragma once
#include "model.h"
#include <span>
#include <thread>

namespace tempos {
struct Region {
  std::wstring name;
  int nx{}, ny{};
  double lat{}, lon{};
};
std::optional<size_t> nearestRegion(std::span<const Region>, double latitude, double longitude);
struct LocationResult {
  double latitude = NAN, longitude = NAN;
  std::wstring error;
};
// One explicit request; never subscribes to location changes.
class LocationLookup {
public:
  ~LocationLookup();
  void start(HWND notify, UINT message);
  void cancel();
  std::optional<LocationResult> take();

private:
  struct State;
  std::shared_ptr<State> state_;
  std::jthread worker_;
};
} // namespace tempos
