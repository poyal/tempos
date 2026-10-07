#pragma once
#include "http.h"
#include "storage.h"
namespace tempos {
struct KmaBase {
  std::string date, time;
};
KmaBase kmaBase(int64_t now, int product);
double kmaNumber(const Json &value);
std::wstring weatherCondition(int rain, int sky);
std::vector<Json> kmaItems(const std::string &body);
class WeatherService {
public:
  WeatherService(Store &store, HttpClient &http, bool fixture = false)
      : store_(store), http_(http), fixture_(fixture) {}
  Snapshot read(const Widget &, std::stop_token);
  void invalidate();

private:
  std::vector<Json> fetch(const Widget &, int product, std::stop_token, int64_t time, bool previous = false);
  Store &store_;
  HttpClient &http_;
  bool fixture_;
  std::mutex mutex_;
  struct Cached {
    int64_t at{};
    std::string items;
  };
  std::map<std::string, Cached> cache_;
  std::map<std::wstring, WeatherData> last_;
  bool diskLoaded_ = false;
  std::map<std::string, std::pair<int64_t, int>> retries_;
  int64_t blockedUntil_{};
  std::wstring blockedMessage_;
};
} // namespace tempos
