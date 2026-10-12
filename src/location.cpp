#include "location.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Geolocation.h>

namespace tempos {
std::optional<size_t> nearestRegion(std::span<const Region> regions, double lat, double lon) {
  if (!std::isfinite(lat) || !std::isfinite(lon) || std::abs(lat) > 90 || std::abs(lon) > 180)
    return {};
  constexpr double radians = 3.141592653589793 / 180;
  double closest = 50.; // Do not silently select a Korean region for a distant location.
  std::optional<size_t> result;
  for (size_t i = 0; i < regions.size(); ++i) {
    double dlat = (regions[i].lat - lat) * radians, dlon = (regions[i].lon - lon) * radians;
    double a = std::pow(std::sin(dlat / 2), 2) +
               std::cos(lat * radians) * std::cos(regions[i].lat * radians) * std::pow(std::sin(dlon / 2), 2);
    double distance = 12742 * std::asin(std::sqrt(std::clamp(a, 0., 1.)));
    if (distance < closest) {
      closest = distance;
      result = i;
    }
  }
  return result;
}
struct LocationLookup::State {
  std::mutex mutex;
  std::optional<LocationResult> result;
};
LocationLookup::~LocationLookup() {
  cancel();
}
void LocationLookup::cancel() {
  worker_.request_stop();
  if (worker_.joinable())
    worker_.join();
  state_.reset();
}
std::optional<LocationResult> LocationLookup::take() {
  if (!state_)
    return {};
  std::lock_guard lock(state_->mutex);
  return std::exchange(state_->result, {});
}
void LocationLookup::start(HWND notify, UINT message) {
  using namespace winrt::Windows::Devices::Geolocation;
  using winrt::Windows::Foundation::AsyncStatus;
  cancel();
  state_ = std::make_shared<State>();
  auto complete = [state = state_, notify, message](LocationResult result) {
    {
      std::lock_guard lock(state->mutex);
      state->result = std::move(result);
    }
    PostMessageW(notify, message, 0, 0);
  };
  try {
    // Request consent on the foreground UI thread, then wait on a cancellable worker.
    auto access = Geolocator::RequestAccessAsync();
    worker_ = std::jthread([access, complete](std::stop_token stop) {
      bool initialized = false;
      LocationResult result;
      auto wait = [&](const auto &operation) {
        auto deadline = Clock::now() + 30s;
        while (operation.Status() == AsyncStatus::Started && !stop.stop_requested() &&
               Clock::now() < deadline)
          std::this_thread::sleep_for(50ms);
        if (stop.stop_requested() || operation.Status() == AsyncStatus::Started) {
          operation.Cancel();
          return false;
        }
        return true;
      };
      try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        initialized = true;
        if (!wait(access))
          result.error = L"위치 요청 시간이 초과됐어요. 다시 찾거나 지역을 직접 선택하세요.";
        else if (access.GetResults() != GeolocationAccessStatus::Allowed)
          result.error = L"Windows 설정 > 개인 정보 및 보안 > 위치에서 위치 서비스를 허용하세요.";
        else {
          Geolocator locator;
          locator.DesiredAccuracy(PositionAccuracy::Default);
          auto position = locator.GetGeopositionAsync(5min, 20s);
          if (wait(position)) {
            auto coordinate = position.GetResults().Coordinate().Point().Position();
            result.latitude = coordinate.Latitude;
            result.longitude = coordinate.Longitude;
          } else
            result.error = L"현재 위치를 찾지 못했어요. 다시 찾거나 지역을 직접 선택하세요.";
        }
      } catch (const winrt::hresult_error &error) {
        result.error = error.code() == E_ACCESSDENIED
                           ? L"Windows 설정 > 개인 정보 및 보안 > 위치에서 위치 서비스를 허용하세요."
                           : L"현재 위치를 찾지 못했어요. 위치 서비스를 확인하거나 지역을 직접 선택하세요.";
      } catch (...) {
        result.error = L"현재 위치를 찾지 못했어요. 지역을 직접 선택하세요.";
      }
      if (!stop.stop_requested())
        complete(std::move(result));
      if (initialized)
        winrt::uninit_apartment();
    });
  } catch (...) {
    complete({NAN, NAN, L"Windows 위치 서비스를 사용할 수 없어요. 지역을 직접 선택하세요."});
  }
}
} // namespace tempos
