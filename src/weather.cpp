#include "weather.h"
#include <cstdio>
#include <stdexcept>
namespace tempos {
using namespace std::chrono;
std::wstring weatherSource(WeatherProvider provider) {
  return provider == WeatherProvider::Kma ? L"자료: 기상청" : L"자료: Open-Meteo";
}
WeatherState kmaWeatherState(int rain, int sky) {
  switch (rain) {
  case 1:
    return WeatherState::Rain;
  case 2:
  case 6:
    return WeatherState::Sleet;
  case 3:
  case 7:
    return WeatherState::Snow;
  case 4:
    return WeatherState::Showers;
  case 5:
    return WeatherState::Drizzle;
  }
  return sky == 1   ? WeatherState::Clear
         : sky == 3 ? WeatherState::Cloudy
         : sky == 4 ? WeatherState::Overcast
                    : WeatherState::Unknown;
}
static std::string compactDate(sys_days day) {
  year_month_day d{day};
  char b[20]{};
  sprintf_s(b, "%04d%02u%02u", int(d.year()), unsigned(d.month()), unsigned(d.day()));
  return b;
}
KmaBase kmaBase(int64_t now, int product) {
  auto kst = sys_seconds{seconds{now}} + 9h;
  auto base = kst - (product == 1 ? 45min : 10min);
  auto day = floor<days>(base);
  int hour = int(duration_cast<hours>(base - day).count());
  if (product == 2) {
    static constexpr int cycles[]{2, 5, 8, 11, 14, 17, 20, 23};
    int selected = -1;
    for (int h : cycles)
      if (h <= hour)
        selected = h;
    if (selected < 0) {
      day -= days{1};
      selected = 23;
    }
    hour = selected;
  }
  char b[8]{};
  sprintf_s(b, "%02d%02d", hour, product == 1 ? 30 : 0);
  return {compactDate(day), b};
}
double kmaNumber(const Json &v) {
  try {
    double x;
    if (v.is_number())
      x = v.get<double>();
    else if (v.is_string()) {
      auto s = v.get<std::string>();
      size_t n = 0;
      x = std::stod(s, &n);
      if (n != s.size())
        return NAN;
    } else
      return NAN;
    return std::isfinite(x) && std::abs(x) < 900 ? x : NAN;
  } catch (...) {
    return NAN;
  }
}
std::wstring weatherCondition(int rain, int sky) {
  switch (rain) {
  case 1:
    return L"비";
  case 2:
  case 6:
    return L"비와 눈";
  case 3:
  case 7:
    return L"눈";
  case 4:
    return L"소나기";
  case 5:
    return L"빗방울";
  }
  switch (sky) {
  case 1:
    return L"맑음";
  case 3:
    return L"구름 많음";
  case 4:
    return L"흐림";
  default:
    return L"날씨 확인 중";
  }
}
std::vector<Json> kmaItems(const std::string &body) {
  if (body.find_first_not_of(" \t\r\n") != std::string::npos &&
      body[body.find_first_not_of(" \t\r\n")] == '<') {
    for (auto tag : {std::string("resultCode"), std::string("returnReasonCode")}) {
      auto begin = body.find("<" + tag + ">");
      auto end = body.find("</" + tag + ">", begin);
      if (begin != std::string::npos && end != std::string::npos && end - begin < tag.size() + 7)
        throw std::runtime_error("KMA:" + body.substr(begin + tag.size() + 2, end - begin - tag.size() - 2));
    }
    throw std::runtime_error("KMA:FORMAT");
  }
  auto j = Json::parse(body);
  auto &r = j.at("response");
  auto code = r.at("header").value("resultCode", std::string{});
  if (code != "00")
    throw std::runtime_error("KMA:" + code);
  auto &items = r.at("body").at("items");
  if (!items.is_object() || !items.contains("item"))
    return {};
  auto &a = items.at("item");
  if (a.is_array())
    return a.get<std::vector<Json>>();
  if (a.is_object())
    return {a};
  return {};
}
void WeatherService::invalidate() {
  std::lock_guard g(mutex_);
  cache_.clear();
  retries_.clear();
  blockedUntil_ = 0;
  openDue_.clear();
  openFailures_.clear();
  openBlockedUntil_ = 0;
}
std::vector<Json> WeatherService::fetch(const Widget &w, int product, std::stop_token stop, int64_t now,
                                        bool previous) {
  auto base = kmaBase(now, product);
  std::string id = std::to_string(product) + ":" + std::to_string(w.nx) + ":" + std::to_string(w.ny) + ":" +
                   base.date + base.time;
  auto old = cache_.find(id);
  if (old != cache_.end() && (product == 2 || nowUnix() - old->second.at < 600))
    return Json::parse(old->second.items).get<std::vector<Json>>();
  auto key = fixture_ ? std::string("fixture") : store_.secret(L"weather-key");
  if (key.empty())
    throw std::runtime_error("KEY_REQUIRED");
  if (nowUnix() < blockedUntil_)
    throw std::runtime_error("BLOCKED");
  auto retryId = std::to_string(product) + ":" + std::to_string(w.nx) + ":" + std::to_string(w.ny);
  auto &retry = retries_[retryId];
  if (!previous && nowUnix() < retry.first)
    throw std::runtime_error("RETRY_WAIT");
  const wchar_t *paths[]{L"getUltraSrtNcst", L"getUltraSrtFcst", L"getVilageFcst"};
  std::wstring basePath = L"/api/typ02/openApi/VilageFcstInfoService_2.0/" + std::wstring(paths[product]) +
                          L"?authKey=" + wide(urlEncode(key)) + L"&dataType=JSON&numOfRows=1000&base_date=" +
                          wide(base.date) + L"&base_time=" + wide(base.time) + L"&nx=" +
                          std::to_wstring(w.nx) + L"&ny=" + std::to_wstring(w.ny);
  SecureZeroMemory(key.data(), key.size());
  struct ClearPath {
    std::wstring &value;
    ~ClearPath() { SecureZeroMemory(value.data(), value.size() * sizeof(wchar_t)); }
  } clearPath{basePath};
  try {
    std::vector<Json> items;
    int received = 0;
    size_t transferred = 0;
    for (int page = 1; page <= 8; ++page) {
      auto path = basePath + L"&pageNo=" + std::to_wstring(page);
      auto response = http_.request(L"apihub.kma.go.kr", path, L"GET", {}, {}, stop);
      SecureZeroMemory(path.data(), path.size() * sizeof(wchar_t));
      if (!response.ok()) {
        if (response.status == 429) {
          blockedUntil_ = nowUnix() + std::max(600, response.retryAfter);
          blockedMessage_ = L"요청 한도 · 잠시 후 재시도";
        }
        if (response.status == 401 || response.status == 403) {
          blockedUntil_ = nowUnix() + 86400;
          blockedMessage_ = L"인증키와 API 이용 권한을 확인하세요";
        }
        throw std::runtime_error("NETWORK");
      }
      transferred += response.body.size();
      if (transferred > 3 * 1024 * 1024)
        throw std::runtime_error("RESPONSE_LIMIT");
      auto values = kmaItems(response.body);
      auto metadata = Json::parse(response.body).at("response").at("body");
      if (metadata.value("pageNo", page) != page)
        throw std::runtime_error("PAGE_MISMATCH");
      received += int(values.size());
      for (auto &v : values) {
        auto category = v.value("category", std::string{});
        bool needed = product == 0
                          ? (category == "T1H" || category == "PTY" || category == "REH" || category == "WSD")
                      : product == 1
                          ? (category == "T1H" || category == "SKY" || category == "PTY" || category == "POP")
                          : (category == "TMN" || category == "TMX" || category == "SKY" ||
                             category == "PTY" || category == "POP");
        if (!needed)
          continue;
        Json compact = Json::object();
        for (auto field :
             {"category", "obsrValue", "fcstValue", "fcstDate", "fcstTime", "baseDate", "baseTime"})
          if (v.contains(field))
            compact[field] = v[field];
        items.push_back(std::move(compact));
      }
      if (received >= metadata.value("totalCount", received))
        break;
      if (values.empty() || page == 8 || stop.stop_requested())
        throw std::runtime_error("INCOMPLETE_PAGES");
    }
    if (!received && product == 0)
      throw std::runtime_error("KMA:03");
    auto packed = Json(items).dump();
    size_t total = packed.size();
    for (auto &[k, c] : cache_)
      total += c.items.size();
    if (total > 3 * 1024 * 1024)
      cache_.clear();
    cache_[id] = {nowUnix(), std::move(packed)};
    retry = {0, 0};
    return items;
  } catch (const std::exception &e) {
    std::string m = e.what();
    if (m == "KMA:22") {
      blockedUntil_ = nowUnix() + 24 * 3600;
      blockedMessage_ = L"오늘 요청 한도 초과";
    }
    if (m == "KMA:30" || m == "KMA:31") {
      blockedUntil_ = nowUnix() + 24 * 3600;
      blockedMessage_ = L"인증키를 확인하세요";
    }
    if (m == "KMA:03" && !previous)
      return fetch(w, product, stop, now - (product == 2 ? 10800 : 3600), true);
    retry.second = std::min(6, retry.second + 1);
    retry.first = nowUnix() + std::min(3600, 600 * (1 << (retry.second - 1)));
    throw;
  }
}
static int64_t forecastTime(const Json &j) {
  auto date = j.value("fcstDate", std::string{}), time = j.value("fcstTime", std::string{});
  if (date.size() != 8 || time.size() != 4)
    return 0;
  return parseRfc3339(date.substr(0, 4) + "-" + date.substr(4, 2) + "-" + date.substr(6, 2) + "T" +
                      time.substr(0, 2) + ":" + time.substr(2, 2) + ":00+09:00");
}
std::vector<WeatherDay> parseKmaDays(const std::vector<Json> &items, int64_t now) {
  const auto today = localMidnight(localDate(now, L"Asia/Seoul"), L"Asia/Seoul");
  std::map<int64_t, WeatherDay> days;
  std::map<int64_t, WeatherHour> hours;
  auto severity = [](WeatherState state) {
    switch (state) {
    case WeatherState::Thunder: return 9;
    case WeatherState::Sleet: return 8;
    case WeatherState::Snow: return 7;
    case WeatherState::Showers: return 6;
    case WeatherState::Rain: return 5;
    case WeatherState::Drizzle: return 4;
    case WeatherState::Overcast: return 3;
    case WeatherState::Cloudy: return 2;
    case WeatherState::Clear: return 1;
    default: return 0;
    }
  };
  for (const auto &item : items) {
    const auto time = forecastTime(item);
    if (time <= 0)
      continue;
    const auto midnight = localMidnight(localDate(time, L"Asia/Seoul"), L"Asia/Seoul");
    if (midnight <= today || midnight > today + 4 * 86400)
      continue;
    const auto category = item.value("category", std::string{});
    const double value = kmaNumber(item.value("fcstValue", Json{}));
    if (!std::isfinite(value))
      continue;
    auto &day = days[midnight];
    day.time = midnight;
    if (category == "TMN" && value >= -100 && value <= 70)
      day.low = value;
    else if (category == "TMX" && value >= -100 && value <= 70)
      day.high = value;
    else if (category == "POP" && value >= 0 && value <= 100)
      day.probability = std::isfinite(day.probability) ? std::max(day.probability, value) : value;
    else if (category == "SKY" && (value == 1 || value == 3 || value == 4))
      hours[time].sky = int(value);
    else if (category == "PTY" && value >= 0 && value <= 7 && value == std::floor(value))
      hours[time].rain = int(value);
  }
  for (auto &[time, hour] : hours) {
    auto &day = days.at(localMidnight(localDate(time, L"Asia/Seoul"), L"Asia/Seoul"));
    auto state = kmaWeatherState(hour.rain, hour.sky);
    if (severity(state) > severity(day.state))
      day.state = state;
  }
  std::vector<WeatherDay> result;
  for (auto &[time, day] : days) {
    if (day.low > day.high)
      day.low = day.high = NAN;
    result.push_back(day);
  }
  return result;
}
Snapshot WeatherService::read(const Widget &w, std::stop_token stop, WeatherProvider provider) {
  std::lock_guard g(mutex_);
  if (provider == WeatherProvider::OpenMeteo)
    return readOpenMeteo(w, stop);
  Snapshot s;
  s.title = w.region;
  WeatherData d;
  int64_t now = nowUnix();
  std::wstring id = std::to_wstring(w.nx) + L":" + std::to_wstring(w.ny);
  if (!diskLoaded_ && !fixture_) {
    diskLoaded_ = true;
    auto cache = store_.loadPrivate(L"weather-cache");
    try {
      if (cache.is_array() && cache.size() <= 32)
        for (auto &v : cache) {
          WeatherData saved;
          saved.fetched = v.at("fetched").get<int64_t>();
          if (saved.fetched > now || now - saved.fetched >= 86400)
            continue;
          saved.temperature = v.at("temperature").get<double>();
          saved.observed = v.at("observed").get<int64_t>();
          saved.rain = v.value("rain", 0);
          saved.sky = v.value("sky", 0);
          saved.condition = weatherCondition(saved.rain, saved.sky);
          saved.state = kmaWeatherState(saved.rain, saved.sky);
          for (auto field : {"low", "high", "humidity", "wind"})
            if (v.contains(field) && v[field].is_number()) {
              double value = v[field].get<double>();
              if (std::string_view(field) == "low")
                saved.low = value;
              else if (std::string_view(field) == "high")
                saved.high = value;
              else if (std::string_view(field) == "humidity")
                saved.humidity = value;
              else
                saved.wind = value;
            }
          if (v.contains("days") && v["days"].is_array() && v["days"].size() <= 4)
            for (const auto &entry : v["days"]) {
              WeatherDay day;
              day.time = entry.at("time").get<int64_t>();
              const auto today = localMidnight(localDate(now, L"Asia/Seoul"), L"Asia/Seoul");
              if (day.time <= today || day.time > today + 4 * 86400)
                continue;
              day.low = entry["low"].is_number() ? entry["low"].get<double>() : NAN;
              day.high = entry["high"].is_number() ? entry["high"].get<double>() : NAN;
              day.probability = entry["probability"].is_number() ? entry["probability"].get<double>() : NAN;
              day.state = WeatherState(std::clamp(entry.value("state", 0), 0, int(WeatherState::Thunder)));
              saved.days.push_back(day);
            }
          last_[wide(v.at("id").get<std::string>())] = saved;
        }
    } catch (...) {
      last_.clear();
    }
  }
  try {
    {
      auto obs = fetch(w, 0, stop, now);
      for (auto &j : obs) {
        auto c = j.value("category", std::string{});
        double v = kmaNumber(j.value("obsrValue", Json{}));
        if (c == "T1H")
          d.temperature = v;
        else if (c == "REH")
          d.humidity = v;
        else if (c == "WSD")
          d.wind = v;
        else if (c == "PTY" && std::isfinite(v))
          d.rain = int(v);
        if (d.observed == 0) {
          auto date = j.value("baseDate", std::string{}), time = j.value("baseTime", std::string{});
          if (date.size() == 8 && time.size() == 4)
            d.observed = parseRfc3339(date.substr(0, 4) + "-" + date.substr(4, 2) + "-" + date.substr(6, 2) +
                                      "T" + time.substr(0, 2) + ":" + time.substr(2, 2) + ":00+09:00");
        }
      }
      if (!std::isfinite(d.temperature) || d.observed <= 0 || d.observed > now + 300 ||
          now - d.observed >= 86400)
        throw std::runtime_error("NO_DATA");
      std::map<int64_t, WeatherHour> hourly;
      try {
        for (auto &j : fetch(w, 1, stop, now)) {
          auto t = forecastTime(j);
          if (t < now - 3600 || t > now + 7 * 3600)
            continue;
          auto &h = hourly[t];
          h.time = t;
          auto c = j.value("category", std::string{});
          double v = kmaNumber(j.value("fcstValue", Json{}));
          if (c == "T1H")
            h.temperature = v;
          else if (c == "POP")
            h.probability = v;
          else if (c == "SKY" && std::isfinite(v))
            h.sky = int(v);
          else if (c == "PTY" && std::isfinite(v))
            h.rain = int(v);
        }
        for (auto &[t, h] : hourly) {
          h.state = kmaWeatherState(h.rain, h.sky);
          if (d.sky == 0)
            d.sky = h.sky;
          if (t >= now && d.hours.size() < 6)
            d.hours.push_back(h);
        }
        d.skyForecast = true;
      } catch (...) {
        s.extra = L"하늘 상태 예보 갱신 대기";
      }
      auto today = compactDate(floor<days>(sys_seconds{seconds{now}} + 9h));
      auto temperatures = [&](const std::vector<Json> &items) {
        for (auto &j : items) {
          if (j.value("fcstDate", std::string{}) != today)
            continue;
          auto c = j.value("category", std::string{});
          if (c == "TMN")
            d.low = kmaNumber(j.value("fcstValue", Json{}));
          else if (c == "TMX")
            d.high = kmaNumber(j.value("fcstValue", Json{}));
        }
      };
      try {
        auto daily = fetch(w, 2, stop, now);
        temperatures(daily);
        d.days = parseKmaDays(daily, now);
        if (!std::isfinite(d.low) || !std::isfinite(d.high)) {
          auto day = floor<days>(sys_seconds{seconds{now}} + 9h);
          auto earliest = duration_cast<seconds>((day + 2h + 10min - 9h).time_since_epoch()).count();
          if (earliest > now)
            earliest -= 86400;
          temperatures(fetch(w, 2, stop, earliest, true));
        }
      } catch (...) {
      }
      d.fetched = now;
    }
    d.condition = weatherCondition(d.rain, d.sky);
    d.state = kmaWeatherState(d.rain, d.sky);
    auto sun = sunriseSunset(localDate(now, L"Asia/Seoul"), w.latitude, w.longitude);
    auto local = localTime(now, L"Asia/Seoul");
    double hour = duration<double, std::ratio<3600>>(local - floor<days>(local)).count();
    d.night =
        std::isfinite(sun.first) && std::isfinite(sun.second) && (hour < sun.first || hour >= sun.second);
    last_[id] = d;
    if (last_.size() > 32)
      last_.erase(last_.begin());
    if (!fixture_) {
      Json cache = Json::array();
      for (auto &[key, value] : last_)
        if (now - value.fetched < 86400) {
          Json days = Json::array();
          for (const auto &day : value.days)
            days.push_back({{"time", day.time}, {"low", day.low}, {"high", day.high},
                            {"probability", day.probability}, {"state", int(day.state)}});
          cache.push_back({{"id", utf8(key)},
                           {"fetched", value.fetched},
                           {"observed", value.observed},
                           {"temperature", value.temperature},
                           {"low", value.low},
                           {"high", value.high},
                           {"humidity", value.humidity},
                           {"wind", value.wind},
                           {"sky", value.sky},
                           {"rain", value.rain},
                           {"days", std::move(days)}});
        }
      store_.savePrivate(L"weather-cache", cache);
    }
    s.status = now - d.observed > 7200 ? Status::Stale : Status::Ready;
    if (s.status == Status::Stale)
      s.message = L"최근 자료 · 갱신 지연";
  } catch (const std::exception &e) {
    auto found = last_.find(id);
    if (found != last_.end() && now - found->second.observed < 86400 && now - found->second.fetched < 86400) {
      d = found->second;
      s.status = Status::Stale;
      s.message = L"최근 자료 · 갱신 대기";
    } else {
      s.status = Status::Unavailable;
      std::string error = e.what();
      s.message = error == "KEY_REQUIRED" ? L"설정에서 기상청 인증키를 입력하세요" : L"날씨를 불러올 수 없음";
    }
    if (now < blockedUntil_)
      s.message = blockedMessage_;
  }
  const auto today = localMidnight(localDate(now, L"Asia/Seoul"), L"Asia/Seoul");
  std::erase_if(d.days, [today](const auto &day) { return day.time <= today; });
  s.weather = std::make_shared<WeatherData>(std::move(d));
  return s;
}
} // namespace tempos
