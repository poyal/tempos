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
WeatherState wmoWeatherState(int code);
WeatherData parseOpenMeteo(const Json &, int64_t now);
std::vector<WeatherDay> parseKmaDays(const std::vector<Json> &, int64_t now);
class WeatherService {
public:
  WeatherService(Store &store, HttpClient &http, bool fixture = false,
                 std::function<int64_t()> clock = nowUnix)
      : store_(store), http_(http), fixture_(fixture), clock_(std::move(clock)) {}
  Snapshot read(const Widget &, std::stop_token, WeatherProvider = WeatherProvider::Kma);
  void invalidate();

private:
  std::vector<Json> fetch(const Widget &, int product, std::stop_token, int64_t time, bool previous = false);
  Store &store_;
  HttpClient &http_;
  bool fixture_;
  std::function<int64_t()> clock_;
  Snapshot readOpenMeteo(const Widget &, std::stop_token);
  std::map<std::string, WeatherData> openLast_;
  std::map<std::string, Json> openResponses_;
  std::map<std::string, int64_t> openDue_;
  std::map<std::string, int> openFailures_;
  bool openLoaded_ = false;
  int64_t openBlockedUntil_{};
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
