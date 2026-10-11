#pragma once
// Included by TestHost only. Responses pass through the production HTTP parser.
#include "weather.h"
#include <fstream>
namespace tempos {
inline HttpClient::Transport weatherFixture(const std::filesystem::path &root) {
  return [root](const std::wstring &host, const std::wstring &path, const auto &, const auto &, const auto &,
                std::stop_token stop) -> Response {
    Json options = Json::object();
    try {
      if (auto text = boundedRead(root / L"weather-scenario.json"))
        options = Json::parse(*text);
    } catch (...) {
    }
    const auto now = nowUnix();
    auto mode = options.value("mode", std::string("normal"));
    std::ofstream(root / L"weather-requests.jsonl", std::ios::app)
        << Json({{"host", utf8(host)}, {"mode", mode}, {"at", now}}).dump() << '\n';
    for (int ms = 0; ms < options.value("delayMs", 0) && !stop.stop_requested(); ms += 25)
      Sleep(25);
    if (stop.stop_requested())
      return {0, ERROR_CANCELLED, {}, 0};
    if (mode == "offline")
      return {0, ERROR_WINHTTP_TIMEOUT, {}, 0};
    if (mode == "quota")
      return {429, 0, {}, 3600};
    if (mode == "malformed")
      return {200, 0, "not-json", 0};
    if (host == L"api.open-meteo.com") {
      Json hourly = {{"time", Json::array()},
                     {"temperature_2m", Json::array()},
                     {"weather_code", Json::array()},
                     {"precipitation_probability", Json::array()}};
      int code = options.value("code", 0);
      for (int i = 1; i <= 6; ++i) {
        hourly["time"].push_back(now / 3600 * 3600 + i * 3600);
        hourly["temperature_2m"].push_back(21 - i);
        hourly["weather_code"].push_back(code);
        hourly["precipitation_probability"].push_back(10 * i);
      }
      Json body = {{"current",
                    {{"time", now - options.value("age", 0)},
                     {"temperature_2m", options.value("temperature", 21)},
                     {"relative_humidity_2m", 62},
                     {"wind_speed_10m", 2.1},
                     {"weather_code", code},
                     {"is_day", 1}}},
                   {"hourly", hourly},
                   {"daily",
                    {{"time", Json::array({localMidnight(localDate(now, L"Asia/Seoul"), L"Asia/Seoul")})},
                     {"temperature_2m_min", Json::array({16})},
                     {"temperature_2m_max", Json::array({24})}}}};
      return {200, 0, body.dump(), 0};
    }
    if (host != L"apihub.kma.go.kr")
      return {0, ERROR_ACCESS_DENIED, {}, 0};
    Json items = Json::array();
    auto base = kmaBase(now, 0);
    if (path.find(L"getUltraSrtNcst") != std::wstring::npos) {
      for (auto pair : {std::pair{"T1H", "18"}, {"REH", "62"}, {"WSD", "2.1"}, {"PTY", "0"}})
        items.push_back({{"category", pair.first},
                         {"obsrValue", pair.second},
                         {"baseDate", base.date},
                         {"baseTime", base.time}});
    } else {
      for (int i = 0; i <= 6; ++i) {
        auto text = rfc3339(now / 3600 * 3600 + i * 3600 + 9 * 3600);
        auto date = text.substr(0, 4) + text.substr(5, 2) + text.substr(8, 2);
        auto time = text.substr(11, 2) + "00";
        for (auto pair : {std::pair{"T1H", "18"},
                          {"SKY", "1"},
                          {"PTY", "0"},
                          {"POP", "10"},
                          {"TMN", "14"},
                          {"TMX", "23"}})
          items.push_back(
              {{"category", pair.first}, {"fcstValue", pair.second}, {"fcstDate", date}, {"fcstTime", time}});
      }
    }
    return {200, 0,
            Json({{"response",
                   {{"header", {{"resultCode", "00"}}},
                    {"body", {{"items", {{"item", items}}}, {"totalCount", items.size()}, {"pageNo", 1}}}}}})
                .dump(),
            0};
  };
}
} // namespace tempos
