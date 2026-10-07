#include <winsock2.h>
#include <ws2tcpip.h>
#include "calendar.h"
#include <bcrypt.h>
#include <shellapi.h>
#include <wincrypt.h>
#include <set>
namespace tempos {
using namespace std::chrono;
static std::string base64(const unsigned char *data, DWORD size) {
  DWORD n = 0;
  CryptBinaryToStringA(data, size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &n);
  std::string out(n, '\0');
  CryptBinaryToStringA(data, size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, out.data(), &n);
  out.resize(n);
  while (!out.empty() && (out.back() == '=' || out.back() == '\0'))
    out.pop_back();
  std::replace(out.begin(), out.end(), '+', '-');
  std::replace(out.begin(), out.end(), '/', '_');
  return out;
}
static std::string randomToken() {
  unsigned char b[32]{};
  if (BCryptGenRandom(nullptr, b, sizeof b, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
    throw std::runtime_error("random");
  return base64(b, sizeof b);
}
std::string pkceChallenge(const std::string &v) {
  unsigned char b[32]{};
  if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char *>(v.data())),
                 ULONG(v.size()), b, sizeof b) < 0)
    throw std::runtime_error("hash");
  return base64(b, sizeof b);
}
static year_month_day parseDate(const std::string &s) {
  int y = 0, m = 0, d = 0;
  if (sscanf_s(s.c_str(), "%d-%d-%d", &y, &m, &d) != 3)
    throw std::runtime_error("date");
  year_month_day date{year{y}, month{unsigned(m)}, day{unsigned(d)}};
  if (!date.ok())
    throw std::runtime_error("date");
  return date;
}
Event parseEvent(const Json &j, const std::wstring &calendar, uint32_t color) {
  Event e;
  e.id = wide(j.at("id").get<std::string>());
  e.calendar = calendar;
  e.color = color;
  e.title = wide(j.value("summary", std::string("(제목 없음)")));
  if (e.title.size() > 512)
    e.title.resize(512);
  auto &s = j.at("start");
  auto &t = j.at("end");
  if (s.contains("date")) {
    e.allDay = true;
    e.first = parseDate(s.at("date").get<std::string>());
    e.last = parseDate(t.at("date").get<std::string>());
    if (sys_days{e.last} <= sys_days{e.first})
      throw std::runtime_error("event interval");
  } else {
    e.start = parseRfc3339(s.at("dateTime").get<std::string>());
    e.end = parseRfc3339(t.at("dateTime").get<std::string>());
    if (e.start == INT64_MIN || e.end == INT64_MIN || e.end <= e.start)
      throw std::runtime_error("event interval");
  }
  return e;
}
CalendarService::CalendarService(Store &s, HttpClient &h, bool fixture)
    : store_(s), http_(h), fixture_(fixture) {
  auto j = store_.loadPrivate(L"calendar-account");
  try {
    if (j.is_object()) {
      for (auto &c : j.value("calendars", Json::array()))
        account_.calendars.push_back({wide(c.at("id").get<std::string>()),
                                      wide(c.at("name").get<std::string>()),
                                      c.value("color", uint32_t(0x78b9ff))});
      for (auto &c : j.value("selected", Json::array()))
        account_.selected.push_back(wide(c.get<std::string>()));
    }
  } catch (...) {
    account_ = {};
  }
  account_.connected = !store_.secret(L"calendar-refresh").empty();
  account_.status = account_.connected ? L"연결됨 · 조회 전용" : L"연결 안 됨";
}
CalendarAccount CalendarService::account() const {
  std::lock_guard g(mutex_);
  return account_;
}
void CalendarService::saveAccount() {
  Json j;
  j["calendars"] = Json::array();
  for (auto &c : account_.calendars)
    j["calendars"].push_back({{"id", utf8(c.id)}, {"name", utf8(c.name)}, {"color", c.color}});
  j["selected"] = Json::array();
  for (auto &c : account_.selected)
    j["selected"].push_back(utf8(c));
  store_.savePrivate(L"calendar-account", j);
}
void CalendarService::select(const std::vector<std::wstring> &fds) {
  std::lock_guard g(mutex_);
  account_.selected.clear();
  for (auto &id : fds)
    if (std::any_of(account_.calendars.begin(), account_.calendars.end(),
                    [&](auto &c) { return c.id == id; }))
      account_.selected.push_back(id);
  ++generation_;
  store_.eraseSecret(L"calendar-events");
  saveAccount();
}
void CalendarService::disconnect() {
  http_.cancel();
  std::lock_guard g(mutex_);
  ++generation_;
  SecureZeroMemory(access_.data(), access_.size());
  access_.clear();
  expiry_ = 0;
  account_ = {};
  account_.status = L"연결 해제됨";
  store_.eraseSecret(L"calendar-refresh");
  store_.eraseSecret(L"calendar-account");
  store_.eraseSecret(L"calendar-events");
}
std::string CalendarService::token(std::stop_token stop) {
  uint64_t generation;
  {
    std::lock_guard g(mutex_);
    if (!access_.empty() && expiry_ > nowUnix() + 60)
      return access_;
    generation = generation_;
  }
  auto refresh = store_.secret(L"calendar-refresh"), id = store_.secret(L"calendar-client"),
       secret = store_.secret(L"calendar-client-secret");
  if (refresh.empty() || id.empty())
    return {};
  std::string body =
      "client_id=" + urlEncode(id) + "&refresh_token=" + urlEncode(refresh) + "&grant_type=refresh_token";
  if (!secret.empty())
    body += "&client_secret=" + urlEncode(secret);
  auto r = http_.request(L"oauth2.googleapis.com", L"/token", L"POST", body,
                         L"Content-Type: application/x-www-form-urlencoded\r\n", stop);
  SecureZeroMemory(body.data(), body.size());
  if (!r.ok()) {
    std::lock_guard g(mutex_);
    if (generation != generation_)
      return {};
    if (r.status == 400 || r.status == 401) {
      account_.connected = false;
      account_.status = L"다시 연결하세요 · 인증 만료";
      store_.eraseSecret(L"calendar-refresh");
    }
    throw std::runtime_error("token");
  }
  auto j = Json::parse(r.body);
  std::lock_guard g(mutex_);
  if (generation != generation_)
    return {};
  access_ = j.at("access_token").get<std::string>();
  expiry_ = nowUnix() + std::clamp(j.value("expires_in", 3600), 60, 86400);
  return access_;
}
Response CalendarService::api(const std::wstring &path, std::stop_token stop) {
  if (nowUnix() < retryAt_)
    throw std::runtime_error("retry wait");
  auto access = token(stop);
  if (access.empty())
    throw std::runtime_error("not connected");
  auto headers = L"Authorization: Bearer " + wide(access) + L"\r\n";
  auto r = http_.request(L"www.googleapis.com", path, L"GET", {}, headers, stop);
  SecureZeroMemory(headers.data(), headers.size() * sizeof(wchar_t));
  if (!r.ok()) {
    if (r.status == 429 || r.status >= 500)
      retryAt_ = nowUnix() + std::max(60, r.retryAfter);
    if (r.status == 401) {
      std::lock_guard g(mutex_);
      SecureZeroMemory(access_.data(), access_.size());
      access_.clear();
      expiry_ = 0;
      account_.status = L"인증을 다시 확인하는 중";
    } else if (r.status == 403) {
      std::lock_guard g(mutex_);
      account_.status = L"API 사용 설정·조회 권한·요청 한도를 확인하세요";
      retryAt_ = nowUnix() + std::max(300, r.retryAfter);
    }
    throw std::runtime_error(r.status == 429 ? "rate limit" : "calendar request");
  }
  return r;
}
static std::string decode(std::string_view input) {
  std::string s;
  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] == '%' && i + 2 < input.size()) {
      unsigned v = 0;
      auto pair = std::string(input.substr(i + 1, 2));
      if (sscanf_s(pair.c_str(), "%x", &v) == 1) {
        s += char(v);
        i += 2;
        continue;
      }
    }
    s += input[i] == '+' ? ' ' : input[i];
  }
  return s;
}
void CalendarService::connect(std::stop_token stop) {
  std::lock_guard requests(requestMutex_);
  retryAt_ = 0;
  uint64_t generation;
  {
    std::lock_guard g(mutex_);
    generation = ++generation_;
    account_.status = L"브라우저에서 연결 승인 대기";
  }
  auto setError = [&](const wchar_t *m) {
    std::lock_guard g(mutex_);
    if (generation == generation_)
      account_.status = m;
  };
  WSADATA wd{};
  if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) {
    setError(L"연결 준비 실패");
    return;
  }
  struct Cleanup {
    SOCKET listener = INVALID_SOCKET;
    ~Cleanup() {
      if (listener != INVALID_SOCKET)
        closesocket(listener);
      WSACleanup();
    }
  } cleanup;
  try {
    auto id = store_.secret(L"calendar-client");
    if (id.empty())
      throw std::runtime_error("client ID");
    cleanup.listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (cleanup.listener == INVALID_SOCKET ||
        bind(cleanup.listener, reinterpret_cast<sockaddr *>(&address), sizeof address) == SOCKET_ERROR ||
        listen(cleanup.listener, 2) == SOCKET_ERROR)
      throw std::runtime_error("listener");
    int len = sizeof address;
    getsockname(cleanup.listener, reinterpret_cast<sockaddr *>(&address), &len);
    std::string redirect = "http://127.0.0.1:" + std::to_string(ntohs(address.sin_port)) + "/callback",
                verifier = randomToken(), state = randomToken();
    std::string url = "https://accounts.google.com/o/oauth2/v2/auth?client_id=" + urlEncode(id) +
                      "&redirect_uri=" + urlEncode(redirect) + "&response_type=code&scope=" +
                      urlEncode("https://www.googleapis.com/auth/calendar.calendarlist.readonly "
                                "https://www.googleapis.com/auth/calendar.events.readonly") +
                      "&code_challenge=" + pkceChallenge(verifier) +
                      "&code_challenge_method=S256&state=" + state + "&access_type=offline&prompt=consent";
    if (reinterpret_cast<INT_PTR>(
            ShellExecuteW(nullptr, L"open", wide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
      throw std::runtime_error("browser");
    auto deadline = Clock::now() + 180s;
    std::string code;
    while (!stop.stop_requested() && Clock::now() < deadline) {
      {
        std::lock_guard g(mutex_);
        if (generation != generation_)
          return;
      }
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(cleanup.listener, &fds);
      timeval tv{0, 250000};
      if (::select(0, &fds, nullptr, nullptr, &tv) <= 0)
        continue;
      SOCKET client = accept(cleanup.listener, nullptr, nullptr);
      if (client == INVALID_SOCKET)
        continue;
      DWORD timeout = 1500;
      setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char *>(&timeout), sizeof timeout);
      std::string request;
      char buffer[2048];
      while (request.size() < 8192 && request.find("\r\n\r\n") == std::string::npos) {
        int n = recv(client, buffer, sizeof buffer, 0);
        if (n <= 0)
          break;
        request.append(buffer, n);
      }
      bool valid = false;
      auto end = request.find(' ', 4);
      if (request.rfind("GET /callback?", 0) == 0 && end != std::string::npos) {
        std::map<std::string, std::string> fields;
        auto query = request.substr(14, end - 14);
        size_t start = 0;
        while (start < query.size()) {
          auto amp = query.find('&', start);
          auto part = query.substr(start, amp == std::string::npos ? amp : amp - start);
          auto eq = part.find('=');
          if (eq != std::string::npos)
            fields[part.substr(0, eq)] = decode(part.substr(eq + 1));
          if (amp == std::string::npos)
            break;
          start = amp + 1;
        }
        valid = fields["state"] == state && !fields["code"].empty();
        if (valid)
          code = fields["code"];
      }
      const char *reply = valid ? "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nConnection: "
                                  "close\r\n\r\nTempos: 인증 응답을 받았습니다. 앱으로 돌아가세요."
                                : "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\nInvalid request";
      send(client, reply, int(strlen(reply)), 0);
      closesocket(client);
      if (valid)
        break;
    }
    if (code.empty())
      throw std::runtime_error("cancelled");
    std::string body = "client_id=" + urlEncode(id) + "&code=" + urlEncode(code) +
                       "&code_verifier=" + verifier + "&redirect_uri=" + urlEncode(redirect) +
                       "&grant_type=authorization_code";
    auto secret = store_.secret(L"calendar-client-secret");
    if (!secret.empty())
      body += "&client_secret=" + urlEncode(secret);
    auto r = http_.request(L"oauth2.googleapis.com", L"/token", L"POST", body,
                           L"Content-Type: application/x-www-form-urlencoded\r\n", stop);
    SecureZeroMemory(body.data(), body.size());
    if (!r.ok())
      throw std::runtime_error("exchange");
    auto j = Json::parse(r.body);
    {
      std::lock_guard g(mutex_);
      if (generation != generation_)
        return;
      access_ = j.at("access_token").get<std::string>();
      expiry_ = nowUnix() + j.value("expires_in", 3600);
      if (!j.contains("refresh_token") ||
          !store_.saveSecret(L"calendar-refresh", j.at("refresh_token").get<std::string>()))
        throw std::runtime_error("refresh token");
      account_.connected = true;
    }
    std::vector<Device> calendars;
    std::string page;
    do {
      auto result = api(L"/calendar/v3/users/me/"
                        L"calendarList?maxResults=250&showDeleted=false&minAccessRole=reader&fields="
                        L"nextPageToken,items(id,summary,backgroundColor)" +
                            (page.empty() ? L"" : L"&pageToken=" + wide(urlEncode(page))),
                        stop);
      auto list = Json::parse(result.body);
      for (auto &c : list.value("items", Json::array())) {
        if (calendars.size() >= 250)
          throw std::runtime_error("calendar limit");
        uint32_t color = 0x78b9ff;
        auto hex = c.value("backgroundColor", std::string{});
        if (hex.size() == 7 && hex[0] == '#' &&
            hex.find_first_not_of("0123456789abcdefABCDEF", 1) == std::string::npos)
          color = uint32_t(std::stoul(hex.substr(1), nullptr, 16));
        calendars.push_back(
            {wide(c.at("id").get<std::string>()), wide(c.value("summary", std::string("캘린더"))), color});
      }
      page = list.value("nextPageToken", std::string{});
    } while (!page.empty() && !stop.stop_requested());
    {
      std::lock_guard g(mutex_);
      if (generation != generation_)
        return;
      account_.calendars = std::move(calendars);
      account_.selected.clear();
      if (!account_.calendars.empty())
        account_.selected.push_back(account_.calendars.front().id);
      account_.status = L"연결됨 · 조회 전용";
      saveAccount();
    }
  } catch (...) {
    setError(stop.stop_requested() ? L"연결 취소됨"
                                   : L"연결 실패 · 클라이언트 정보와 브라우저 승인을 확인하세요");
  }
}
Snapshot CalendarService::read(const Widget &w, std::stop_token stop) {
  std::lock_guard requests(requestMutex_);
  Snapshot s;
  auto d = std::make_shared<CalendarData>();
  auto now = nowUnix();
  auto date = localDate(now, w.timezone);
  int y = int(date.year());
  unsigned m = unsigned(date.month());
  if (!w.source.empty()) {
    int ym = 0, mm = 0;
    if (swscanf_s(w.source.c_str(), L"%d-%d", &ym, &mm) == 2 && ym >= 1900 && ym <= 2200 && mm >= 1 &&
        mm <= 12) {
      y = ym;
      m = unsigned(mm);
    }
  }
  auto grid = calendarGrid(y, m, w.weekStart);
  auto first = w.size == Size::L || w.size == Size::XL ? year_month_day{grid.first} : date;
  auto last = w.size == Size::L || w.size == Size::XL
                  ? year_month_day{grid.first + days{grid.weeks * 7}}
                  : year_month_day{sys_days{date} + days{w.size == Size::M ? 1 : 14}};
  int64_t from = localMidnight(first, w.timezone), to = localMidnight(last, w.timezone);
  uint64_t generation;
  CalendarAccount account;
  {
    std::lock_guard g(mutex_);
    generation = generation_;
    account = account_;
  }
  if (fixture_) {
    d->connected = d->complete = true;
    auto start = localMidnight(date, w.timezone);
    d->events.push_back(
        {L"meeting", L"fixture", L"팀 미팅", start + 14 * 3600, start + 15 * 3600, {}, {}, false, 0x78b9ff});
    d->events.push_back({L"exercise",
                         L"fixture",
                         L"운동",
                         start + 18 * 3600 + 1800,
                         start + 19 * 3600,
                         {},
                         {},
                         false,
                         0x79e5b8});
    Event e;
    e.id = L"anniversary";
    e.title = L"기념일";
    e.allDay = true;
    e.first = date;
    e.last = year_month_day{sys_days{date} + days{1}};
    e.color = 0xf9d76a;
    d->events.push_back(e);
    e.id = L"workshop";
    e.title = L"워크숍";
    e.first = year_month_day{sys_days{date} + days{4}};
    e.last = year_month_day{sys_days{date} + days{7}};
    e.color = 0x7bd8d8;
    d->events.push_back(e);
    s.status = Status::Ready;
  } else if (!account.connected) {
    s.status = Status::Ready;
    d->complete = true;
  } else
    try {
      d->connected = true;
      std::vector<std::wstring> selected = w.calendars.empty() ? account.selected : w.calendars;
      Json cached = Json::array();
      size_t bytes = 0;
      std::set<std::wstring> fds;
      for (size_t index = 0; index < selected.size() && index < 32; ++index) {
        auto &calendar = selected[index];
        auto source = std::find_if(account.calendars.begin(), account.calendars.end(),
                                   [&](auto &c) { return c.id == calendar; });
        uint32_t color = source == account.calendars.end() ? palettes()[index % 9].bottom : source->color;
        std::string page;
        std::set<std::string> seenPages;
        do {
          std::wstring path =
              L"/calendar/v3/calendars/" + wide(urlEncode(utf8(calendar))) +
              L"/events?singleEvents=true&orderBy=startTime&showDeleted=false&maxResults=250&timeMin=" +
              wide(urlEncode(rfc3339(from))) + L"&timeMax=" + wide(urlEncode(rfc3339(to))) +
              L"&fields=nextPageToken,items(id,summary,status,start,end)";
          if (!page.empty())
            path += L"&pageToken=" + wide(urlEncode(page));
          auto r = api(path, stop);
          bytes += r.body.size();
          if (bytes > 3 * 1024 * 1024)
            throw std::runtime_error("event limit");
          auto j = Json::parse(r.body);
          for (auto &item : j.value("items", Json::array())) {
            if (item.value("status", std::string{}) == "cancelled")
              continue;
            try {
              auto event = parseEvent(item, calendar, color);
              if (fds.insert(calendar + L":" + event.id).second) {
                d->events.push_back(std::move(event));
                Json minimal = Json::object();
                for (auto field : {"id", "summary", "start", "end"})
                  if (item.contains(field))
                    minimal[field] = item[field];
                cached.push_back({{"calendar", utf8(calendar)}, {"color", color}, {"event", minimal}});
              }
            } catch (...) {
              throw std::runtime_error("invalid event");
            }
            if (d->events.size() > 5000)
              throw std::runtime_error("event limit");
          }
          page = j.value("nextPageToken", std::string{});
          if (!page.empty() && !seenPages.insert(page).second)
            throw std::runtime_error("pagination loop");
        } while (!page.empty() && !stop.stop_requested());
        if (stop.stop_requested())
          throw std::runtime_error("cancelled");
      }
      d->complete = true;
      d->fetched = now;
      {
        std::lock_guard g(mutex_);
        if (generation != generation_) {
          s.status = Status::Ready;
          s.calendar = std::make_shared<CalendarData>();
          return s;
        }
        store_.savePrivate(L"calendar-events", {{"from", from}, {"to", to}, {"at", now}, {"items", cached}});
      }
      s.status = Status::Ready;
    } catch (...) {
      s.status = Status::Stale;
      auto cache = store_.loadPrivate(L"calendar-events");
      try {
        if (cache.value("from", int64_t{}) == from && cache.value("to", int64_t{}) == to &&
            now - cache.value("at", int64_t{}) < 86400) {
          d->events.clear();
          for (auto &item : cache.at("items"))
            d->events.push_back(parseEvent(item.at("event"), wide(item.at("calendar").get<std::string>()),
                                           item.at("color").get<uint32_t>()));
          d->fetched = cache.at("at").get<int64_t>();
          d->complete = true;
        } else
          d->events.clear();
      } catch (...) {
        d->events.clear();
      }
      s.message = L"저장된 일정";
    }
  {
    std::lock_guard g(mutex_);
    if (generation != generation_)
      d = std::make_shared<CalendarData>();
  }
  s.calendar = d;
  return s;
}
} // namespace tempos
