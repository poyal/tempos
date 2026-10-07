#include "model.h"
#include "storage.h"
#include "weather.h"
#include "calendar.h"
#include "providers.h"
#include <iostream>
#include <stdexcept>
using namespace tempos;
using namespace std::chrono;
int passed = 0;
void check(bool condition, const char *name) {
  if (!condition)
    throw std::runtime_error(name);
  ++passed;
  std::cerr << "PASS " << passed << " " << name << "\n";
}
template <class F> void throws(F fn, const char *name) {
  bool threw = false;
  try {
    fn();
  } catch (...) {
    threw = true;
  }
  check(threw, name);
}
int main() {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  try {
    check(gpuEngineKey(L"pid_10184_luid_0x00000000_0x000129F0_phys_0_eng_0_engtype_3D") ==
              gpuEngineKey(L"pid_7_luid_0x00000000_0x000129f0_phys_0_eng_0_engtype_3d"),
          "GPU process instances share case-insensitive engine identity");
    check(gpuEngineKey(L"_Total").empty() && gpuEngineKey(L"pid_1_luid_0x0_Total").empty(),
          "GPU total counters excluded");
    check(extent(Size::M).width == 336 && extent(Size::M, 150).height == 240, "ratio scale");
    check(snap(17) == 16 && snap(-7) == -8, "grid negative snap");
    check(overlaps({0, 0, 160, 160}, {170, 0, 160, 160}), "gutter collision");
    check(!overlaps({0, 0, 160, 160}, {176, 0, 160, 160}), "gutter boundary");
    auto placement = freePosition({1920, 1080}, {336, 160}, {{16, 16, 336, 160}});
    check(placement.has_value() && !overlaps(*placement, {16, 16, 336, 160}), "free placement");
    check(!freePosition({100, 100}, {160, 160}, {}), "no space");
    History history;
    history.add({0, 1, 100, true});
    history.add({1, 10, 0, true});
    check(std::abs(history.average(10).value() - 10) < .001, "time weighted history");
    history.add({10, 12, NAN, false});
    check(std::abs(history.average(12).value() - 10) < .001, "invalid not zero");
    for (int i = 12; i < 200; ++i)
      history.add({double(i), double(i + 1), 42, true});
    check(history.samples().size() <= 61, "history bounded");
    auto grid = calendarGrid(2026, 10, 0);
    check(grid.first == sys_days{year{2026} / 9 / 27} && grid.weeks == 5, "October calendar grid");
    check(calendarGrid(2026, 2, 0).weeks == 4, "four week month");
    check(calendarGrid(2026, 8, 0).weeks == 6, "six week month");
    check(parseRfc3339("2026-10-07T14:00:00+09:00") == parseRfc3339("2026-10-07T05:00:00Z"),
          "timezone offset");
    check(rfc3339(parseRfc3339("2026-10-07T05:00:00Z")) == "2026-10-07T05:00:00Z", "rfc roundtrip");
    check(localDate(parseRfc3339("2026-10-06T23:00:00Z"), L"Asia/Seoul") == year{2026} / 10 / 7,
          "local date");
    check(localMidnight(year{2026} / 10 / 7, L"Asia/Seoul") == parseRfc3339("2026-10-06T15:00:00Z"),
          "midnight seconds");
    Event allday;
    allday.allDay = true;
    allday.first = year{2026} / 10 / 7;
    allday.last = year{2026} / 10 / 9;
    check(eventOnDate(allday, year{2026} / 10 / 8, L"Asia/Seoul"), "multi day membership");
    check(!eventOnDate(allday, year{2026} / 10 / 9, L"Asia/Seoul"), "exclusive all day end");
    Event span = allday;
    span.first = year{2026} / 10 / 9;
    span.last = year{2026} / 10 / 13;
    auto segments = calendarSegments({span, allday}, grid, L"Asia/Seoul");
    check(segments.size() == 3 && segments[1].span == 2 && segments[2].span == 2,
          "multi-day bars split at week boundaries");
    check(parseRfc3339("2026-10-07T05:00:00garbageZ") == INT64_MIN &&
              parseRfc3339("2026-10-07T05:00:00.Z") == INT64_MIN,
          "malformed timestamp suffix rejected");
    check(parseRfc3339("2026-10-07T05:00:00.123Z") == parseRfc3339("2026-10-07T05:00:00Z"),
          "fractional seconds accepted");
    auto obs = kmaBase(parseRfc3339("2026-10-07T10:09:00+09:00"), 0);
    check(obs.date == "20261007" && obs.time == "0900", "observation publication");
    auto ultra = kmaBase(parseRfc3339("2026-10-07T10:44:00+09:00"), 1);
    check(ultra.time == "0930", "ultra publication");
    auto village = kmaBase(parseRfc3339("2026-10-07T02:09:00+09:00"), 2);
    check(village.date == "20261006" && village.time == "2300", "forecast previous date");
    check(std::isnan(kmaNumber(Json("-999"))) && std::isnan(kmaNumber(Json("1mm 미만"))),
          "weather missing categories");
    check(kmaNumber(Json("0")) == 0, "weather true zero");
    check(weatherCondition(3, 1) == L"눈" && weatherCondition(0, 4) == L"흐림", "KMA conditions");
    throws([] { kmaItems("{\"response\":{\"header\":{\"resultCode\":\"30\"}}}"); }, "KMA error code");
    check(pkceChallenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk") ==
              "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM",
          "RFC7636 test vector");
    auto event = parseEvent(
        Json::parse(
            R"({"id":"a","summary":"meeting","start":{"dateTime":"2026-10-07T14:00:00+09:00"},"end":{"dateTime":"2026-10-07T15:00:00+09:00"}})"),
        L"c", 0xff);
    check(event.end - event.start == 3600 && !event.allDay, "calendar parsing");
    Widget w;
    w.kind = Kind::Cpu;
    w.interval = 2;
    auto copy = readWidget(widgetJson(w));
    check(copy.id == w.id && copy.kind == w.kind, "widget persistence");
    w.size = Size::XL;
    check(!validateWidget(w), "XL calendar only");
    Widget corruptMonth;
    corruptMonth.kind = Kind::Calendar;
    corruptMonth.interval = 300;
    corruptMonth.source = L"2026-99";
    check(!validateWidget(corruptMonth), "malformed calendar month in imported layout rejected");
    Settings settings;
    settings.widgets.push_back(copy);
    auto j = settingsJson(settings, true);
    check(j.dump().find("client_secret") == std::string::npos &&
              j.dump().find("refresh_token") == std::string::npos,
          "export excludes credentials");
    auto duplicate = j;
    duplicate["widgets"].push_back(duplicate["widgets"][0]);
    throws([&] { readSettings(duplicate); }, "duplicate IDs rejected");
    auto sun = sunriseSunset(year{2026} / 10 / 7, 37.573, 126.979);
    check(sun.first > 5 && sun.first < 8 && sun.second > 17 && sun.second < 20, "Seoul sun plausibility");
    auto testRoot = std::filesystem::temp_directory_path() / (L"Tempos.CoreTests." + newId());
    Store store(testRoot);
    check(store.save(settings), "atomic settings save");
    check(store.load().widgets[0].id == copy.id, "settings read");
    check(store.saveSecret(L"test", "private-data"), "DPAPI save");
    check(store.secret(L"test") == "private-data", "DPAPI read");
    auto raw = boundedRead(testRoot / L"test.bin");
    check(raw && raw->find("private-data") == std::string::npos, "DPAPI ciphertext");
    store.eraseSecret(L"test");
    check(store.secret(L"test").empty(), "DPAPI erase");
    HttpClient http;
    auto denied = http.request(L"www.googleapis.com", L"/calendar/v3/calendars/primary/events", L"DELETE");
    check(denied.error != 0, "calendar mutation blocked");
    check(rfc3339(nowUnix()).size() == 20, "current timestamp bounded formatting");
    check(rfc3339(localMidnight(localDate(nowUnix()))).size() == 20, "local midnight bounded formatting");
    check(parseRfc3339("2026-10-07T-1:00:00Z") == INT64_MIN, "negative hour rejected");
    check(parseRfc3339("2026-10-07") == INT64_MIN, "event datetime requires offset");
    check(localMidnight(year{2026} / 3 / 9, L"America/New_York") -
                  localMidnight(year{2026} / 3 / 8, L"America/New_York") ==
              23 * 3600,
          "DST day length");
    Monitor primary{L"primary", L"", {0, 0, 1920, 1080}, 96, true};
    Monitor left{L"left", L"", {-2560, 0, 0, 1440}, 144, false};
    Widget first;
    first.monitor = primary.id;
    Widget second;
    second.monitor = left.id;
    auto originalX = second.x;
    auto positions = arrangeWidgets({first, second}, {primary, left});
    check(positions[1].visible && positions[1].monitor == left.id && !positions[1].temporary,
          "negative-origin mixed-DPI monitor retained");
    positions = arrangeWidgets({first, second}, {primary});
    check(positions[1].visible && positions[1].temporary && !overlaps(positions[0].box, positions[1].box),
          "disconnected monitor collision-free relocation");
    check(second.monitor == left.id && second.x == originalX,
          "temporary relocation preserves original layout");
    Widget oversized;
    oversized.kind = Kind::Calendar;
    oversized.size = Size::XL;
    oversized.scale = 200;
    positions = arrangeWidgets({oversized}, {primary});
    check(!positions[0].visible, "oversized widget safely parked");
    store.saveSecret(L"weather-key", "test-key");
    int weatherCalls = 0;
    auto envelope = [](Json items) {
      Json j;
      j["response"]["header"]["resultCode"] = "00";
      j["response"]["body"]["items"]["item"] = items;
      return j.dump();
    };
    auto todayKst = localDate(nowUnix(), L"Asia/Seoul");
    char dateText[16]{};
    sprintf_s(dateText, "%04d%02u%02u", int(todayKst.year()), unsigned(todayKst.month()),
              unsigned(todayKst.day()));
    HttpClient mockWeather(
        [&](const auto &, const std::wstring &path, const auto &method, const auto &, const auto &, auto) {
          ++weatherCalls;
          check(method == L"GET" && path.find(L"authKey=test-key") != std::wstring::npos,
                "KMA APIhub auth and GET");
          Json items = Json::array();
          if (path.find(L"getUltraSrtNcst") != std::wstring::npos) {
            for (auto pair : std::vector<std::pair<std::string, std::string>>{
                     {"T1H", "0"}, {"REH", "85"}, {"PTY", "3"}, {"WSD", "1.2"}})
              items.push_back({{"category", pair.first},
                               {"obsrValue", pair.second},
                               {"baseDate", dateText},
                               {"baseTime", "0000"}});
          } else if (path.find(L"getVilageFcst") != std::wstring::npos) {
            items.push_back({{"category", "TMN"}, {"fcstValue", "-3"}, {"fcstDate", dateText}});
            items.push_back({{"category", "TMX"}, {"fcstValue", "4"}, {"fcstDate", dateText}});
          }
          return Response{200, 0, envelope(items), 0};
        });
    WeatherService service(store, mockWeather);
    Widget weather;
    weather.kind = Kind::Weather;
    auto snow = service.read(weather, {});
    check(snow.status == Status::Ready && snow.weather->temperature == 0 && snow.weather->condition == L"눈",
          "actual weather parser distinguishes freezing zero and snow");
    auto calls = weatherCalls;
    service.read(weather, {});
    check(weatherCalls == calls, "same-grid weather cache avoids repeat requests");
    HttpClient deniedWeather([](const auto &, const auto &, const auto &, const auto &, const auto &, auto) {
      return Response{200, 0, R"({"response":{"header":{"resultCode":"30"}}})", 0};
    });
    WeatherService invalidKey(store, deniedWeather);
    auto offline = invalidKey.read(weather, {});
    check(offline.status == Status::Stale && offline.weather->temperature == 0,
          "restart restores offline weather without inventing values");
    throws(
        [] {
          kmaItems(
              "<OpenAPI_ServiceResponse><returnReasonCode>30</returnReasonCode></OpenAPI_ServiceResponse>");
        },
        "XML gateway error handled");
    check(offline.message.find(L"인증키") != std::wstring::npos, "invalid KMA key explained");
    Store pagingStore(testRoot / L"paging");
    pagingStore.saveSecret(L"weather-key", "test+key/&");
    int observationPages = 0;
    bool pageFailure = false;
    HttpClient pages([&](const auto &, const std::wstring &path, const auto &, const auto &, const auto &,
                         auto) {
      if (path.find(L"getUltraSrtNcst") == std::wstring::npos)
        return Response{200, 0, envelope(Json::array()), 0};
      ++observationPages;
      check(path.find(L"authKey=test%2Bkey%2F%26") != std::wstring::npos, "KMA key is encoded exactly once");
      bool second = path.find(L"pageNo=2") != std::wstring::npos;
      if (second && pageFailure)
        return Response{503, 0, "", 0};
      Json items = Json::array();
      items.push_back({{"category", second ? "WSD" : "T1H"},
                       {"obsrValue", second        ? "2.5"
                                     : pageFailure ? "17"
                                                   : "10"},
                       {"baseDate", dateText},
                       {"baseTime", "0100"}});
      auto page = Json::parse(envelope(items));
      page["response"]["body"]["pageNo"] = second ? 2 : 1;
      page["response"]["body"]["totalCount"] = 2;
      return Response{200, 0, page.dump(), 0};
    });
    WeatherService paged(pagingStore, pages);
    auto firstPage = paged.read(weather, {});
    check(observationPages == 2 && firstPage.weather->temperature == 10 && firstPage.weather->wind == 2.5,
          "KMA pagination combines complete observations");
    pageFailure = true;
    paged.invalidate();
    auto interruptedPage = paged.read(weather, {});
    check(interruptedPage.status == Status::Stale && interruptedPage.weather->temperature == 10,
          "failed KMA page cannot replace complete snapshot");
    int quotaCalls = 0;
    HttpClient quota([&](const auto &, const auto &, const auto &, const auto &, const auto &, auto) {
      ++quotaCalls;
      return Response{429, 0, "", 3600};
    });
    WeatherService limited(pagingStore, quota);
    limited.read(weather, {});
    limited.read(weather, {});
    check(quotaCalls == 1, "KMA Retry-After suppresses repeated calls");
    store.saveSecret(L"calendar-client", "client.apps.googleusercontent.com");
    store.saveSecret(L"calendar-refresh", "fake-refresh");
    store.savePrivate(L"calendar-account",
                      {{"calendars", Json::array({{{"id", "primary"}, {"name", "개인"}}})},
                       {"selected", Json::array({"primary"})}});
    int pageCount = 0;
    bool failSecond = false;
    HttpClient mockCalendar([&](const std::wstring &host, const std::wstring &path,
                                const std::wstring &method, const std::string &, const auto &, auto) {
      if (host == L"oauth2.googleapis.com")
        return Response{200, 0, R"({"access_token":"fake-access","expires_in":3600})", 0};
      check(method == L"GET", "calendar HTTP read-only");
      ++pageCount;
      bool secondPage = path.find(L"pageToken=page2") != std::wstring::npos;
      if (secondPage && failSecond)
        return Response{503, 0, "", 0};
      auto eventJson = Json{{"id", secondPage ? "two" : "one"},
                            {"summary", secondPage ? "두 번째" : "첫 번째"},
                            {"start", {{"dateTime", "2026-10-08T14:00:00+09:00"}}},
                            {"end", {{"dateTime", "2026-10-08T15:00:00+09:00"}}}};
      Json page{{"items", Json::array({eventJson})}};
      if (!secondPage)
        page["nextPageToken"] = "page2";
      return Response{200, 0, page.dump(), 0};
    });
    CalendarService calendar(store, mockCalendar);
    Widget calendarWidget;
    calendarWidget.kind = Kind::Calendar;
    auto complete = calendar.read(calendarWidget, {});
    check(pageCount == 2 && complete.calendar->complete && complete.calendar->events.size() == 2,
          "calendar atomic pagination");
    failSecond = true;
    auto partial = calendar.read(calendarWidget, {});
    check(partial.status == Status::Stale && partial.calendar->events.size() == 2,
          "failed page retains full previous snapshot");
    calendar.disconnect();
    check(store.secret(L"calendar-refresh").empty() && store.secret(L"calendar-events").empty(),
          "disconnect clears private cache and token");
    check(calendar.read(calendarWidget, {}).calendar->events.empty(),
          "disconnected calendar renders local dates only");
    store.saveSecret(L"calendar-refresh", "fake-expired");
    store.savePrivate(L"calendar-account",
                      {{"calendars", Json::array({{{"id", "primary"}, {"name", "개인"}}})},
                       {"selected", Json::array({"primary"})}});
    HttpClient expired([](const auto &, const auto &, const auto &, const auto &, const auto &, auto) {
      return Response{400, 0, R"({"error":"invalid_grant"})", 0};
    });
    CalendarService expiredAccount(store, expired);
    expiredAccount.read(calendarWidget, {});
    check(!expiredAccount.account().connected && store.secret(L"calendar-refresh").empty(),
          "expired refresh token prompts reconnect");
    store.saveSecret(L"calendar-refresh", "fake-racing");
    store.savePrivate(L"calendar-account",
                      {{"calendars", Json::array({{{"id", "primary"}, {"name", "개인"}}})},
                       {"selected", Json::array({"primary"})}});
    CalendarService *racingService = nullptr;
    HttpClient racing([&](const std::wstring &host, const auto &, const auto &, const auto &, const auto &,
                          auto) {
      if (host == L"oauth2.googleapis.com")
        return Response{200, 0, R"({"access_token":"test","expires_in":3600})", 0};
      racingService->disconnect();
      return Response{
          200, 0,
          R"({"items":[{"id":"late","summary":"늦은 응답","start":{"date":"2026-10-08"},"end":{"date":"2026-10-09"}}]})",
          0};
    });
    CalendarService raced(store, racing);
    racingService = &raced;
    auto staleResponse = raced.read(calendarWidget, {});
    check(staleResponse.calendar->events.empty() && store.secret(L"calendar-events").empty(),
          "late calendar response cannot resurrect disconnected data");
    std::cout << "PASS " << passed << " checks\n";
    CoUninitialize();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL after " << passed << ": " << e.what() << "\n";
    CoUninitialize();
    return 1;
  }
}
