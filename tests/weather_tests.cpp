#include "weather.h"
#include "providers.h"
#include "startup.h"
#include <shellapi.h>
#include <atomic>
#include <future>
#include <iostream>
using namespace tempos;
void check(bool, const char *);
static Json response(int64_t now) {
  auto midnight = localMidnight(localDate(now, L"Asia/Seoul"), L"Asia/Seoul");
  return {{"current",
           {{"time", now},
            {"temperature_2m", 21.5},
            {"relative_humidity_2m", 62},
            {"wind_speed_10m", 2.1},
            {"weather_code", 95},
            {"is_day", 0}}},
          {"daily",
           {{"time", {midnight, midnight + 86400}},
            {"temperature_2m_min", {16, 17}},
            {"temperature_2m_max", {24, 25}}}},
          {"hourly",
           {{"time", {now - 3600, now + 3600, now + 7200, now + 10800, now + 14400}},
            {"temperature_2m", {20, 21, 22, 23, 24}},
            {"weather_code", {0, 45, 61, 71, 99}},
            {"precipitation_probability", {0, 10, 20, 30, 40}}}}};
}
void weatherTests() {
  Settings settings;
  check(settings.weatherProvider == WeatherProvider::OpenMeteo, "New profiles default to Open-Meteo");
  auto legacy = settingsJson(settings);
  legacy.erase("weatherProvider");
  check(readSettings(legacy).weatherProvider == WeatherProvider::Kma, "Legacy profiles retain KMA");
  check(readSettings(settingsJson(settings, true)).weatherProvider == WeatherProvider::OpenMeteo,
        "Weather provider survives layout export/import");
  legacy["weatherProvider"] = "unknown";
  bool rejected = false;
  try {
    readSettings(legacy);
  } catch (...) {
    rejected = true;
  }
  check(rejected, "Unsupported provider rejected without silent fallback");
  int64_t now = parseRfc3339("2026-10-12T23:30:00+09:00");
  auto payload = response(now);
  auto parsed = parseOpenMeteo(payload, now);
  check(parsed.low == 16 && parsed.high == 24 && parsed.hours.size() == 4 && parsed.night,
        "Open-Meteo KST daily boundary and next four hours across midnight");
  check(parsed.wind == 2.1 && parsed.humidity == 62 && parsed.temperature == 21.5,
        "Open-Meteo Celsius and metres-per-second units");
  for (int c : {0,  1,  2,  3,  45, 48, 51, 53, 55, 56, 57, 61, 63, 65, 66,
                67, 71, 73, 75, 77, 80, 81, 82, 85, 86, 95, 96, 97, 99})
    check(wmoWeatherState(c) != WeatherState::Unknown, "Documented WMO code mapped");
  check(wmoWeatherState(100) == WeatherState::Unknown && wmoWeatherState(-1) == WeatherState::Unknown,
        "Unknown WMO code remains unknown");
  auto missing = payload;
  missing["current"]["relative_humidity_2m"] = nullptr;
  missing["hourly"]["precipitation_probability"][1] = nullptr;
  auto values = parseOpenMeteo(missing, now);
  check(std::isnan(values.humidity) && std::isnan(values.hours[0].probability),
        "Missing optional values are not zero");
  for (int scenario = 0; scenario < 5; ++scenario) {
    auto bad = payload;
    if (scenario == 0)
      bad["hourly"]["weather_code"].erase(0);
    if (scenario == 1)
      bad["current"]["temperature_2m"] = nullptr;
    if (scenario == 2)
      bad["current"]["time"] = now + 3600;
    if (scenario == 3)
      bad["current"]["time"] = now - 86400;
    if (scenario == 4)
      bad["hourly"]["time"][1] = now - 7200;
    rejected = false;
    try {
      parseOpenMeteo(bad, now);
    } catch (...) {
      rejected = true;
    }
    check(rejected, "Malformed, stale or unordered Open-Meteo data rejected");
  }
  auto root = std::filesystem::temp_directory_path() / (L"Tempos.WeatherTests." + newId());
  Store store(root);
  check(store.saveSecret(L"weather-key", "private-kma-key"), "Weather test credential protected");
  int calls = 0;
  DWORD httpStatus = 200;
  HttpClient http([&](const std::wstring &host, const std::wstring &path, const auto &, const auto &,
                      const auto &, auto) {
    ++calls;
    check(host == L"api.open-meteo.com" && path.find(L"private-kma-key") == std::wstring::npos &&
              path.find(L"authKey") == std::wstring::npos,
          "KMA credentials never sent to Open-Meteo");
    check(path.find(L"wind_speed_unit=ms") != std::wstring::npos &&
              path.find(L"timezone=Asia%2FSeoul") != std::wstring::npos,
          "Open-Meteo request fixes units and Korean timezone");
    return Response{httpStatus, 0, payload.dump(), 1800};
  });
  Widget w;
  w.kind = Kind::Weather;
  WeatherService service(store, http, false, [&] { return now; });
  auto read = [&] { return service.read(w, {}, WeatherProvider::OpenMeteo); };
  check(read().status == Status::Ready && read().weather->temperature == 21.5 && calls == 1,
        "Open-Meteo request shared within fifteen-minute cache");
  WeatherService restarted(store, http, false, [&] { return now; });
  check(restarted.read(w, {}, WeatherProvider::OpenMeteo).weather->temperature == 21.5 && calls == 1,
        "Open-Meteo protected disk cache survives restart");
  now += 900;
  httpStatus = 429;
  check(read().status == Status::Stale && calls == 2, "Rate limit preserves same-provider snapshot");
  w.longitude += .01;
  check(read().status == Status::Unavailable && calls == 2,
        "Rate limit applies across locations without mixing caches");
  w.longitude -= .01;
  now += 901;
  check(read().status == Status::Stale && calls == 2, "Retry-After exceeds normal retry interval");
  now += 900;
  httpStatus = 503;
  check(read().status == Status::Stale && calls == 3, "Server failure retains same-provider data");
  now += 86400;
  check(read().status == Status::Unavailable && !read().weather,
        "Expired weather values removed after twenty-four hours");
  check(store.secret(L"weather-key") == "private-kma-key", "Provider operations preserve KMA key");
  check(providerKey(w, WeatherProvider::Kma) != providerKey(w, WeatherProvider::OpenMeteo),
        "Subscription keys include provider");
  Widget other = w;
  other.id = newId();
  std::promise<void> entered, release;
  auto gate = release.get_future().share();
  std::atomic<int> kmaCalls = 0, openCalls = 0;
  ProviderService providers(
      nullptr,
      [&](const Widget &, std::stop_token, WeatherProvider provider) {
        Snapshot s;
        s.status = Status::Ready;
        s.value = provider == WeatherProvider::Kma ? 18 : 21;
        if (provider == WeatherProvider::Kma) {
          if (++kmaCalls == 1)
            entered.set_value();
          gate.wait();
        } else
          ++openCalls;
        return s;
      },
      true);
  providers.configure({w, other}, false, false, WeatherProvider::Kma);
  bool started = entered.get_future().wait_for(std::chrono::seconds(5)) == std::future_status::ready;
  providers.configure({w, other}, false, false, WeatherProvider::OpenMeteo);
  for (int i = 0; i < 500 && providers.snapshot(w)->status != Status::Ready; ++i)
    Sleep(10);
  bool switched = providers.snapshot(w)->value == 21;
  release.set_value();
  providers.stop();
  check(started && switched && providers.snapshot(w)->value == 21 && openCalls == 1 && kmaCalls == 1,
        "Late old-provider response discarded and duplicate widgets share requests");
  auto registry = L"Software\\Tempos\\Tests\\Core-" + newId();
  StartupRegistration startup(registry);
  struct Cleanup {
    std::wstring key;
    ~Cleanup() { RegDeleteTreeW(HKEY_CURRENT_USER, key.c_str()); }
  } cleanup{registry};
  auto command = startupCommand(L"C:\\앱 폴더\\Tempos.exe", L"C:\\자료 폴더\\");
  int argc = 0;
  auto argv = CommandLineToArgvW(command.c_str(), &argc);
  bool correct = argv && argc == 3 && std::wstring(argv[0]) == L"C:\\앱 폴더\\Tempos.exe" &&
                 std::wstring(argv[1]) == L"--data-dir" && std::wstring(argv[2]) == L"C:\\자료 폴더\\";
  if (argv)
    LocalFree(argv);
  check(correct, "Startup command preserves Unicode, spaces and trailing backslash");
  check(startup.set(true, command) == ERROR_SUCCESS && StartupRegistration(registry).enabled(),
        "Startup registration survives object recreation");
  check(startup.set(false, command) == ERROR_SUCCESS && !startup.enabled() &&
            startup.set(false, command) == ERROR_SUCCESS,
        "Startup disable is persistent and idempotent");
  HKEY readonly{};
  RegCreateKeyExW(HKEY_CURRENT_USER, registry.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &readonly,
                  nullptr);
  RegSetValueExW(readonly, L"Tempos", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&argc), sizeof argc);
  RegCloseKey(readonly);
  check(!startup.enabled(), "Malformed startup registry value is not shown as enabled");
  check(StartupRegistration(L"", nullptr).set(true, command) != ERROR_SUCCESS,
        "Startup registry failure propagates to caller");
}
int liveWeatherProbe() {
  Store store(std::filesystem::temp_directory_path() / (L"Tempos.LiveWeather." + newId()));
  HttpClient wire;
  HttpClient http([&](const auto &host, const auto &path, const auto &method, const auto &body,
                      const auto &headers, auto stop) {
    auto response = wire.request(host, path, method, body, headers, stop);
    std::cout << Json({{"httpStatus", response.status},
                       {"winHttpError", response.error},
                       {"responseBytes", response.body.size()},
                       {"now", nowUnix()}})
                     .dump()
              << '\n';
    if (response.ok()) {
      try {
        parseOpenMeteo(Json::parse(response.body), nowUnix());
      } catch (const std::exception &e) {
        std::cerr << "Parser: " << e.what() << '\n';
      }
    }
    return response;
  });
  WeatherService service(store, http);
  Widget w;
  w.kind = Kind::Weather;
  auto result = service.read(w, {}, WeatherProvider::OpenMeteo);
  check(result.status == Status::Ready && result.weather && std::isfinite(result.weather->temperature),
        "Real Open-Meteo request and production parser");
  std::cout << Json({{"provider", "open-meteo"},
                     {"temperature", result.weather->temperature},
                     {"observed", result.weather->observed},
                     {"fetched", result.weather->fetched},
                     {"forecastHours", result.weather->hours.size()}})
                   .dump()
            << '\n';
  return 0;
}
