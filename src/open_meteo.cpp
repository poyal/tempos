#include "weather.h"
#include <stdexcept>
namespace tempos {
WeatherState wmoWeatherState(int code) {
  switch (code) {
  case 0:
  case 1:
    return WeatherState::Clear;
  case 2:
    return WeatherState::Cloudy;
  case 3:
    return WeatherState::Overcast;
  case 45:
  case 48:
    return WeatherState::Fog;
  case 51:
  case 53:
  case 55:
  case 56:
  case 57:
    return WeatherState::Drizzle;
  case 61:
  case 63:
  case 65:
  case 66:
  case 67:
    return WeatherState::Rain;
  case 71:
  case 73:
  case 75:
  case 77:
  case 85:
  case 86:
    return WeatherState::Snow;
  case 80:
  case 81:
  case 82:
    return WeatherState::Showers;
  case 95:
  case 96:
  case 97:
  case 99:
    return WeatherState::Thunder;
  default:
    return WeatherState::Unknown;
  }
}
static std::wstring description(WeatherState state) {
  switch (state) {
  case WeatherState::Clear:
    return L"맑음";
  case WeatherState::Cloudy:
    return L"구름 많음";
  case WeatherState::Overcast:
    return L"흐림";
  case WeatherState::Rain:
    return L"비";
  case WeatherState::Sleet:
    return L"비와 눈";
  case WeatherState::Snow:
    return L"눈";
  case WeatherState::Showers:
    return L"소나기";
  case WeatherState::Drizzle:
    return L"이슬비";
  case WeatherState::Fog:
    return L"안개";
  case WeatherState::Thunder:
    return L"뇌우";
  default:
    return L"상태 정보 없음";
  }
}
static double value(const Json &j, const char *field, double min, double max) {
  auto it = j.find(field);
  if (it == j.end() || !it->is_number())
    return NAN;
  double v = it->get<double>();
  return std::isfinite(v) && v >= min && v <= max ? v : NAN;
}
static int code(const Json &j) {
  auto it = j.find("weather_code");
  if (it == j.end() || !it->is_number_integer() || *it < 0 || *it > 99)
    return -1;
  return it->get<int>();
}
static Json compactResponse(const Json &j) {
  Json result = Json::object();
  for (const auto &section : {"current", "hourly", "daily"}) {
    if (!j.contains(section))
      continue;
    result[section] = Json::object();
    for (const auto &field :
         {"time", "temperature_2m", "relative_humidity_2m", "wind_speed_10m", "weather_code", "is_day",
          "precipitation_probability", "temperature_2m_min", "temperature_2m_max",
          "precipitation_probability_max"})
      if (j.at(section).contains(field)) {
        auto numeric = [](const Json &v) { return v.is_number() ? v : Json(); };
        const auto &v = j.at(section).at(field);
        if (v.is_array()) {
          result[section][field] = Json::array();
          for (const auto &entry : v)
            result[section][field].push_back(numeric(entry));
        } else
          result[section][field] = numeric(v);
      }
  }
  return result;
}
WeatherData parseOpenMeteo(const Json &j, int64_t now) {
  if (j.value("error", false))
    throw std::runtime_error("OPEN_METEO_ERROR");
  const auto &current = j.at("current");
  WeatherData d;
  d.temperature = value(current, "temperature_2m", -100, 70);
  d.humidity = value(current, "relative_humidity_2m", 0, 100);
  d.wind = value(current, "wind_speed_10m", 0, 150);
  if (!current.at("time").is_number_integer())
    throw std::runtime_error("INVALID_TIME");
  d.observed = current.at("time").get<int64_t>();
  if (!std::isfinite(d.temperature) || d.observed <= 0 || d.observed > now + 300 || now - d.observed >= 86400)
    throw std::runtime_error("NO_DATA");
  d.fetched = now;
  d.state = wmoWeatherState(code(current));
  d.condition = description(d.state);
  d.night = value(current, "is_day", 0, 1) == 0;
  auto rows = [](const Json &section) {
    std::vector<Json> out;
    const auto &times = section.at("time");
    if (!times.is_array() || times.size() > 72)
      throw std::runtime_error("INVALID_TIMES");
    for (size_t i = 0; i < times.size(); ++i) {
      if (!times[i].is_number_integer())
        throw std::runtime_error("INVALID_TIME");
      Json row = {{"time", times[i]}};
      for (auto it = section.begin(); it != section.end(); ++it) {
        if (!it.value().is_array() || it.value().size() != times.size())
          throw std::runtime_error("INVALID_ARRAY_LENGTH");
        row[it.key()] = it.value()[i];
      }
      out.push_back(std::move(row));
    }
    return out;
  };
  if (j.contains("daily")) {
    const auto today = localMidnight(localDate(now, L"Asia/Seoul"), L"Asia/Seoul");
    int64_t previous = 0;
    for (const auto &day : rows(j.at("daily"))) {
      const auto time = day.at("time").get<int64_t>();
      if (time <= previous || time != localMidnight(localDate(time, L"Asia/Seoul"), L"Asia/Seoul"))
        throw std::runtime_error("INVALID_DAYS");
      previous = time;
      if (time == today) {
        d.low = value(day, "temperature_2m_min", -100, 70);
        d.high = value(day, "temperature_2m_max", -100, 70);
      } else if (time > today && time <= today + 4 * 86400) {
        WeatherDay forecast;
        forecast.time = time;
        forecast.low = value(day, "temperature_2m_min", -100, 70);
        forecast.high = value(day, "temperature_2m_max", -100, 70);
        if (forecast.low > forecast.high)
          forecast.low = forecast.high = NAN;
        forecast.probability = value(day, "precipitation_probability_max", 0, 100);
        forecast.state = wmoWeatherState(code(day));
        d.days.push_back(forecast);
      }
    }
  }
  if (j.contains("hourly")) {
    int64_t previous = 0;
    for (const auto &hour : rows(j.at("hourly"))) {
      auto time = hour.at("time").get<int64_t>();
      if (time <= previous)
        throw std::runtime_error("UNORDERED_HOURS");
      previous = time;
      if (time <= now || time > now + 4 * 3600 || d.hours.size() >= 4)
        continue;
      WeatherHour h;
      h.time = time;
      h.temperature = value(hour, "temperature_2m", -100, 70);
      h.probability = value(hour, "precipitation_probability", 0, 100);
      h.state = wmoWeatherState(code(hour));
      d.hours.push_back(h);
    }
  }
  return d;
}
Snapshot WeatherService::readOpenMeteo(const Widget &w, std::stop_token stop) {
  const int64_t now = clock_();
  const std::string id = std::to_string(w.latitude) + ":" + std::to_string(w.longitude);
  Snapshot s;
  s.title = w.region;
  s.weatherProvider = WeatherProvider::OpenMeteo;
  if (!openLoaded_) {
    openLoaded_ = true;
    auto cache = store_.loadPrivate(L"open-meteo-cache");
    if (cache.is_array() && cache.size() <= 32)
      for (const auto &item : cache) {
        try {
          auto fetched = item.at("fetched").get<int64_t>();
          if (fetched > now || now - fetched >= 86400)
            continue;
          auto d = parseOpenMeteo(item.at("response"), now);
          d.fetched = fetched;
          auto key = item.at("id").get<std::string>();
          openLast_[key] = d;
          openDue_[key] = item.at("response").contains("daily") &&
                                  item.at("response").at("daily").contains("precipitation_probability_max")
                              ? fetched + 900 : 0;
          openResponses_[key] = compactResponse(item.at("response"));
        } catch (...) {
        }
      }
  }
  auto found = openLast_.find(id);
  if (openDue_.size() >= 64 && !openDue_.contains(id)) {
    // Failed locations must not accumulate during repeated region changes.
    for (auto it = openDue_.begin(); it != openDue_.end();) {
      if (!openLast_.contains(it->first)) {
        openFailures_.erase(it->first);
        it = openDue_.erase(it);
      } else
        ++it;
    }
  }
  bool valid = found != openLast_.end() && now >= found->second.fetched &&
               now - found->second.fetched < 86400 && now - found->second.observed < 86400;
  bool failed = openFailures_[id] > 0;
  if (now >= openDue_[id] && now >= openBlockedUntil_) {
    try {
      std::wstring path = L"/v1/forecast?latitude=" + std::to_wstring(w.latitude) + L"&longitude=" +
                          std::to_wstring(w.longitude) +
                          L"&current=temperature_2m,relative_humidity_2m,wind_speed_10m,weather_code,is_day"
                          L"&daily=temperature_2m_min,temperature_2m_max,weather_code,precipitation_probability_max"
                          L"&timezone=Asia%2FSeoul"
                          L"&timeformat=unixtime&wind_speed_unit=ms&temperature_unit=celsius&forecast_days=5";
      auto response = http_.request(L"api.open-meteo.com", path, L"GET", {}, {}, stop);
      if (response.status == 429)
        openBlockedUntil_ = now + std::max(900, response.retryAfter);
      if (!response.ok())
        throw std::runtime_error("NETWORK");
      auto payload = Json::parse(response.body);
      auto d = parseOpenMeteo(payload, now);
      openLast_[id] = std::move(d);
      openResponses_[id] = compactResponse(payload);
      openDue_[id] = now + 900;
      openFailures_[id] = 0;
      failed = false;
      valid = true;
      found = openLast_.find(id);
      while (openLast_.size() > 32) {
        auto oldest = std::min_element(openLast_.begin(), openLast_.end(),
                                       [](auto &a, auto &b) { return a.second.fetched < b.second.fetched; });
        if (oldest == found)
          oldest = oldest == openLast_.begin() ? std::next(oldest) : openLast_.begin();
        openResponses_.erase(oldest->first);
        openDue_.erase(oldest->first);
        openFailures_.erase(oldest->first);
        openLast_.erase(oldest);
      }
      Json cache = Json::array();
      for (auto &[key, data] : openLast_)
        if (now - data.fetched < 86400)
          cache.push_back({{"id", key}, {"fetched", data.fetched}, {"response", openResponses_.at(key)}});
      store_.savePrivate(L"open-meteo-cache", cache);
    } catch (...) {
      failed = true;
      int attempts = openFailures_[id] = std::min(3, openFailures_[id] + 1);
      openDue_[id] = now + std::min(3600, 900 * (1 << (attempts - 1)));
    }
  }
  if (valid) {
    auto d = found->second;
    const auto sun = sunriseSunset(localDate(now, L"Asia/Seoul"), w.latitude, w.longitude);
    const auto local = localTime(now, L"Asia/Seoul");
    const double hour =
        std::chrono::duration<double, std::ratio<3600>>(local - std::chrono::floor<std::chrono::days>(local))
            .count();
    if (std::isfinite(sun.first) && std::isfinite(sun.second))
      d.night = hour < sun.first || hour >= sun.second;
    std::erase_if(d.hours, [now](auto &h) { return h.time <= now; });
    const auto today = localMidnight(localDate(now, L"Asia/Seoul"), L"Asia/Seoul");
    std::erase_if(d.days, [today](auto &day) { return day.time <= today; });
    s.status = failed || now < openBlockedUntil_ || now - d.observed > 7200 ? Status::Stale : Status::Ready;
    s.weather = std::make_shared<WeatherData>(std::move(d));
    if (s.status == Status::Stale)
      s.message = L"최근 자료 · 갱신 지연";
  } else {
    s.status = Status::Unavailable;
    s.message = L"날씨를 불러올 수 없음";
  }
  if (now < openBlockedUntil_)
    s.message = L"Open-Meteo 요청 한도 · 잠시 후 재시도";
  return s;
}
} // namespace tempos
